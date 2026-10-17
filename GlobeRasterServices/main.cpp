#include <GeoKernel.Globe/RuntimeBasemaps.h>
#include <GeoKernel.Globe/RuntimeFactory.h>
#include <GeoKernel.Globe/RuntimeLocalDem.h>
#include <GeoKernel.Globe/RuntimeWindow.h>
#include <QApplication>
#include <QCoreApplication>
#include <QFutureWatcher>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QStatusBar>
#include <QTimer>
#include <QVulkanInstance>
#include <QtConcurrent>
#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

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

class RasterWindow : public QMainWindow
{
  public:
    Runtime::RuntimeWindow *globe;
    std::atomic_bool cancelled{false};
    bool busy = false;

    explicit RasterWindow(QVulkanInstance &instance, const QString &title)
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
        setWindowTitle(title);
        resize(1100, 760);
        statusBar()->showMessage("Loading Sagrada orthophoto and DEM...");
    }

    ~RasterWindow() override
    {
        cancelled = true;
        QThreadPool::globalInstance()->waitForDone();
        delete takeCentralWidget();
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

struct SagradaData
{
    Runtime::LocalDemResult ortho, dem;
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
        RasterWindow w(instance, "Globe — Sagrada Ortho & DEM");

        auto data = std::make_shared<SagradaData>();
        const auto directory = QCoreApplication::applicationDirPath() + "/data/";
        w.statusBar()->showMessage("Loading Sagrada orthophoto and DEM...");
        w.statusBar()->addPermanentWidget(new QLabel(
            Runtime::Basemaps::attribution(Runtime::runtimeTileSourceNames().indexOf("Esri Satellite"))));
        w.task(
            [directory] {
                SagradaData result;
                result.ortho = Runtime::openLocalRaster(directory + "sagrada_familia_ortophoto.tif");
                result.dem = Runtime::openLocalDem(directory + "sagrada_familia_terrain.tif");
                return result;
            },
            [&w, data](auto result) {
                require(result.ortho.error.isEmpty() && bool(result.ortho.reader) && result.ortho.hasFocus,
                        result.ortho.error);
                require(result.dem.error.isEmpty() && bool(result.dem.reader), result.dem.error);
                *data = std::move(result);
                w.globe->setRasterOverlay(data->ortho.reader);
                w.globe->setDemOverlay(data->dem.reader, data->dem.heightBounds);
                w.globe->setDemStyle(false, data->dem.minimum, data->dem.maximum);

                if (!w.globe->terrainEnabled())
                    w.globe->toggleTerrain();

                auto camera = data->ortho.focus;
                camera.pitch = 1.32;
                camera.heading = .6;
                camera.distance *= .6;
                w.globe->setCamera(camera);
                w.statusBar()->showMessage("Sagrada Familia orthophoto + DEM | Elevation 1x");
            });
        auto *errors = new QTimer(&w);
        QObject::connect(errors, &QTimer::timeout, &w, [&w, data] {
            for (const auto &error : {data->ortho.status->error(), data->dem.status->error(),
                                      w.globe->localImageryError(), w.globe->localElevationError()})
                if (!error.isEmpty())
                {
                    w.statusBar()->showMessage(error);
                    return;
                }
        });
        errors->start(500);

        w.show();
        return app.exec();
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
