#include <GeoKernel.Globe/RuntimeBasemaps.h>
#include <GeoKernel.Globe/RuntimeFactory.h>
#include <GeoKernel.Globe/RuntimeSceneImport.h>
#include <GeoKernel.Globe/RuntimeVectorPolygonFill.h>
#include <GeoKernel.Globe/RuntimeVectorPresentation.h>
#include <GeoKernel.Globe/RuntimeWindow.h>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFutureWatcher>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QStatusBar>
#include <QToolBar>
#include <QVulkanInstance>
#include <QtConcurrent>
#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Runtime = GeoKernel3D::GlobeRuntime;

inline void initializeDataPaths()
{
    const auto root = QCoreApplication::applicationDirPath();
    qputenv("PROJ_DATA", (root + "/proj-data").toUtf8());
    qputenv("GDAL_DATA", (root + "/gdal-data").toUtf8());
    qputenv("GDAL_DRIVER_PATH", (root + "/gdalplugins").toUtf8());
}

inline void require(bool condition, const QString &message)
{
    if (!condition)
        throw std::runtime_error(message.toStdString());
}

class BuildingWindow : public QMainWindow
{
  public:
    Runtime::RuntimeWindow *globe;
    QToolBar *bar;
    std::atomic_bool cancelled{false};
    bool busy = false;

    explicit BuildingWindow(QVulkanInstance &instance, const QString &title)
    {
        Runtime::RuntimeOptions options;
        options.terrainEnabled = false;
        options.terrainExaggeration = 1.0f;
        options.spaceBackgroundEnabled = false;
        options.sunlight = false;
        options.networkEnabled = true;
        options.initialTileSource = Runtime::runtimeTileSourceNames().indexOf("Esri Satellite");
        options.atmosphereEnabled = true;
        globe = new Runtime::RuntimeWindow(options);
        globe->setVulkanInstance(&instance);
        setCentralWidget(QWidget::createWindowContainer(globe));
        bar = addToolBar("Controls");
        setWindowTitle(title);
        resize(1100, 760);
        statusBar()->showMessage("Loading Sagrada Familia buildings...");
    }

    ~BuildingWindow() override
    {
        cancelled = true;
        QThreadPool::globalInstance()->waitForDone();
        delete takeCentralWidget();
    }
    
    template <class F> void action(const QString &name, F callback)
    {
        auto *a = bar->addAction(name);
        connect(a, &QAction::triggered, this, [this, callback] {
            if (busy)
                return;
            try
            {
                callback();
            }
            catch (const std::exception &e)
            {
                report(e.what());
            }
        });
    }

    void report(const QString &message)
    {
        statusBar()->showMessage(message);
    }

    template <class Work, class Done> void task(Work work, Done done)
    {
        if (busy)
            return;
        busy = true;
        cancelled = false;
        using T = decltype(work());
        auto *watcher = new QFutureWatcher<T>(this);
        connect(watcher, &QFutureWatcher<T>::finished, this, [this, watcher, done] {
            busy = false;
            try
            {
                auto result = watcher->result();
                if (!cancelled)
                {
                    done(std::move(result));
                }
            }
            catch (const std::exception &e)
            {
                report(e.what());
            }
            catch (...)
            {
                report("SDK worker failed");
            }
            watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run(std::move(work)));
    }
};

struct BuildingResult
{
    std::vector<std::shared_ptr<const Runtime::GlobeSceneMesh>> meshes;
    QString error;
    size_t count = 0;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico")));
    initializeDataPaths();

    QVulkanInstance instance;
    if (!instance.create())
    {
        std::cerr << "Vulkan initialization failed\n";
        return 1;
    }

