#include "SampleSupport.h"
#include "MeasurementPanel.h"
#include <GeoKernel.Viewer3D/ViewerController.h>
#include <QApplication>
#include <QMainWindow>

#include <QFileInfo>
#include <QDir>
#include <QDockWidget>
#include <QFormLayout>
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
#include <future>
#include <atomic>
using namespace GeoKernel3D;
struct LoadResult { std::unique_ptr<LoadedScene> scene; QString error; };

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    app.setApplicationName("Measurement3D");
    app.setWindowIcon(QIcon(":/icons/geokernel.ico"));
    QVulkanInstance instance; instance.setApiVersion(QVersionNumber(1,1,0));
    if(!instance.create()) {
        QMessageBox::critical(nullptr,"Measurement3D","Vulkan could not be initialized.");
        return 1;
    }
    // Lifetime: controller, container/window, then Vulkan instance.
    QMainWindow window; window.setWindowTitle("Measurement3D — GeoKernel");window.resize(1400,850);
    auto* viewport=createViewerWindow(&instance);
    auto* container=QWidget::createWindowContainer(viewport,&window);container->setFocusPolicy(Qt::StrongFocus);
    auto* stack=new QStackedWidget(&window);
    auto* welcome=new QLabel("Preparing Sagrada Família terrain and orthophoto…");
    welcome->setAlignment(Qt::AlignCenter);stack->addWidget(welcome);stack->addWidget(container);stack->setCurrentWidget(welcome);window.setCentralWidget(stack);
    ViewerController viewer(viewport);
    auto* measurementDock=new QDockWidget("Measurement",&window);
    auto* measurement=new MeasurementPanel(viewer,measurementDock);
    measurementDock->setWidget(measurement);measurementDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    window.addDockWidget(Qt::RightDockWidgetArea,measurementDock);
    auto* dock=new QDockWidget("Terrain data",&window);dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto* panel=new QWidget;auto* form=new QFormLayout(panel);
    QString demPath,photoPath;
    auto* dataset=new QLabel("Sagrada Família — terrain and orthophoto");dataset->setWordWrap(true);form->addRow(dataset);
    auto* note=new QLabel("Sample DEM heights are an unverified preview. No vertical datum conversion is applied.");
    note->setWordWrap(true);form->addRow(note);
    auto* load=new QPushButton("Reload sample");auto* cancel=new QPushButton("Cancel loading");cancel->setEnabled(false);
    form->addRow(load);form->addRow(cancel);auto* progress=new QProgressBar;progress->setRange(0,0);progress->hide();form->addRow(progress);
    auto* imagery=new QCheckBox("Show orthophoto");imagery->setChecked(true);imagery->setEnabled(false);form->addRow(imagery);
    auto* exaggeration=new QDoubleSpinBox;
    exaggeration->setRange(0.25,10.0);
    exaggeration->setSingleStep(0.25);
    exaggeration->setDecimals(2);
    exaggeration->setValue(1.0);
    exaggeration->setSuffix(" ×");
    exaggeration->setToolTip("Scales displayed terrain height only. Source elevations are unchanged.");
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
    auto busy=[&](bool on){load->setEnabled(!on);cancel->setEnabled(on);progress->setVisible(on);};
    auto fail=[&](const QString& error){window.statusBar()->showMessage(error);QMessageBox::warning(&window,"Unable to load terrain",error);};
    QTimer poll;poll.setInterval(75);
    QObject::connect(cancel,&QPushButton::clicked,&window,[&]{cancelled=true;cancel->setEnabled(false);window.statusBar()->showMessage("Cancelling…");});
    QObject::connect(load,&QPushButton::clicked,&window,[&]{
        if(pending.valid())return;
        busy(true);cancel->setEnabled(false);window.statusBar()->showMessage("Preparing sample data…");
        auto unavailable=[&]{busy(false);window.statusBar()->showMessage("Sample unavailable. Click Reload sample to retry.");
};
        demPath=ensureSampleFile(
            QUrl("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_terrain.zip"),
            "sagrada_familia_terrain.zip","sagrada_familia_terrain","sagrada_familia_terrain.tif",&window);
        if(demPath.isEmpty()){unavailable();return;}
        photoPath=ensureSampleFile(
            QUrl("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_ortophoto.zip"),
            "sagrada_familia_ortophoto.zip","sagrada_familia_ortophoto","sagrada_familia_ortophoto.tif",&window);
        if(photoPath.isEmpty()){unavailable();return;}
        const QString folder=QFileInfo(demPath).absolutePath();const QDir dir(folder);
        SceneOptions options;options.demFile=dir.relativeFilePath(QFileInfo(demPath).absoluteFilePath());
        options.imageFile=photoPath.isEmpty()?QString():dir.relativeFilePath(QFileInfo(photoPath).absoluteFilePath());
        // Explicitly disable vector discovery: measurement clicks query only the DEM.
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
        QString error;
        if(!viewer.setScene(std::move(r.scene),error)){fail(error);return;}
        measurement->resetScene();
        viewer.setHeightScale(static_cast<float>(exaggeration->value()));
        viewer.resetCamera();stack->setCurrentWidget(container);imagery->setEnabled(pendingPhoto);imagery->setChecked(pendingPhoto);
        auto appearance=viewer.layerAppearance(DisplayLayer::Imagery);appearance.visible=pendingPhoto;viewer.setLayerAppearance(DisplayLayer::Imagery,appearance);
        info->setText(QString("DEM: %1\nOrthophoto: %2\nHeights: %3").arg(QFileInfo(demPath).fileName(), pendingPhoto ? QFileInfo(photoPath).fileName() : "None", "Unknown — unverified preview"));window.statusBar()->showMessage("Terrain loaded | Left: pan | Wheel/right: zoom | Middle/Ctrl+left: orbit");
    });
    window.show();
    QTimer::singleShot(0,load,&QPushButton::click);
    const int result=app.exec();poll.stop();cancelled=true;
    // Native raster reads may finish before cancellation is observed; join before SDK teardown.
    if(pending.valid())pending.get();
    return result;
}


