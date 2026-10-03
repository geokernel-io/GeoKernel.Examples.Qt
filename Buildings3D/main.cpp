#include "SampleSupport.h"
#include <GeoKernel.Viewer3D/ViewerController.h>
#include <QApplication>
#include <QCommandLineParser>
#include <QMainWindow>

#include <QFileInfo>
#include <QDir>
#include <QDockWidget>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>

#include <QDoubleSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QColorDialog>
#include <QSlider>
#include <QProgressBar>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QMessageBox>
#include <QTimer>
#include <QVulkanInstance>
#include <QVulkanWindow>
#include <QScreen>
#include <QJsonDocument>
#include <QFile>
#include <future>
#include <atomic>
#include <cmath>
using namespace GeoKernel3D;
struct LoadResult { std::unique_ptr<LoadedScene> scene; QString error; };

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    app.setApplicationName("Buildings3D");
    app.setWindowIcon(QIcon(":/icons/geokernel.ico"));
    QCommandLineParser parser;
    parser.setApplicationDescription("Extrude and style building footprints on terrain with GeoKernel Viewer3D.");
    parser.addHelpOption();
    parser.addOptions({{"smoke-report","Run a non-focus validation and save JSON plus PNG.","file"},{"smoke-height","Building height for automated validation.","metres","9"}});
    parser.process(app);
    const bool smoke=parser.isSet("smoke-report");
    auto report=[&](QJsonObject result) {
        if(!smoke)return;
        QFile f(parser.value("smoke-report"));
        if(!f.open(QIODevice::WriteOnly)){app.exit(3);return;}
        f.write(QJsonDocument(result).toJson());
        app.exit(result["success"].toBool()?0:2);
    };
    QVulkanInstance instance; instance.setApiVersion(QVersionNumber(1,1,0));
    if(!instance.create()) {
        if(smoke){QFile f(parser.value("smoke-report"));if(f.open(QIODevice::WriteOnly))f.write("{\"success\":false,\"error\":\"Vulkan initialization failed\"}");}
        else QMessageBox::critical(nullptr,"Buildings3D","Vulkan could not be initialized.");
        return 1;
    }
    // Lifetime: controller, container/window, then Vulkan instance.
    QMainWindow window; window.setWindowTitle("Buildings3D — GeoKernel");window.resize(1200,800);window.setProperty("smoke",smoke);
    auto* viewport=createViewerWindow(&instance);
    auto* container=QWidget::createWindowContainer(viewport,&window);container->setFocusPolicy(Qt::StrongFocus);
    auto* stack=new QStackedWidget(&window);
    auto* welcome=new QLabel("Preparing Sagrada Família buildings, terrain and orthophoto…");
    welcome->setAlignment(Qt::AlignCenter);stack->addWidget(welcome);stack->addWidget(container);stack->setCurrentWidget(welcome);window.setCentralWidget(stack);
    ViewerController viewer(viewport);
    auto* dock=new QDockWidget("Buildings on terrain",&window);dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto* panel=new QWidget;auto* form=new QFormLayout(panel);
    QString demPath,photoPath,buildingPath;
    bool buildingsCached=false;
    double pendingHeight=9;int heightFeatureCount=0;bool assignedHeightsMatch=false;
    bool demCached=false,photoCached=false;
    auto* dataset=new QLabel("Sagrada Família — buildings on terrain");dataset->setWordWrap(true);form->addRow(dataset);
    auto* note=new QLabel("Sample DEM heights are an unverified preview. No vertical datum conversion is applied.");
    note->setWordWrap(true);form->addRow(note);
    auto* buildingHeight=new QDoubleSpinBox;buildingHeight->setRange(1,300);buildingHeight->setValue(9);buildingHeight->setSuffix(" m");
    if(smoke){bool ok=false;double h=parser.value("smoke-height").toDouble(&ok);if(!ok||h<1||h>300)return 2;buildingHeight->setValue(h);}
    form->addRow("Default building height",buildingHeight);
    auto* heightNote=new QLabel("Illustrative height: this dataset has no measured building heights. Click Apply height / Reload sample after changing it.");
    heightNote->setWordWrap(true);form->addRow(heightNote);
    auto* load=new QPushButton("Apply height / Reload sample");auto* cancel=new QPushButton("Cancel loading");cancel->setEnabled(false);
    form->addRow(load);form->addRow(cancel);auto* progress=new QProgressBar;progress->setRange(0,0);progress->hide();form->addRow(progress);
    auto* imagery=new QCheckBox("Show orthophoto");imagery->setChecked(true);imagery->setEnabled(false);form->addRow(imagery);
    auto* buildingsVisible=new QCheckBox("Show buildings");buildingsVisible->setChecked(true);
    form->addRow(buildingsVisible);
    auto* buildingColor=new QPushButton("Building color");form->addRow(buildingColor);
    QColor selectedBuildingColor("#d9c4a5");
    auto* opacity=new QSlider(Qt::Horizontal);opacity->setRange(0,100);opacity->setValue(100);
    auto* opacityText=new QLabel("100%");form->addRow("Building opacity",opacity);form->addRow(opacityText);
    auto applyBuildingStyle=[&]{
        auto appearance=viewer.layerAppearance(DisplayLayer::Buildings);
        appearance.visible=buildingsVisible->isChecked();appearance.opacity=opacity->value()/100.f;
        viewer.setLayerAppearance(DisplayLayer::Buildings,appearance);
        viewer.setLayerColor(DisplayLayer::Buildings,selectedBuildingColor);
        buildingColor->setText("Building color: "+selectedBuildingColor.name());
        opacityText->setText(QString::number(opacity->value())+"%");
    };
    QObject::connect(buildingsVisible,&QCheckBox::toggled,&window,[&]{applyBuildingStyle();});
    QObject::connect(opacity,&QSlider::valueChanged,&window,[&]{applyBuildingStyle();});
    QObject::connect(buildingColor,&QPushButton::clicked,&window,[&]{
        const QColor color=QColorDialog::getColor(selectedBuildingColor,&window,"Building color");
        if(color.isValid()){selectedBuildingColor=color;applyBuildingStyle();}
    });
    applyBuildingStyle();
    auto* exaggeration=new QDoubleSpinBox;
    exaggeration->setRange(0.25,10.0);
    exaggeration->setSingleStep(0.25);
    exaggeration->setDecimals(2);
    exaggeration->setValue(1.0);
    exaggeration->setSuffix(" ×");
    exaggeration->setToolTip("Scales displayed scene height. Source elevations are unchanged.");
    form->addRow("Vertical exaggeration",exaggeration);
    QObject::connect(exaggeration,qOverload<double>(&QDoubleSpinBox::valueChanged),&window,
        [&](double value){viewer.setHeightScale(static_cast<float>(value));});
    auto* info=new QLabel("No terrain loaded.");info->setWordWrap(true);form->addRow(info);
    dock->setWidget(panel);window.addDockWidget(Qt::LeftDockWidgetArea,dock);
    auto* toolbar=window.addToolBar("Navigation");toolbar->setMovable(false);
    QObject::connect(toolbar->addAction("Reset view"),&QAction::triggered,&window,[&]{viewer.resetCamera();});
    QObject::connect(toolbar->addAction("Controls"),&QAction::triggered,&window,[&]{QMessageBox::information(&window,"Camera controls","Left drag: pan\nWheel or right drag: zoom\nMiddle drag or Ctrl + left drag: orbit\nShift + left drag: look");});
    QObject::connect(imagery,&QCheckBox::toggled,&window,[&](bool visible){auto a=viewer.layerAppearance(DisplayLayer::Imagery);a.visible=visible;viewer.setLayerAppearance(DisplayLayer::Imagery,a);});
    std::atomic_bool cancelled{false};std::future<LoadResult> pending;bool pendingPhoto=false;
    auto busy=[&](bool on){load->setEnabled(!on);buildingHeight->setEnabled(!on);cancel->setEnabled(on);progress->setVisible(on);};
    auto fail=[&](const QString& error){window.statusBar()->showMessage(error);if(smoke)report({{"success",false},{"error",error}});else QMessageBox::warning(&window,"Unable to load terrain",error);};
    QTimer poll;poll.setInterval(75);
    QObject::connect(cancel,&QPushButton::clicked,&window,[&]{cancelled=true;cancel->setEnabled(false);window.statusBar()->showMessage("Cancelling…");});
    QObject::connect(load,&QPushButton::clicked,&window,[&]{
        if(pending.valid())return;
        busy(true);cancel->setEnabled(false);window.statusBar()->showMessage("Preparing sample data…");
        auto unavailable=[&]{busy(false);window.statusBar()->showMessage("Sample unavailable. Click Reload sample to retry.");
            if(smoke)report({{"success",false},{"error",window.property("sampleError").toString()}});};
        demPath=ensureSampleFile(
            QUrl("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_terrain.zip"),
            "sagrada_familia_terrain.zip","sagrada_familia_terrain","sagrada_familia_terrain.tif",&window);
        if(demPath.isEmpty()){unavailable();return;}
        demCached=window.property("sampleCacheHit").toBool();
        photoPath=ensureSampleFile(
            QUrl("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_ortophoto.zip"),
            "sagrada_familia_ortophoto.zip","sagrada_familia_ortophoto","sagrada_familia_ortophoto.tif",&window);
        if(photoPath.isEmpty()){unavailable();return;}
        photoCached=window.property("sampleCacheHit").toBool();
        buildingPath=ensureSampleFile(
            QUrl("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_buildings.zip"),
            "sagrada_familia_buildings.zip","sagrada_familia_buildings","buildings.shp",&window);
        if(buildingPath.isEmpty()){unavailable();return;}
        buildingsCached=window.property("sampleCacheHit").toBool();
        const QString folder=QFileInfo(demPath).absolutePath();const QDir dir(folder);
        SceneOptions options;options.demFile=dir.relativeFilePath(QFileInfo(demPath).absoluteFilePath());
        options.imageFile=photoPath.isEmpty()?QString():dir.relativeFilePath(QFileInfo(photoPath).absoluteFilePath());
        // Extrude footprints from the terrain. This sample has no source height attributes.
        options.roadFile.clear();
        options.buildingFile=dir.relativeFilePath(buildingPath);
        options.buildingPlacement.mode=HeightMode::ClampToTerrain;options.buildingPlacement.useSourceZ=false;
        pendingHeight=buildingHeight->value();options.buildingHeight=pendingHeight;
        options.overrideHeightReference=true;
        options.height.datum=HeightDatum::Unknown;
        options.height.unit="m";options.height.unitAssumed=true;
        pendingPhoto=!options.imageFile.isEmpty();cancelled=false;busy(true);window.statusBar()->showMessage("Loading terrain…");
        // Only CPU preparation happens on this worker. GUI/GPU commit stays on the GUI thread.
        pending=std::async(std::launch::async,[&,folder,options]{LoadResult r;try{LoadControl c;c.cancellationFlag=&cancelled;c.cancelled=[&]{return cancelled.load();};r.scene=loadPreparedScene(folder,options,r.error,c);}catch(const std::exception& e){r.error=QString::fromUtf8(e.what());}return r;});poll.start();
    });
    QObject::connect(&poll,&QTimer::timeout,&window,[&]{
        if(!pending.valid()||pending.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
        poll.stop();auto r=pending.get();busy(false);
        if(cancelled){window.statusBar()->showMessage("Loading cancelled. Previous scene retained.");return;}
        if(!r.scene){fail(r.error);return;}
        heightFeatureCount=0;assignedHeightsMatch=true;
        if(auto scene=r.scene->scene())for(const auto& layer:scene->layers)if(layer.type==LayerType::Buildings)
            for(const auto& feature:layer.features)if(feature.buildingHeight){
                ++heightFeatureCount;
                assignedHeightsMatch=assignedHeightsMatch&&std::abs(feature.buildingHeight->metres-pendingHeight)<.001;
            }
        QString error;
        if(!viewer.setScene(std::move(r.scene),error)){fail(error);return;}
        applyBuildingStyle();
        viewer.setHeightScale(static_cast<float>(exaggeration->value()));
        viewer.resetCamera();stack->setCurrentWidget(container);imagery->setEnabled(pendingPhoto);imagery->setChecked(pendingPhoto);
        auto appearance=viewer.layerAppearance(DisplayLayer::Imagery);appearance.visible=pendingPhoto;viewer.setLayerAppearance(DisplayLayer::Imagery,appearance);
        info->setText(QString("Building features: %1\nIllustrative height: %2 m\nDEM datum: unknown — preview").arg(heightFeatureCount).arg(pendingHeight));window.statusBar()->showMessage("Terrain loaded | Left: pan | Wheel/right: zoom | Middle/Ctrl+left: orbit");
        if(smoke)QTimer::singleShot(4000,&window,[&]{
            auto* vk=qobject_cast<QVulkanWindow*>(viewport);
            const QImage visible=vk&&vk->supportsGrab()?vk->grab():QImage();
            const bool saved=!visible.isNull()&&visible.save(parser.value("smoke-report")+".png");
            buildingsVisible->setChecked(false);
            QTimer::singleShot(1000,&window,[&,visible,saved]{
                auto* vk=qobject_cast<QVulkanWindow*>(viewport);
                const QImage hidden=vk&&vk->supportsGrab()?vk->grab():QImage();
                const bool differs=saved&&!hidden.isNull()&&visible!=hidden;
                const bool hides=!viewer.layerAppearance(DisplayLayer::Buildings).visible;
                buildingsVisible->setChecked(true);opacity->setValue(40);
                selectedBuildingColor=QColor("#00ffff");applyBuildingStyle();
                const auto styled=viewer.layerAppearance(DisplayLayer::Buildings);
                const bool style=styled.visible&&std::abs(styled.opacity-.4f)<.001f&&styled.color==selectedBuildingColor;
                exaggeration->setValue(5);const bool scaled=viewer.heightScale()==5;
                exaggeration->setValue(1);
                opacity->setValue(100);selectedBuildingColor=QColor("#d9c4a5");applyBuildingStyle();
                const auto d=viewer.renderDiagnostics();const auto counts=viewer.sceneGeometryCounts();
                report({{"success",differs&&hides&&style&&scaled&&counts.buildingVertices>0&&counts.roadVertices==0&&heightFeatureCount>0&&assignedHeightsMatch&&d.renderError.isEmpty()&&d.imageryStreamError.isEmpty()},
                    {"heightMetres",pendingHeight},{"heightFeatures",heightFeatureCount},{"assignedHeightsMatch",assignedHeightsMatch},{"buildingVertices",double(counts.buildingVertices)},{"buildingsHideChangesFrame",differs},{"buildingStyleApplied",style},{"exaggerationApplied",scaled},
                    {"demCacheHit",demCached},{"imageryCacheHit",photoCached},{"buildingsCacheHit",buildingsCached},
                    {"renderError",d.renderError},{"imageryError",d.imageryStreamError},{"captureSaved",saved}});
            });
        });
    });
    if(smoke){window.setWindowFlags(Qt::Tool|Qt::WindowDoesNotAcceptFocus|Qt::WindowTransparentForInput);window.setAttribute(Qt::WA_ShowWithoutActivating);window.move(app.screens().back()->availableGeometry().topLeft()+QPoint(20,20));}
    window.show();
    QTimer::singleShot(0,load,&QPushButton::click);
    const int result=app.exec();poll.stop();cancelled=true;
    // Native raster reads may finish before cancellation is observed; join before SDK teardown.
    if(pending.valid())pending.get();
    return result;
}


