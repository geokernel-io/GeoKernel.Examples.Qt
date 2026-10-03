#include "CameraPanel.h"
#include <QKeyEvent>
#include <QTemporaryDir>
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
using namespace GeoKernel3D;
struct LoadResult { std::unique_ptr<LoadedScene> scene; QString error; };

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    app.setApplicationName("CameraNavigation");
    app.setOrganizationName("GeoKernel");
    app.setWindowIcon(QIcon(":/icons/geokernel.ico"));
    QCommandLineParser parser;
    parser.setApplicationDescription("Camera presets, smooth transitions and saved views with GeoKernel Viewer3D.");
    parser.addHelpOption();
    parser.addOptions({{"smoke-report","Run a non-focus validation and save JSON plus PNG.","file"}});
    parser.process(app);
    const bool smoke=parser.isSet("smoke-report");
    QTemporaryDir testSettings;
    if(smoke){
        if(!testSettings.isValid())return 3;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,testSettings.path());
    }
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
        else QMessageBox::critical(nullptr,"CameraNavigation","Vulkan could not be initialized.");
        return 1;
    }
    // Lifetime: controller, container/window, then Vulkan instance.
    QMainWindow window; window.setWindowTitle("CameraNavigation — GeoKernel");window.resize(1200,800);window.setProperty("smoke",smoke);
    auto* viewport=createViewerWindow(&instance);
    auto* container=QWidget::createWindowContainer(viewport,&window);container->setFocusPolicy(Qt::StrongFocus);
    auto* stack=new QStackedWidget(&window);
    auto* welcome=new QLabel("Preparing Sagrada Família terrain and orthophoto…");
    welcome->setAlignment(Qt::AlignCenter);stack->addWidget(welcome);stack->addWidget(container);stack->setCurrentWidget(welcome);window.setCentralWidget(stack);
    ViewerController viewer(viewport);
    auto* dock=new QDockWidget("Camera navigation",&window);dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto* panel=new QWidget;auto* form=new QFormLayout(panel);
    QString demPath,photoPath;
    bool demCached=false,photoCached=false;
    auto* dataset=new QLabel("Sagrada Família — terrain and orthophoto");dataset->setWordWrap(true);form->addRow(dataset);
    auto* note=new QLabel("Sample DEM heights are an unverified preview. No vertical datum conversion is applied.");
    note->setWordWrap(true);form->addRow(note);
    auto* load=new QPushButton("Reload sample");auto* cancel=new QPushButton("Cancel loading");cancel->setEnabled(false);
    form->addRow(load);form->addRow(cancel);auto* progress=new QProgressBar;progress->setRange(0,0);progress->hide();form->addRow(progress);
    auto* navigation=new CameraPanel(viewer,true,panel);
    auto* exaggeration=new QDoubleSpinBox;
    exaggeration->setRange(0.25,10.0);
    exaggeration->setSingleStep(0.25);
    exaggeration->setDecimals(2);
    exaggeration->setValue(1.0);
    exaggeration->setSuffix(" ×");
    exaggeration->setToolTip("Changes displayed terrain height only; source elevations are unchanged.");
    form->addRow("Vertical exaggeration",exaggeration);
    QObject::connect(exaggeration,qOverload<double>(&QDoubleSpinBox::valueChanged),&window,[&](double value){
        navigation->stop();
        viewer.setHeightScale(static_cast<float>(value));
    });
    form->addRow(navigation);navigation->setEnabled(false);
    viewport->installEventFilter(navigation);
    container->installEventFilter(navigation);
    auto* info=new QLabel("No terrain loaded.");info->setWordWrap(true);form->addRow(info);
    dock->setWidget(panel);window.addDockWidget(Qt::LeftDockWidgetArea,dock);
    auto* toolbar=window.addToolBar("Navigation");toolbar->setMovable(false);
    QObject::connect(toolbar->addAction("Reset view"),&QAction::triggered,&window,[&]{navigation->goHome();});
    QObject::connect(toolbar->addAction("Controls"),&QAction::triggered,&window,[&]{QMessageBox::information(&window,"Camera controls","Left drag: pan\nWheel or right drag: zoom\nMiddle drag or Ctrl + left drag: orbit\nShift + left drag: look");});
    std::atomic_bool cancelled{false};std::future<LoadResult> pending;bool pendingPhoto=false;
    auto busy=[&](bool on){load->setEnabled(!on);cancel->setEnabled(on);progress->setVisible(on);};
    auto fail=[&](const QString& error){window.statusBar()->showMessage(error);if(smoke)report({{"success",false},{"error",error}});else QMessageBox::warning(&window,"Unable to load terrain",error);};
    QTimer poll;poll.setInterval(75);
    QObject::connect(cancel,&QPushButton::clicked,&window,[&]{cancelled=true;cancel->setEnabled(false);window.statusBar()->showMessage("Cancelling…");});
    QObject::connect(load,&QPushButton::clicked,&window,[&]{
        if(pending.valid())return;
        navigation->stop();busy(true);cancel->setEnabled(false);window.statusBar()->showMessage("Preparing sample data…");
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
        const QString folder=QFileInfo(demPath).absolutePath();const QDir dir(folder);
        SceneOptions options;options.demFile=dir.relativeFilePath(QFileInfo(demPath).absoluteFilePath());
        options.imageFile=photoPath.isEmpty()?QString():dir.relativeFilePath(QFileInfo(photoPath).absoluteFilePath());
        // Explicitly disable vector discovery: this example teaches raster layers only.
        options.roadFile.clear();options.buildingFile.clear();options.overrideHeightReference=true;
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
        const auto description=r.scene->description();QString error;
        if(!viewer.setScene(std::move(r.scene),error)){fail(error);return;}

        viewer.setHeightScale(static_cast<float>(exaggeration->value()));
        viewer.resetCamera();stack->setCurrentWidget(container);navigation->setHome(viewer.camera());navigation->setEnabled(true);
        auto appearance=viewer.layerAppearance(DisplayLayer::Imagery);appearance.visible=pendingPhoto;viewer.setLayerAppearance(DisplayLayer::Imagery,appearance);
        info->setText(QString("DEM: %1\nOrthophoto: %2\nHeights: %3").arg(QFileInfo(demPath).fileName(), pendingPhoto ? QFileInfo(photoPath).fileName() : "None", "Unknown — unverified preview"));window.statusBar()->showMessage("Terrain loaded | Left: pan | Wheel/right: zoom | Middle/Ctrl+left: orbit");
        if(smoke)QTimer::singleShot(3000,&window,[&]{
            exaggeration->setValue(5.0);
            const bool exaggerationApplied=viewer.heightScale()==5.0f;
            exaggeration->setValue(1.0);
            const bool exaggerationOk=exaggerationApplied&&viewer.heightScale()==1.0f;
            const auto home=viewer.camera();
            navigation->saveView("Smoke bookmark");
            bool persisted=false;
            {CameraPanel reopened(viewer,true);persisted=reopened.viewCount()==1;}
            navigation->goPreset(1);
            QTimer::singleShot(1600,&window,[&,home,persisted,exaggerationOk]{
                const bool preset=std::abs(viewer.camera().pitch-85.f)<.1f;
                auto* vk=qobject_cast<QVulkanWindow*>(viewport);
                const bool captured=vk&&vk->supportsGrab()&&vk->grab().save(parser.value("smoke-report")+".png");
                navigation->restoreView();
                QTimer::singleShot(1600,&window,[&,home,preset,captured,persisted,exaggerationOk]{
                    const auto restored=viewer.camera();
                    const bool bookmark=std::abs(restored.pitch-home.pitch)<.1f&&std::abs(restored.distance-home.distance)<.001f;
                    navigation->goPreset(2);
                    QTimer::singleShot(100,&window,[&,preset,captured,bookmark,persisted,exaggerationOk]{
                        QKeyEvent manualInput(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
                        QCoreApplication::sendEvent(container,&manualInput);
                        const bool interrupted=!navigation->animating();
                        navigation->deleteView();
                        const auto d=viewer.renderDiagnostics();
                        report({{"success",exaggerationOk&&preset&&captured&&bookmark&&persisted&&interrupted&&navigation->viewCount()==0&&d.renderError.isEmpty()&&d.imageryStreamError.isEmpty()},
                            {"exaggerationControl",exaggerationOk},{"bookmarkPersisted",persisted},{"presetReached",preset},{"bookmarkRestored",bookmark},{"inputStopsAnimation",interrupted},{"bookmarkDeleted",navigation->viewCount()==0},
                            {"captureSaved",captured},{"demCacheHit",demCached},{"imageryCacheHit",photoCached},{"renderError",d.renderError},{"imageryError",d.imageryStreamError}});
                    });
                });
            });
        });
    });
    if(smoke){window.setWindowFlags(Qt::Tool|Qt::WindowDoesNotAcceptFocus|Qt::WindowTransparentForInput);window.setAttribute(Qt::WA_ShowWithoutActivating);window.move(app.screens().back()->availableGeometry().topLeft()+QPoint(20,20));}
    window.show();
    QTimer::singleShot(0,load,&QPushButton::click);
    const int result=app.exec();navigation->stop();poll.stop();cancelled=true;
    // Native raster reads may finish before cancellation is observed; join before SDK teardown.
    if(pending.valid())pending.get();
    return result;
}


