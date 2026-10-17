#include "GlobeSampleSupport.h"
#include <GeoKernel.Globe/RuntimeFactory.h>
#include <GeoKernel.Globe/RuntimeBasemaps.h>
#include <GeoKernel.Globe/RuntimeElevationDecode.h>
#include <QApplication>
#include <QIcon>
#include <QMainWindow>
#include <QToolBar>
#include <QStatusBar>
#include <QAction>
#include <QSignalBlocker>
#include <QVulkanInstance>
#include <QTimer>
#include <iostream>

using namespace GlobeSample;

int main(int argc, char** argv)
{    
    QApplication app(argc, argv);
    app.setApplicationName("GlobeHello");
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico")));
 
    initializeDataPaths();

    QVulkanInstance instance;
    if (!instance.create()) {
        std::cerr << "Vulkan initialization failed: " << instance.errorCode() << std::endl;
        return 1;
    }

    auto options = offlineOptions();
    options.networkEnabled = true;
    options.terrainUrl = Runtime::esriTerrainUrl();
    options.terrainLerc = true;
    options.terrainMaxZoom = 14;

    QMainWindow host;
    host.setWindowTitle("GeoKernel — Globe Hello");

    auto* globe = new Runtime::RuntimeWindow(options);
    globe->setVulkanInstance(&instance);
    
    host.setCentralWidget(QWidget::createWindowContainer(globe, &host));
    host.resize(1100, 760);
    
    auto* toolbar = host.addToolBar("Globe controls");
    toolbar->setMovable(false);
    
    auto toggle = [&](const char* text, const char* tip, auto callback) {
        auto* action = toolbar->addAction(text);
        action->setCheckable(true);
        action->setToolTip(tip);
        QObject::connect(action, &QAction::toggled, &host, callback);
        return action;
    };
    
    auto* atmosphere = toggle("Atmosphere", "Show or hide the atmosphere", [=](bool on) {
        if (globe->atmosphereEnabled() != on) globe->toggleAtmosphere();
    });

    const int esriIndex = Runtime::runtimeTileSourceNames().indexOf("Esri Satellite");
    auto* esri = toggle("Esri Satellite", "Online Esri World Imagery; internet required", [=](bool on) {
        globe->setTileSource(on ? esriIndex : -1, false);
    });
    esri->setEnabled(esriIndex >= 0);
    
    auto* dem = toggle("DEM", "Online Esri Terrain3D elevation, 1x; internet required", [=](bool on) {
        if (globe->terrainEnabled() != on) globe->toggleTerrain();
    });
    
    auto* light = toggle("Sunlight", "Enable or disable directional sunlight", [=](bool on) {
        globe->setSunlight(on);
    });
    
    auto* status = new QTimer(&host);

    QObject::connect(status, &QTimer::timeout, &host, [=, &host] {

        auto sync = [](QAction* a, bool checked) { 
            QSignalBlocker block(a);
            a->setChecked(checked);
        };
        
        sync(atmosphere, globe->atmosphereEnabled());
        sync(esri, globe->tileSource() == esriIndex && esriIndex >= 0);
        sync(dem, globe->terrainEnabled());
        sync(light, globe->sunlightEnabled());
        
        QStringList messages;
        
        if (globe->tileSource() >= 0)
            messages << Runtime::Basemaps::attribution(globe->tileSource());
        
        if (globe->terrainEnabled())
            messages << "Terrain: Esri / source contributors";
        
        if (globe->tileSource() >= 0 && !globe->tileSourceError().isEmpty())
            messages << globe->tileSourceError();
        
        if (globe->terrainEnabled() && !globe->elevationError().isEmpty())
            messages << globe->elevationError();
        
        if (messages.isEmpty())
            messages << "Left: pan | Right / wheel: zoom | Middle / Ctrl: orbit";
        
        host.statusBar()->showMessage(messages.join(" | "));
    });

    status->start(250);
    host.statusBar()->showMessage("Select Atmosphere, Esri Satellite, DEM or Sunlight. All start off.");

    host.show();
    return app.exec();
}


