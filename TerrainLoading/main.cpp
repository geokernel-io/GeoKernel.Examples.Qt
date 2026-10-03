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

#include <QComboBox>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QSet>
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
    app.setApplicationName("TerrainLoading");
    app.setWindowIcon(QIcon(":/icons/geokernel.ico"));
    QCommandLineParser parser;
    parser.setApplicationDescription("Load and style a georeferenced DEM with GeoKernel Viewer3D.");
    parser.addHelpOption();
    parser.addOptions({{"smoke-report","Run a non-focus validation and save JSON plus PNG.","file"}});
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
        else QMessageBox::critical(nullptr,"TerrainLoading","Vulkan could not be initialized.");
        return 1;
    }
    // Lifetime: controller, container/window, then Vulkan instance.
    QMainWindow window; window.setWindowTitle("TerrainLoading — GeoKernel");window.resize(1200,800);window.setProperty("smoke",smoke);
    auto* viewport=createViewerWindow(&instance);
    auto* container=QWidget::createWindowContainer(viewport,&window);container->setFocusPolicy(Qt::StrongFocus);
    auto* stack=new QStackedWidget(&window);
    auto* welcome=new QLabel("Preparing Sagrada Família terrain…");
    welcome->setAlignment(Qt::AlignCenter);stack->addWidget(welcome);stack->addWidget(container);stack->setCurrentWidget(welcome);window.setCentralWidget(stack);
    ViewerController viewer(viewport);
    auto* dock=new QDockWidget("Terrain data",&window);dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto* panel=new QWidget;auto* form=new QFormLayout(panel);
    QString demPath;
    auto* dataset=new QLabel("Sagrada Família — sample terrain");dataset->setWordWrap(true);form->addRow(dataset);
    auto* note=new QLabel("Sample DEM heights are displayed as an unverified preview. No vertical datum conversion is applied.");
    note->setWordWrap(true);form->addRow(note);
    auto* load=new QPushButton("Reload sample");auto* cancel=new QPushButton("Cancel loading");cancel->setEnabled(false);
    form->addRow(load);form->addRow(cancel);auto* progress=new QProgressBar;progress->setRange(0,0);progress->hide();form->addRow(progress);
    auto* quality=new QComboBox;quality->addItems({"256 — fast","512 — balanced","1024 — detailed"});quality->setCurrentIndex(1);
    form->addRow("Mesh samples per side",quality);
    auto* ramp=new QComboBox;ramp->addItems({"Terrain","Grayscale","Viridis","Inferno","Ocean"});
    form->addRow("Color scale",ramp);
    auto* legend=new QLabel;legend->setFixedHeight(24);form->addRow(legend);
    auto* legendCaption=new QLabel("Low ← relative terrain height → High\nScene-local ENU relief; not absolute elevation. Colors include terrain shading.");
    legendCaption->setWordWrap(true);form->addRow(legendCaption);
    auto* exaggeration=new QDoubleSpinBox;exaggeration->setRange(.25,10);exaggeration->setSingleStep(.25);exaggeration->setValue(1);exaggeration->setSuffix(" ×");
    form->addRow("Vertical exaggeration",exaggeration);
    QObject::connect(exaggeration,qOverload<double>(&QDoubleSpinBox::valueChanged),&window,[&](double value){viewer.setHeightScale(float(value));});
    auto applyRamp=[&]{
        const QStringList gradients={
            "#185b3f,#68984c,#cfbe7e,#926b4c,#fafafa",
            "#141414,#4e4e4e,#888888,#c2c2c2,#fcfcfc",
            "#440154,#3b528b,#21918c,#5ec962,#fde725",
            "#000004,#57106e,#bc3754,#f98e09,#fcffa4",
            "#081d58,#253494,#225ea8,#41b6c4,#edf8d9"};
        const auto colors=gradients[ramp->currentIndex()].split(',');
        legend->setStyleSheet(QString("background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 %1,stop:0.25 %2,stop:0.5 %3,stop:0.75 %4,stop:1 %5);border:1px solid #666;").arg(colors[0],colors[1],colors[2],colors[3],colors[4]));
        viewer.setTerrainColorRamp(static_cast<TerrainColorRamp>(ramp->currentIndex()+1));
    };
    QObject::connect(ramp,qOverload<int>(&QComboBox::currentIndexChanged),&window,[&]{applyRamp();});applyRamp();
    auto* info=new QLabel("No terrain loaded.");info->setWordWrap(true);form->addRow(info);
    dock->setWidget(panel);window.addDockWidget(Qt::LeftDockWidgetArea,dock);
    auto* toolbar=window.addToolBar("Navigation");toolbar->setMovable(false);
    QObject::connect(toolbar->addAction("Reset view"),&QAction::triggered,&window,[&]{viewer.resetCamera();});
    QObject::connect(toolbar->addAction("Controls"),&QAction::triggered,&window,[&]{QMessageBox::information(&window,"Camera controls","Left drag: pan\nWheel or right drag: zoom\nMiddle drag or Ctrl + left drag: orbit\nShift + left drag: look");});
    std::atomic_bool cancelled{false};std::future<LoadResult> pending;
    auto busy=[&](bool on){load->setEnabled(!on);quality->setEnabled(!on);cancel->setEnabled(on);progress->setVisible(on);};
    auto fail=[&](const QString& error){window.statusBar()->showMessage(error);if(smoke)report({{"success",false},{"error",error}});else QMessageBox::warning(&window,"Unable to load terrain",error);};
    QTimer poll;poll.setInterval(75);
    QObject::connect(cancel,&QPushButton::clicked,&window,[&]{cancelled=true;cancel->setEnabled(false);window.statusBar()->showMessage("Cancelling…");});
    QObject::connect(load,&QPushButton::clicked,&window,[&]{
        if(pending.valid())return;
        busy(true);window.statusBar()->showMessage("Preparing sample data…");
        demPath=ensureSampleFile(
            QUrl("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_terrain.zip"),
            "sagrada_familia_terrain.zip","sagrada_familia_terrain","sagrada_familia_terrain.tif",&window);
        if(demPath.isEmpty()){busy(false);window.statusBar()->showMessage("Sample unavailable. Click Reload sample to retry.");
            if(smoke)report({{"success",false},{"error",window.property("sampleError").toString()}});return;}

        const QString folder=QFileInfo(demPath).absolutePath();const QDir dir(folder);
        SceneOptions options;options.demFile=dir.relativeFilePath(QFileInfo(demPath).absoluteFilePath());
        options.imageFile.clear();options.terrainResolution=256 << quality->currentIndex();
        // Explicitly disable vector discovery: this example teaches raster layers only.
        options.roadFile.clear();options.buildingFile.clear();options.overrideHeightReference=true;
        options.height.datum=HeightDatum::Unknown;
        options.height.unit="m";options.height.unitAssumed=true;
        cancelled=false;busy(true);window.statusBar()->showMessage("Loading terrain…");
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
        viewer.resetCamera();stack->setCurrentWidget(container);applyRamp();viewer.setHeightScale(float(exaggeration->value()));
        info->setText(QString("DEM: %1\nHeights: %2\nMesh: up to %3 samples per side").arg(QFileInfo(demPath).fileName(), "Unknown — unverified preview").arg(256 << quality->currentIndex()));window.statusBar()->showMessage("Terrain loaded | Left: pan | Wheel/right: zoom | Middle/Ctrl+left: orbit");
        if(smoke) {
            auto* checks=new QTimer(&window);checks->setInterval(1500);
            auto hashes=std::make_shared<QSet<QByteArray>>();auto captures=std::make_shared<QJsonArray>();
            auto index=std::make_shared<int>(0);
            QObject::connect(checks,&QTimer::timeout,&window,[&,checks,hashes,captures,index,description]{
                auto* vk=qobject_cast<QVulkanWindow*>(viewport);QImage shot=vk&&vk->supportsGrab()?vk->grab():QImage();
                const bool saved=!shot.isNull()&&shot.save(parser.value("smoke-report")+QString(".%1.png").arg(*index));
                if(saved)hashes->insert(QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(shot.constBits()),shot.sizeInBytes()),QCryptographicHash::Sha256));
                captures->append(QJsonObject{{"ramp",ramp->currentText()},{"saved",saved}});
                if(++*index<5){ramp->setCurrentIndex(*index);return;}
                checks->stop();auto d=viewer.renderDiagnostics();
                bool rejected=false;try{viewer.setTerrainColorRamp(static_cast<TerrainColorRamp>(99));}catch(const std::invalid_argument&){rejected=viewer.terrainColorRamp()==TerrainColorRamp::Ocean;}
                report({{"success",hashes->size()==5&&d.renderError.isEmpty()&&rejected},{"description",description},{"sampleCacheHit",window.property("sampleCacheHit").toBool()},{"demPath",demPath},{"renderError",d.renderError},{"distinctFrames",hashes->size()},{"invalidRampRejected",rejected},{"captures",*captures}});
            });
            checks->start();
        }
    });
    if(smoke){window.setWindowFlags(Qt::Tool|Qt::WindowDoesNotAcceptFocus|Qt::WindowTransparentForInput);window.setAttribute(Qt::WA_ShowWithoutActivating);window.move(app.screens().back()->availableGeometry().topLeft()+QPoint(20,20));}
    window.show();
    QTimer::singleShot(0,load,&QPushButton::click);
    const int result=app.exec();poll.stop();cancelled=true;
    // Native raster reads may finish before cancellation is observed; join before SDK teardown.
    if(pending.valid())pending.get();
    return result;
}


