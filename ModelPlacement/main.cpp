#include "SampleSupport.h"
#include <GeoKernel.Viewer3D/ViewerController.h>
#include <GeoKernel.Viewer3D/ModelPlacement.h>
#include <QApplication>
#include <QCommandLineParser>
#include <QDockWidget>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QStackedWidget>
#include <QStatusBar>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QVulkanInstance>
#include <QVulkanWindow>
#include <QScreen>
#include <future>
#include <atomic>
#include <algorithm>
#include <cmath>
using namespace GeoKernel3D;

// Convert the official regional grid to a GDAL-readable raster without changing samples.
static QString geoidRaster(const QString& source) {
    QFile f(source); if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read geoid.");
    const QByteArray bytes=f.readAll();
    if(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()!=
       "1008a221282bce3b5d17d463f866a45b8011e63768fb718c48f5e126b2873ed9")
        throw std::runtime_error("Unexpected regional geoid version.");
    const int start=bytes.indexOf("\\DATA");
    const auto words=QString::fromLatin1(bytes.mid(bytes.indexOf('\n',start)+1,bytes.indexOf("\\END OF DATA")-bytes.indexOf('\n',start)-1)).split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
    if(words.size()!=181*211)throw std::runtime_error("Invalid geoid sample count.");
    const QString path=QFileInfo(source).absolutePath()+"/egm08d595.asc";
    QByteArray raster;QTextStream out(&raster,QIODevice::WriteOnly);out<<"ncols 211\nnrows 181\nxllcenter 0\nyllcenter 40\ncellsize 0.016666666666666667\nNODATA_value -9999\n";
    for(int i=0;i<words.size();++i){bool ok;double n=words[i].toDouble(&ok);if(!ok||n<40||n>60)throw std::runtime_error("Invalid geoid sample.");out<<words[i]<<((i+1)%211?' ':'\n');}
    out.flush();
    auto saveChanged=[](const QString& name,const QByteArray& bytes){
        QFile current(name);if(current.open(QIODevice::ReadOnly)&&current.readAll()==bytes)return;
        current.close();QSaveFile file(name);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())
            throw std::runtime_error("Cannot save geoid raster or CRS.");
    };
    saveChanged(path,raster);
    saveChanged(QFileInfo(source).absolutePath()+"/egm08d595.prj",
        "GEOGCS[\"ETRS89\",DATUM[\"European_Terrestrial_Reference_System_1989\",SPHEROID[\"GRS 1980\",6378137,298.257222101]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]");
    return path;
}
struct Result {std::unique_ptr<LoadedScene> scene;std::shared_ptr<PlacedModel> model;QString error;int parts=0;int precisionSkipped=0;};
int main(int argc,char** argv){
    QApplication app(argc,argv);app.setApplicationName("ModelPlacement");app.setWindowIcon(QIcon(":/icons/geokernel.ico"));
    QCommandLineParser cli;cli.addHelpOption();cli.addOption({"smoke-report","Non-focus validation report","file"});cli.process(app);const bool smoke=cli.isSet("smoke-report");
    QVulkanInstance instance;instance.setApiVersion(QVersionNumber(1,1,0));if(!instance.create())return 1;
    QMainWindow window;window.setWindowTitle("ModelPlacement — GeoKernel");window.resize(1280,850);window.setProperty("smoke",smoke);
    auto* viewport=createViewerWindow(&instance);
    auto* container=QWidget::createWindowContainer(viewport,&window);
    container->setFocusPolicy(Qt::StrongFocus);
    auto* stack=new QStackedWidget(&window);
    auto* loading=new QLabel("Preparing Sagrada Familia model and terrain...");
    loading->setAlignment(Qt::AlignCenter);
    stack->addWidget(loading);stack->addWidget(container);stack->setCurrentWidget(loading);
    window.setCentralWidget(stack);ViewerController viewer(viewport);
    auto* dock=new QDockWidget("Model placement",&window);dock->setFeatures(QDockWidget::NoDockWidgetFeatures);auto* panel=new QWidget;auto* form=new QFormLayout(panel);dock->setWidget(panel);window.addDockWidget(Qt::LeftDockWidgetArea,dock);
    auto spin=[&](const char* label,double lo,double hi,double value,int decimals){auto* s=new QDoubleSpinBox;s->setDecimals(decimals);s->setRange(lo,hi);s->setValue(value);form->addRow(label,s);return s;};
    auto* lon=spin("Longitude",-180,180,2.174401283,7);auto* lat=spin("Latitude",-90,90,41.403605046,7);
    auto* height=spin("Above terrain (m)",-500,1000,2.938,3);auto* heading=spin("Heading (degrees)",-180,180,.2373346,3);
    auto* pitch=spin("Pitch (degrees)",-180,180,0,2);auto* roll=spin("Roll (degrees)",-180,180,0,2);auto* scale=spin("Scale",.01,100,.958139,6);
    auto* apply=new QPushButton("Apply placement / Reload");form->addRow(apply);auto* cancel=new QPushButton("Cancel loading");cancel->setEnabled(false);form->addRow(cancel);
    auto* focus=new QPushButton("Focus model");form->addRow(focus);auto* reset=new QPushButton("Reset scene view");form->addRow(reset);
    auto* visible=new QCheckBox("Show model");visible->setChecked(true);form->addRow(visible);
    auto* imagery=new QCheckBox("Show orthophoto");imagery->setChecked(true);form->addRow(imagery);
    auto* exaggeration=spin("Vertical exaggeration",.25,10,1,2);exaggeration->setSingleStep(.25);
    auto* note=new QLabel("Approximate demo placement; not a surveyed fit. Height is an offset above the DEM at the model anchor. DEM uses EGM08D595 geoid correction. Textures are retained. Triangles that collapse at scene precision are counted and skipped.");note->setWordWrap(true);form->addRow(note);
    auto* credit=new QLabel("Model: PeeJaa - La Sagrada Familia\nSource and license: downloaded model/license.txt");credit->setWordWrap(true);form->addRow(credit);
    auto* info=new QLabel("Preparing sample data…");info->setWordWrap(true);form->addRow(info);
    int smokeStage=0;double initialHeight=0;QVector3D initialCenter;bool hideChangesFrame=false;int precisionSkipped=0;
    std::shared_ptr<const PlacedModel> placed;std::atomic_bool cancelled=false;std::future<Result> pending;QTimer poll;poll.setInterval(75);
    auto finish=[&](QJsonObject obj){QFile f(cli.value("smoke-report"));if(!f.open(QIODevice::WriteOnly)){app.exit(3);return;}f.write(QJsonDocument(obj).toJson());app.exit(obj["success"].toBool()?0:2);};
    auto fail=[&](QString error){if(!placed)loading->setText("Unable to load the scene.\nUse Apply placement / Reload to retry.");window.statusBar()->showMessage(error);if(smoke)finish({{"success",false},{"error",error}});else QMessageBox::warning(&window,"ModelPlacement",error);};
    auto busy=[&](bool b){if(b&&!placed)loading->setText("Preparing Sagrada Familia model and terrain...");apply->setEnabled(!b);cancel->setEnabled(b);for(auto* s:{lon,lat,height,heading,pitch,roll,scale})s->setEnabled(!b);};
    auto focusModel=[&]{if(!placed)return;Vec3d low{1e100,1e100,1e100},high{-1e100,-1e100,-1e100};for(auto v:placed->vertices){auto p=placed->reference->fromRender({v.x,v.y,v.z});low={std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};high={std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};}viewer.focusModelBounds(placed,low,high);};
    QObject::connect(focus,&QPushButton::clicked,&window,focusModel);QObject::connect(reset,&QPushButton::clicked,&window,[&]{viewer.resetCamera();});
    QObject::connect(visible,&QCheckBox::toggled,&window,[&](bool on){viewer.setModel(on?placed:nullptr);});
    QObject::connect(imagery,&QCheckBox::toggled,&window,[&](bool on){auto a=viewer.layerAppearance(DisplayLayer::Imagery);a.visible=on;viewer.setLayerAppearance(DisplayLayer::Imagery,a);});
    QObject::connect(exaggeration,qOverload<double>(&QDoubleSpinBox::valueChanged),&window,[&](double h){viewer.setHeightScale(float(h));});
    QObject::connect(cancel,&QPushButton::clicked,&window,[&]{cancelled=true;cancel->setEnabled(false);});
    QObject::connect(apply,&QPushButton::clicked,&window,[&]{
        if(pending.valid())return;busy(true);cancel->setEnabled(false);
        auto get=[&](QString name,QString required){return ensureSampleFile(QUrl("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/"+name+".zip"),name+".zip",name,required,&window);};
        auto dem=get("sagrada_familia_terrain","sagrada_familia_terrain.tif");if(dem.isEmpty()){busy(false);fail(window.property("sampleError").toString());return;}
        auto photo=get("sagrada_familia_ortophoto","sagrada_familia_ortophoto.tif");if(photo.isEmpty()){busy(false);fail(window.property("sampleError").toString());return;}
        auto model=get("sagrada_familia_3d_model","model.gltf");if(model.isEmpty()){busy(false);fail(window.property("sampleError").toString());return;}
        auto geoid=ensureSampleFile(QUrl("https://icgc-web-pro.s3.eu-central-1.amazonaws.com/produccio/s3fs-public/EGM08D595_19839.zip"),"EGM08D595.zip","egm08d595","cat80000.gr",&window);
        if(geoid.isEmpty()){busy(false);fail(window.property("sampleError").toString());return;}
        GeoKernel3D::ModelPlacement p;p.longitude=lon->value();p.latitude=lat->value();p.height=height->value();p.heading=heading->value();p.pitch=pitch->value();p.roll=roll->value();p.scale=scale->value();
        cancelled=false;busy(true);window.statusBar()->showMessage("Preparing terrain and model…");
        pending=std::async(std::launch::async,[&,dem,photo,model,geoid,p]{Result r;try{
            SceneOptions o;o.demFile=dem;o.imageFile=photo;o.roadFile.clear();o.buildingFile.clear();o.overrideHeightReference=true;o.height.datum=HeightDatum::Orthometric;o.height.unitAssumed=false;o.height.geoidModel="EGM08D595";o.height.geoidPath=geoidRaster(geoid);
            LoadControl c;c.cancellationFlag=&cancelled;c.cancelled=[&]{return cancelled.load();};r.scene=loadPreparedScene(QFileInfo(dem).absolutePath(),o,r.error,c);if(!r.scene)return r;
            auto source=readGltfModel(model,cancelled,{},32ull*1024*1024,DegenerateTrianglePolicy::SkipZeroAreaSource);
            // Respect the 4 MiB per-placement limit using triangle-aligned chunks.
            const size_t chunk=(3*1024*1024/sizeof(TerrainVertex)/3)*3;r.model=std::make_shared<PlacedModel>();
            std::function<void(size_t,size_t)> appendPart;
            appendPart=[&](size_t first,size_t count){
                if(cancelled)throw std::runtime_error("Loading cancelled.");
                GltfModel part;part.sourceFile=source.sourceFile;part.materials=source.materials;
                part.vertices.assign(source.vertices.begin()+first,source.vertices.begin()+first+count);
                std::shared_ptr<const PlacedModel> piece;
                try {piece=placeModel(part,r.scene->reference(),p,[&](double x,double y){return r.scene->terrainHeight(x,y);});}
                catch(const std::runtime_error& e){
                    if(QString::fromUtf8(e.what())!="Model is too small for scene precision.")throw;
                    if(count==3){++r.precisionSkipped;return;}
                    const size_t half=(count/6)*3;appendPart(first,half);appendPart(first+half,count-half);return;
                }
                if(r.model->vertices.empty())*r.model=*piece;
                else r.model->vertices.insert(r.model->vertices.end(),piece->vertices.begin(),piece->vertices.end());
                ++r.parts;
            };
            for(size_t first=0;first<source.vertices.size();first+=chunk)appendPart(first,std::min(chunk,source.vertices.size()-first));
            if(r.model->vertices.empty())throw std::runtime_error("No triangles survive scene precision.");
            QVector3D lo(1e10,1e10,1e10),hi(-1e10,-1e10,-1e10);for(auto v:r.model->vertices)for(int i=0;i<3;++i){const float a=i==0?v.x:i==1?v.y:v.z;lo[i]=std::min(lo[i],a);hi[i]=std::max(hi[i],a);}r.model->center=(lo+hi)*.5f;r.model->radius=(hi-lo).length()*.5f;
        }catch(const std::exception& e){r.error=e.what();r.scene.reset();r.model.reset();}return r;});poll.start();
    });
    QObject::connect(&poll,&QTimer::timeout,&window,[&]{if(!pending.valid()||pending.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;poll.stop();auto r=pending.get();busy(false);if(cancelled){if(!placed)loading->setText("Loading cancelled.\nUse Apply placement / Reload to retry.");window.statusBar()->showMessage("Cancelled; previous scene retained.");return;}if(!r.scene||!r.model){fail(r.error);return;}QString error;if(!viewer.setScene(std::move(r.scene),error)){fail(error);return;}precisionSkipped=r.precisionSkipped;placed=r.model;viewer.setModel(visible->isChecked()?placed:nullptr);viewer.setHeightScale(float(exaggeration->value()));auto a=viewer.layerAppearance(DisplayLayer::Imagery);a.visible=imagery->isChecked();viewer.setLayerAppearance(DisplayLayer::Imagery,a);focusModel();stack->setCurrentWidget(container);info->setText(QString("%1 triangles · %2 placement chunks\n%3 precision-collapsed triangles skipped\nEGM08D595 corrected terrain").arg(placed->vertices.size()/3).arg(r.parts).arg(r.precisionSkipped));window.statusBar()->showMessage("Left: pan | Wheel/right: zoom | Middle/Ctrl+left: orbit | Shift+left: look");
        if(smoke)QTimer::singleShot(4000,&window,[&]{
            auto* vk=qobject_cast<QVulkanWindow*>(viewport);const QImage image=vk->grab();
            if(smokeStage==0){
                image.save(cli.value("smoke-report")+".initial.png");initialHeight=placed->anchor.height;initialCenter=placed->center;
                visible->setChecked(false);
                QTimer::singleShot(1000,&window,[&,image]{
                    auto hidden=qobject_cast<QVulkanWindow*>(viewport)->grab();hideChangesFrame=!image.isNull()&&!hidden.isNull()&&image!=hidden;
                    visible->setChecked(true);height->setValue(height->value()+10);heading->setValue(heading->value()+15);scale->setValue(.85);smokeStage=1;apply->click();
                });return;
            }
            bool saved=image.save(cli.value("smoke-report")+".png");auto state=viewer.modelState();auto d=viewer.renderDiagnostics();
            exaggeration->setValue(2);bool exaggerates=viewer.heightScale()==2;exaggeration->setValue(1);
            bool transformed=std::abs(placed->anchor.height-initialHeight-10)<.001&&(placed->center-initialCenter).length()>0;
            finish({{"success",saved&&state.attached&&state.drawnVertices>0&&state.error.isEmpty()&&d.renderError.isEmpty()&&d.imageryStreamError.isEmpty()&&transformed&&hideChangesFrame&&exaggerates},
                {"vertices",int(placed->vertices.size())},{"precisionSkipped",precisionSkipped},{"drawnVertices",int(state.drawnVertices)},
                {"modelError",state.error},{"renderError",d.renderError},{"imageryError",d.imageryStreamError},{"anchorHeight",placed->anchor.height},
                {"placementControlsApplied",transformed},{"hideChangesFrame",hideChangesFrame},{"exaggerationApplied",exaggerates}});
        });
    });
    if(smoke){window.setWindowFlags(Qt::Tool|Qt::WindowDoesNotAcceptFocus|Qt::WindowTransparentForInput);window.setAttribute(Qt::WA_ShowWithoutActivating);window.move(app.screens().back()->availableGeometry().topLeft()+QPoint(20,20));}
    window.show();QTimer::singleShot(0,&window,[&]{
        if(smoke&&(stack->currentWidget()!=loading||container->isVisible()||viewport->isVisible())){
            finish({{"success",false},{"error","Viewer visible before scene loading"}});return;
        }
        apply->click();
    });int result=app.exec();poll.stop();cancelled=true;if(pending.valid())pending.get();return result;
}