    try
    {
        BuildingWindow w(instance, "Globe — Sagrada Familia Buildings");

        auto data = std::make_shared<Runtime::VectorImportResult>();
        auto buildings = std::make_shared<BuildingResult>();
        auto rejected = std::make_shared<int>(0);
        auto *extrude = w.bar->addAction("Extrude");
        extrude->setCheckable(true);
        extrude->setEnabled(false);
        auto *height = new QDoubleSpinBox;
        height->setRange(1, 300);
        height->setValue(18);
        height->setSuffix(" m");
        height->setToolTip("Display height; this source has no measured building heights");
        w.bar->addWidget(new QLabel(" Height: "));
        w.bar->addWidget(height);
        const auto path = QCoreApplication::applicationDirPath() + "/data/buildings.shp";
        auto restore = [&w, data, extrude, height, rejected] {
            w.globe->setSceneLayers({});
            w.globe->setVectorLayers({data->mesh});
            extrude->setChecked(false);
            extrude->setEnabled(true);
            height->setEnabled(true);
            w.statusBar()->showMessage(QString("%1 buildings | %2 outline-only | Click Extrude for 3D")
                                           .arg(data->features)
                                           .arg(*rejected));
        };

        QObject::connect(
            extrude, &QAction::triggered, &w, [&w, data, buildings, extrude, height, path, restore](bool on) {
                if (w.busy)
                    return;
                if (!on)
                {
                    restore();
                    return;
                }
                extrude->setEnabled(false);
                height->setEnabled(false);
                w.statusBar()->showMessage("Extruding buildings...");
                const double metres = height->value();
                w.task(
                    [&w, data, path, metres] {
                        BuildingResult result;
                        const QJsonObject state{
                            {"kind", "buildings"},
                            {"mappedAttributes", true},
                            {"name", "Sagrada Familia"},
                            {"path", path},
                            {"visible", true},
                            {"sourceLayer", 0},
                            {"sourceCrs", "EPSG:25831"},
                            {"heightField", ""},
                            {"baseField", ""},
                            {"baseMode", "currentTerrain"},
                            {"demPath", ""},
                            {"defaultHeight", metres},
                            {"heightMultiplier", 1},
                            {"buildingColors", QJsonObject{{"mode", "uniform"}, {"fallback", "#e8d6b4"}}}};
                        Runtime::VectorImportResult page;
                        page.path = data->path;
                        page.sourceIdentity = data->sourceIdentity;

                        const auto sphereBase = [](auto, double &base) {
                            base = 0.;
                            return true;
                        };

                        size_t coordinates = 0;
                        auto flush = [&] {
                            if (page.polygons.empty())
                                return true;
                            auto layer = Runtime::importBuildingPage(page, state, sphereBase, w.cancelled);
                            if (!layer.error.isEmpty() || !layer.mesh)
                            {
                                result.error = layer.error;
                                return false;
                            }
                            result.count += page.polygons.size();
                            result.meshes.push_back(layer.mesh);
                            page.polygons.clear();
                            coordinates = 0;
                            return true;
                        };

                        for (const auto &polygon : data->renderPolygons)
                        {
                            if (w.cancelled)
                            {
                                result.error = "Cancelled";
                                return result;
                            }
                            size_t count = 0;
                            for (const auto &ring : polygon.rings)
                                count += ring.size();
                            if (page.polygons.size() >= 128 || coordinates + count > 32768)
                                if (!flush())
                                    return result;
                            page.polygons.push_back(polygon);
                            coordinates += count;
                        }

                        if (!flush())
                            return result;

                        Runtime::GlobeSceneLayer combined;
                        combined.chunks = result.meshes;
                        Runtime::sceneLayerBudget({combined}, result.error);
                        return result;
                    },
                    [&w, buildings, extrude, restore, metres](auto result) {
                        if (!result.error.isEmpty())
                        {
                            restore();
                            w.report(result.error);
                            return;
                        }
                        require(!result.meshes.empty(), "No buildings extruded");
                        *buildings = std::move(result);
                        w.globe->setVectorLayers({});
                        w.globe->setSceneLayers(buildings->meshes);
                        extrude->setEnabled(true);
                        w.statusBar()->showMessage(QString("%1 extruded polygons | Display height %2 m (not "
                                                           "measured) | Click Extrude for footprints")
                                                       .arg(buildings->count)
                                                       .arg(metres));
                    });
            });

        w.action("Zoom to buildings", [&w, data] {
            if (data->mesh)
                w.globe->setCamera(data->camera);
        });

        w.action("Atmosphere", [&w] {
            w.globe->toggleAtmosphere();
        });

        w.action("Esri on/off", [&w] {
            w.globe->setTileSource(
                w.globe->tileSource() < 0 ? Runtime::runtimeTileSourceNames().indexOf("Esri Satellite") : -1);
        });

        w.statusBar()->addPermanentWidget(new QLabel(
            Runtime::Basemaps::attribution(Runtime::runtimeTileSourceNames().indexOf("Esri Satellite"))));

        w.task(
            [&w, path, rejected] {
                auto result = Runtime::importVector(path, 0, "EPSG:25831", w.cancelled);
                if (!result.error.isEmpty() || !result.mesh)
                    return result;
                for (const auto &polygon : result.polygons)
                {
                    if (w.cancelled)
                        return result;
                    Runtime::GlobeVectorMesh scratch;
                    scratch.origin = result.mesh->origin;
                    QString error;
                    if (Runtime::appendVectorPolygonFill(scratch, polygon, QColor("#ffffff"), error,
                                                         &w.cancelled))
                        result.renderPolygons.push_back(polygon);
                    else
                        ++*rejected;
                }
                return result;
            },
            [&w, data, restore](auto result) {
                require(result.error.isEmpty() && result.mesh, result.error);
                require(!result.budgetExceeded && !result.hasMorePages,
                        "Building source did not load completely");
                require(!result.renderPolygons.empty(), "No valid building polygons");
                result.camera.distance *= .28;
                result.camera.pitch = 1.32;
                result.camera.heading = .60;
                *data = std::move(result);
                QString error;
                require(Runtime::applyVectorPresentation(
                            *data, QJsonObject{{"fill", true}, {"labels", false}, {"baseColor", "#e8d6b4"}},
                            error),
                        error);
                data->mesh->opacity = .65f;
                restore();
                w.globe->setCamera(data->camera);
            });

        w.show();
        return app.exec();
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
