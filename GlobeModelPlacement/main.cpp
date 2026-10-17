#include <GeoKernel.Globe/RuntimeBasemaps.h>
#include <GeoKernel.Globe/RuntimeFactory.h>
#include <GeoKernel.Globe/RuntimeSceneImport.h>
#include <GeoKernel.Globe/RuntimeTerrainAnalysis.h>
#include <GeoKernel.Globe/RuntimeWindow.h>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QIcon>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMainWindow>
#include <QStatusBar>
#include <QTemporaryFile>
#include <QToolBar>
#include <QVulkanInstance>
#include <QtConcurrent>
#include <algorithm>
#include <atomic>
#include <cmath>
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
class ModelWindow : public QMainWindow
{
  public:
    Runtime::RuntimeWindow *globe;
    QToolBar *bar;
    std::atomic_bool cancelled{false};
    bool busy = false;

    explicit ModelWindow(QVulkanInstance &instance, const QString &title)
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
        statusBar()->showMessage("Loading Sagrada Familia model...");
    }

    ~ModelWindow() override
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

struct ModelResult
{
    std::vector<std::shared_ptr<const Runtime::GlobeSceneMesh>> meshes;
    QString error;
};

ModelResult loadModel(const QString &path, const QJsonObject &settings, const std::atomic_bool &cancelled)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), file.errorString());

    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    require(error.error == QJsonParseError::NoError && document.isObject(), "Invalid model JSON");
    
    const auto source = document.object();
    const auto meshes = source["meshes"].toArray();
    const auto accessors = source["accessors"].toArray();
    ModelResult result;
    
    for (int meshIndex = 0; meshIndex < meshes.size(); ++meshIndex)
    {
        for (const auto value : meshes[meshIndex].toObject()["primitives"].toArray())
        {
            const auto primitive = value.toObject();
            const int accessorIndex = primitive["indices"].toInt(-1);
            require(accessorIndex >= 0 && accessorIndex < accessors.size(), "Missing model indices");
            const auto accessor = accessors[accessorIndex].toObject();
            const int count = accessor["count"].toInt();
            const int component = accessor["componentType"].toInt();
            const int width = component == 5121 ? 1 : component == 5123 ? 2 : component == 5125 ? 4 : 0;
            require(width && count > 0 && count % 3 == 0 && primitive["mode"].toInt(4) == 4,
                    "Expected indexed model triangles");
            for (int start = 0; start < count; start += 54000)
            {
                require(!cancelled, "Cancelled");
                auto part = source;
                auto nodes = source["nodes"].toArray();
                for (int i = 0; i < nodes.size(); ++i)
                {
                    auto node = nodes[i].toObject();
                    if (node["mesh"].toInt(-1) == meshIndex)
                        node["mesh"] = 0;
                    else
                        node.remove("mesh");
                    nodes[i] = node;
                }
                part["nodes"] = nodes;
                auto indices = accessor;
                indices["byteOffset"] = accessor["byteOffset"].toInteger() + qint64(start) * width;
                indices["count"] = std::min(54000, count - start);
                indices.remove("min");
                indices.remove("max");
                auto partAccessors = accessors;
                auto selected = primitive;
                selected["indices"] = partAccessors.size();
                partAccessors.append(indices);
                part["accessors"] = partAccessors;
                part["meshes"] = QJsonArray{QJsonObject{{"primitives", QJsonArray{selected}}}};

                QTemporaryFile temporary(QFileInfo(path).absolutePath() + "/model-part-XXXXXX.gltf");
                require(temporary.open(), temporary.errorString());
                const auto json = QJsonDocument(part).toJson(QJsonDocument::Compact);
                require(temporary.write(json) == json.size() && temporary.flush(), temporary.errorString());
                temporary.close();
                auto placement = settings;
                placement["path"] = temporary.fileName();
                auto layer = Runtime::importSceneLayer(placement, cancelled);
                require(layer.error.isEmpty() && bool(layer.mesh), layer.error);
                result.meshes.push_back(layer.mesh);
            }
        }
    }
    require(!result.meshes.empty(), "Model contains no meshes");
    Runtime::GlobeSceneLayer combined;
    combined.chunks = result.meshes;
    require(Runtime::sceneLayerBudget({combined}, result.error), result.error);
    return result;
}

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
        ModelWindow w(instance, "Globe — Sagrada Familia Model");

        const auto directory = QCoreApplication::applicationDirPath() + "/data/sagrada-familia/";
        auto model = std::make_shared<ModelResult>();
        auto state = std::make_shared<QJsonObject>(QJsonObject{{"kind", "model"},
                                                               {"name", "Sagrada Familia"},
                                                               {"visible", true},
                                                               {"longitude", 2.1744},
                                                               {"latitude", 41.4036},
                                                               {"height", 14.089},
                                                               {"heightMode", "ellipsoidal"},
                                                               {"heading", 0},
                                                               {"pitch", 0},
                                                               {"roll", 0},
                                                               {"scale", 1},
                                                               {"demPath", ""}});
        auto focus = [&w, state] {
            Runtime::SurfaceCamera camera;
            camera.pivot = Runtime::GlobeTerrainAnalysis::direction(
                {(*state)["longitude"].toDouble(), (*state)["latitude"].toDouble(), 0});
            camera.pivotHeightMetres = (*state)["height"].toDouble() + 65 * (*state)["scale"].toDouble();
            camera.distance = 550 * (*state)["scale"].toDouble() / 6378137.;
            camera.pitch = 1.15;
            camera.heading = .6;
            w.globe->setCamera(camera);
        };

        auto reload = [&w, state, model, directory] {
            const auto settings = *state;
            w.statusBar()->showMessage("Loading Sagrada Familia model...");
            w.task(
                [&w, settings, directory] {
                    return loadModel(directory + "model.gltf", settings, w.cancelled);
                },
                [&w, model](auto result) {
                    require(result.error.isEmpty(), result.error);
                    require(!result.meshes.empty(), "Model is empty");
                    *model = std::move(result);
                    w.globe->setSceneLayers(model->meshes);
                    w.statusBar()->showMessage(
                        "Sagrada Familia | Approximate placement | Rotate, scale or adjust height");
                });
        };

        w.action("Zoom to model", focus);

        w.action("Rotate 30 degrees", [state, reload] {
            (*state)["heading"] = std::fmod((*state)["heading"].toDouble() + 30, 360.);
            reload();
        });

        w.action("Scale...", [&w, state, reload, focus] {
            bool ok;
            const double scale = QInputDialog::getDouble(&w, "Model placement", "Scale",
                                                         (*state)["scale"].toDouble(), .01, 10, 2, &ok);
            if (ok)
            {
                (*state)["scale"] = scale;
                reload();
                focus();
            }
        });

        w.action("Height...", [&w, state, reload, focus] {
            bool ok;
            const double height = QInputDialog::getDouble(&w, "Model placement", "Anchor height (m)",
                                                          (*state)["height"].toDouble(), -100, 10000, 3, &ok);
            if (ok)
            {
                (*state)["height"] = height;
                reload();
                focus();
            }
        });

        w.action("Remove", [&w] {
            w.globe->setSceneLayers({});
        });

        w.action("Restore", [&w, model] {
            w.globe->setSceneLayers(model->meshes);
        });

        w.action("Atmosphere", [&w] {
            w.globe->toggleAtmosphere();
        });

        w.action("Esri on/off", [&w] {
            w.globe->setTileSource(
                w.globe->tileSource() < 0 ? Runtime::runtimeTileSourceNames().indexOf("Esri Satellite") : -1);
        });

        w.statusBar()->addPermanentWidget(new QLabel(
            "Model: PeeJaa | " +
            Runtime::Basemaps::attribution(Runtime::runtimeTileSourceNames().indexOf("Esri Satellite"))));

        focus();
        reload();

        w.show();

        return app.exec();
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
