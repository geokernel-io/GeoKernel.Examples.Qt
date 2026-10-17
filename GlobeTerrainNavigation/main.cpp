#include "GlobeSampleSupport.h"
#include <GeoKernel.Globe/RuntimeFactory.h>
#include <GeoKernel.Globe/RuntimeBasemaps.h>
#include <GeoKernel.Globe/RuntimeElevationDecode.h>
#include <GeoKernel.Globe/RuntimeTerrainAnalysis.h>
#include <QApplication>
#include <QIcon>
#include <QMainWindow>
#include <QToolBar>
#include <QStatusBar>
#include <QAction>
#include <QSignalBlocker>
#include <QVulkanInstance>

using namespace GlobeSample;

class TerrainWindow : public QMainWindow {
public:
    Runtime::RuntimeWindow* globe;
    Runtime::SurfaceCamera home;
    QAction *terrain, *imagery, *atmosphere, *light;
    
    int esriIndex;

    TerrainWindow(QVulkanInstance& instance) {

        auto options=offlineOptions();
        esriIndex=Runtime::runtimeTileSourceNames().indexOf("Esri Satellite");
        options.networkEnabled=true;
        options.initialTileSource=esriIndex;
        options.terrainEnabled=true;
        options.atmosphereEnabled=true;
        options.sunlight=true;
        options.terrainUrl=Runtime::esriTerrainUrl();
        options.terrainLerc=true; options.terrainMaxZoom=14;
        
        home.pivot=Runtime::GlobeTerrainAnalysis::direction({2.1744,41.4036,0});
        home.pivotHeightMetres=40;
        home.distance=2500./6378137.;home.pitch=.65;
        
        options.camera=home;

        globe = new Runtime::RuntimeWindow(options);
        globe->setVulkanInstance(&instance);

        setCentralWidget(QWidget::createWindowContainer(globe,this));
        setWindowTitle("GeoKernel — Terrain & Navigation");resize(1200,800);
        
        auto* bar=addToolBar("Terrain");bar->setMovable(false);
        
        auto toggle=[&](const QString& name,bool checked,auto fn) {
            auto* a=bar->addAction(name);
            a->setCheckable(true);
            a->setChecked(checked);
            connect(a,&QAction::toggled,this,fn);
            return a;
        };
        
        terrain=toggle("DEM",true,[this](bool on) {
            if(globe->terrainEnabled()!=on)
                globe->toggleTerrain();
        });

        imagery=toggle("Esri Satellite",esriIndex>=0,[this](bool on) {
            globe->setTileSource(on?esriIndex:-1);
        });

        imagery->setEnabled(esriIndex>=0);

        atmosphere=toggle("Atmosphere",true,[this](bool on) {
            if(globe->atmosphereEnabled()!=on)
                globe->toggleAtmosphere();
        });

        light=toggle("Sunlight",true,[this](bool on) {
            globe->setSunlight(on);
        });

        auto* nav=addToolBar("Navigation");nav->setMovable(false);
        
        const std::pair<const char*,Runtime::NavigationActions::Action> actions[] = {
            {"Zoom in",Runtime::NavigationActions::ZoomIn},
            {"Zoom out",Runtime::NavigationActions::ZoomOut},
            {"Rotate left",Runtime::NavigationActions::Left},
            {"Rotate right",Runtime::NavigationActions::Right},
            {"Tilt up",Runtime::NavigationActions::TiltUp},
            {"Tilt down",Runtime::NavigationActions::TiltDown},
            {"Top view",Runtime::NavigationActions::Top},
            {"North",Runtime::NavigationActions::North}
        };
        
        for(auto entry:actions)connect(nav->addAction(entry.first),&QAction::triggered,this,[this,entry] {
            globe->navigate(entry.second);
        });

        connect(nav->addAction("Overview"),&QAction::triggered,this,[this] {
            globe->setCamera(home);
        });
        
        auto* timer=new QTimer(this);
        
        connect(timer,&QTimer::timeout,this,[this] {

            auto sync=[](QAction* a,bool on) {
                QSignalBlocker b(a);
                a->setChecked(on);
            };

            sync(terrain,globe->terrainEnabled());
            sync(imagery,globe->tileSource()==esriIndex&&esriIndex>=0);
            sync(atmosphere,globe->atmosphereEnabled());
            sync(light,globe->sunlightEnabled());
            
            QStringList text;

            if(globe->tileSource()>=0)
                text<<Runtime::Basemaps::attribution(globe->tileSource());
            
            if(globe->terrainEnabled())
                text<<"Terrain: Esri Terrain3D / source contributors";

            if(globe->terrainEnabled() && !globe->elevationError().isEmpty())
                text<<globe->elevationError();

            if(globe->tileSource()>=0 && !globe->tileSourceError().isEmpty())
                text<<globe->tileSourceError();

            statusBar()->showMessage(text.join(" | "));
        });

        timer->start(400);
    }
};

int main(int argc,char** argv) {

    QApplication app(argc,argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico")));
    initializeDataPaths();
    
    QVulkanInstance instance;
    if(!instance.create())
        return 1;
    
    TerrainWindow window(instance);
    
    window.show();
    return app.exec();
}






