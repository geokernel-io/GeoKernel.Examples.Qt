#include <GeoKernel.Globe/RuntimeBasemaps.h>
#include <GeoKernel.Globe/RuntimeFactory.h>
#include <GeoKernel.Globe/RuntimeLocalDem.h>
#include <GeoKernel.Globe/RuntimeTerrainAnalysis.h>
#include <GeoKernel.Globe/RuntimeWindow.h>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QFutureWatcher>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QScrollArea>
#include <QStatusBar>
#include <QTimer>
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

class TerrainAnalysisWindow : public QMainWindow
{
  public:
    Runtime::RuntimeWindow *globe;
    QToolBar *bar;
    std::atomic_bool cancelled{false};
    bool busy = false;

    explicit TerrainAnalysisWindow(QVulkanInstance &instance, const QString &title)
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
        statusBar()->showMessage("Loading Barcelona terrain...");
    }

    ~TerrainAnalysisWindow() override
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
        TerrainAnalysisWindow w(instance, "Globe — Barcelona Terrain Analysis");

        using A = Runtime::GlobeTerrainAnalysis;
        const auto centre = A::direction({2.1744, 41.4036, 0});
        auto analysis = std::make_shared<A>();
        auto dem = std::make_shared<Runtime::LocalDemResult>();
        auto elevation = [&w](auto pos, double &height) {
            auto h = w.globe->displayedGroundHeightAt(pos);
            if (!h)
                return false;
            height = *h;
            return true;
        };
        auto points = std::make_shared<std::vector<Runtime::Vec3>>();
        auto mode = std::make_shared<int>(-1); // 0 LOS, 1 viewshed, 2 slope, 3 distance
        auto *instructions =
            new QLabel("Choose Line of sight, then click an observer and a target on the map.");
        instructions->setWordWrap(true);
        instructions->setTextInteractionFlags(Qt::TextSelectableByMouse);
        instructions->setMargin(10);
        instructions->setMinimumHeight(125);
        instructions->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto *info = new QDockWidget("Analysis result", &w);
        info->setFeatures(QDockWidget::NoDockWidgetFeatures);
        auto *resultScroll = new QScrollArea;
        resultScroll->setWidgetResizable(true);
        resultScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        resultScroll->setWidget(instructions);
        info->setWidget(resultScroll);
        info->setFixedHeight(170);
        w.addDockWidget(Qt::BottomDockWidgetArea, info);
        auto *group = new QActionGroup(&w);
        group->setExclusive(true);
        auto select = [&w, analysis, points, mode, instructions](int selected) {
            *analysis = A{};
            points->clear();
            *mode = selected;
            w.globe->setMeasurement({});
            w.globe->measurementEnabled = true;
            w.globe->setCursor(Qt::CrossCursor);
            instructions->setText(selected == 0
                                      ? "Line of sight: click the observer, then the target. Heights: 1.7 m "
                                        "above ground. Terrain only; buildings excluded."
                                  : selected == 3 ? "Measure: click a start point, then an end point."
                                  : selected == 2 ? "Slope / aspect: click a point on the terrain. Slope "
                                                    "(degrees and %) and downhill direction will appear here."
                                                  : "Viewshed: click the observer location on the terrain.");
            w.statusBar()->showMessage(instructions->text());
        };
        const QStringList names{"Line of sight", "Viewshed", "Slope / aspect", "Measure"};
        for (int i = 0; i < names.size(); ++i)
        {
            auto *action = w.bar->addAction(names[i]);
            action->setCheckable(true);
            group->addAction(action);
            QObject::connect(action, &QAction::triggered, &w, [select, i] {
                select(i);
            });
        }

        auto clear = [&w, analysis, points, mode, instructions, group] {
            *analysis = A{};
            points->clear();
            *mode = -1;
            w.globe->measurementEnabled = false;
            w.globe->unsetCursor();
            w.globe->setMeasurement({});
            group->setExclusive(false);
            for (auto *action : group->actions())
                action->setChecked(false);
            group->setExclusive(true);
            instructions->setText("Selection cleared. Choose an analysis tool to select new points.");
        };
        w.action("Clear", clear);
        w.globe->measurementFinished = clear;
        w.globe->groundClicked = [&w, analysis, points, mode, instructions, elevation](Runtime::Vec3 point) {
            if (*mode < 0)
                return;
            if (w.busy)
            {
                instructions->setText("Terrain is still loading. Please click again when loading finishes.");
                return;
            }
            const size_t required = (*mode == 0 || *mode == 3) ? 2 : 1;
            if (points->size() == required)
            {
                points->clear();
                *analysis = A{};
                w.globe->setMeasurement({});
            }
            point = point.normalized();
            double height = 0;
            if (!elevation(point, height))
            {
                instructions->setText(
                    "DEM not loaded here. Wait for terrain or return to Barcelona and try again.");
                return;
            }
            if (!points->empty() && Runtime::GlobeMeasurement::distance(points->front(), point) < 1)
            {
                instructions->setText("Choose a different target, at least 1 metre away.");
                return;
            }
            if (*mode == 0 && !points->empty() &&
                Runtime::GlobeMeasurement::distance(points->front(), point) > 10000)
            {
                instructions->setText("Choose a target within 10 km of the observer.");
                return;
            }
            points->push_back(point);
            w.globe->setMeasurement(Runtime::GlobeMeasurement::overlay(*points, false));
            const auto geo = A::geodetic(point);
            if (points->size() < required)
            {
                instructions->setText(QString("Observer/start: %1, %2 | Ground: %3 m. Now click the "
                                              "target/end point. Drag to pan; Esc cancels.")
                                          .arg(geo.latitude, 0, 'f', 6)
                                          .arg(geo.longitude, 0, 'f', 6)
                                          .arg(height, 0, 'f', 1));
                return;
            }
            if (*mode == 3)
            {
                instructions->setText(
                    QString("Ground distance: %1 m. Click to start a new measurement.")
                        .arg(Runtime::GlobeMeasurement::distance(points->front(), points->back()), 0, 'f',
                             2));
                return;
            }
            A::Settings settings;
            settings.radius = 350;
            settings.spacing = 15;
            const auto selected = *mode == 0   ? A::Mode::LineOfSight
                                  : *mode == 1 ? A::Mode::Viewshed
                                               : A::Mode::Slope;
            analysis->start(selected, *points, settings, elevation);
            w.globe->setMeasurement(analysis->overlay());
            if (*mode == 2)
            {
                QString result = QString("Location: %1, %2 | Ground: %3 m\n")
                                     .arg(geo.latitude, 0, 'f', 6)
                                     .arg(geo.longitude, 0, 'f', 6)
                                     .arg(height, 0, 'f', 1);
                if (analysis->slope)
                {
                    const auto &slope = *analysis->slope;
                    result += QString("Slope: %1 degrees (%2 %)\nAspect: %3\n")
                                  .arg(slope.degrees, 0, 'f', 2)
                                  .arg(slope.percent, 0, 'f', 2)
                                  .arg(slope.aspect ? QString::number(*slope.aspect, 'f', 1) +
                                                          " degrees clockwise from north (downhill)"
                                                    : "Undefined: flat terrain");
                    w.statusBar()->showMessage(QString("Slope: %1 degrees | %2 %")
                                                   .arg(slope.degrees, 0, 'f', 2)
                                                   .arg(slope.percent, 0, 'f', 2));
                }
                else
                {
                    result += "Slope unavailable: neighbouring DEM samples are missing. Wait for terrain or "
                              "choose a point near Sagrada Familia.\n";
                    if (!analysis->error.isEmpty())
                        result += analysis->error + "\n";
                    w.statusBar()->showMessage("Slope unavailable at the selected point");
                }
                instructions->setText(
                    result + "Click another point to recalculate. Sample spacing: 15 m. Terrain only.");
            }
            else
                instructions->setText(analysis->description() +
                                      "\nClick to start a new selection. Esc cancels.");
        };
        Runtime::SurfaceCamera home;
        home.pivot = centre;
        home.pivotHeightMetres = 40;
        home.distance = 2500. / 6378137.;
        home.pitch = .85;
        home.heading = .6;
        w.globe->setCamera(home);
        w.action("Barcelona", [&w, home] {
            w.globe->setCamera(home);
        });
        w.statusBar()->addPermanentWidget(new QLabel(
            Runtime::Basemaps::attribution(Runtime::runtimeTileSourceNames().indexOf("Esri Satellite"))));
        auto *tick = new QTimer(&w);
        QObject::connect(tick, &QTimer::timeout, &w, [&w, analysis, dem, instructions] {
            if (analysis->pending())
            {
                analysis->advance(128);
                instructions->setText(analysis->description() +
                                      "\nClick to start a new selection. Esc cancels.");
                w.globe->setMeasurement(analysis->overlay());
            }
            const auto error = dem->status->error();
            if (!error.isEmpty())
                w.statusBar()->showMessage(error);
        });
        tick->start(30);
        const auto path = QCoreApplication::applicationDirPath() + "/data/sagrada_familia_terrain.tif";
        w.task(
            [path] {
                return Runtime::openLocalDem(path);
            },
            [&w, dem](auto result) {
                require(result.error.isEmpty() && bool(result.reader), result.error);
                *dem = std::move(result);
                w.globe->setDemOverlay(dem->reader, dem->heightBounds);
                w.globe->setDemStyle(false, dem->minimum, dem->maximum);
                if (!w.globe->terrainEnabled())
                    w.globe->toggleTerrain();
                w.statusBar()->showMessage("Barcelona | Real DEM | Choose an analysis (terrain only)");
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
