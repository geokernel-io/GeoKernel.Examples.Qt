#include <GeoKernel.Globe/RuntimeWindow.h>
#include <GeoKernel.Globe/RuntimeVectorPresentation.h>
#include <GeoKernel.Globe/RuntimeBasemaps.h>
#include <GeoKernel.Globe/RuntimeTerrainAnalysis.h>
#include <GeoKernel.Globe/RuntimeVectorPolygonFill.h>
#include <GeoKernel.Globe/RuntimeFactory.h>
#include <QCoreApplication>
#include <QDir>
#include <QTimer>
#include <iostream>
#include <QApplication>
#include <QIcon>
#include <QMainWindow>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QVulkanInstance>
#include <QComboBox>
#include <QColorDialog>
#include <QCheckBox>
#include <QSlider>
#include <QSignalBlocker>
#include <functional>

namespace Runtime = GeoKernel3D::GlobeRuntime;

inline Runtime::RuntimeOptions offlineOptions()
{
    Runtime::RuntimeOptions options;
    options.networkEnabled = false;
    options.initialTileSource = -1;
    options.terrainEnabled = false;
    options.terrainExaggeration = 1.0f;
    options.atmosphereEnabled = false;
    options.spaceBackgroundEnabled = false;
    options.sunlight = false;
    return options;
}

inline void initializeDataPaths()
{
    const auto root = QCoreApplication::applicationDirPath();
    qputenv("PROJ_DATA", (root + "/proj-data").toUtf8());
    qputenv("GDAL_DATA", (root + "/gdal-data").toUtf8());
    qputenv("GDAL_DRIVER_PATH", (root + "/gdalplugins").toUtf8());
}

inline void require(bool condition, const QString& message) {
    if (!condition)
        throw std::runtime_error(message.toStdString());
}

class DemoWindow : public QMainWindow {
public:
    Runtime::RuntimeWindow* globe;
    QToolBar* bar;
    std::atomic_bool cancelled{false};
    bool busy=false;

    explicit DemoWindow(QVulkanInstance& instance, const QString& title) {
        auto options=offlineOptions();
        options.networkEnabled=true;
        options.initialTileSource=Runtime::runtimeTileSourceNames().indexOf("Esri Satellite");
        options.atmosphereEnabled=true;
        globe = new Runtime::RuntimeWindow(options);
        globe->setVulkanInstance(&instance);
        setCentralWidget(QWidget::createWindowContainer(globe));
        bar=addToolBar("Controls");
        setWindowTitle(title); resize(1100,760);
        statusBar()->showMessage("Loading Sagrada Familia buildings...");
    }
    
    ~DemoWindow() override {
        cancelled=true;
        QThreadPool::globalInstance()->waitForDone();
        delete takeCentralWidget();
    }
    
    template<class F> void action(const QString& name,F callback) {
        auto* a=bar->addAction(name);
        connect(a,&QAction::triggered,this,[this,callback] {
            if(busy)return;
            try { callback(); } catch(const std::exception& e) { report(e.what()); }
        });
    }

    void report(const QString& message) { 
        statusBar()->showMessage(message);
    }

    template<class Work,class Done> void task(Work work,Done done) {
        if(busy)
            return;
        busy=true;
        cancelled=false;
        using T=decltype(work());
        auto* watcher=new QFutureWatcher<T>(this);
        connect(watcher,&QFutureWatcher<T>::finished,this,[this,watcher,done] {
            busy=false;
            try {
                auto result=watcher->result(); 
                if(!cancelled) {
                    done(std::move(result));
                }
            }
            catch(const std::exception& e) {
                report(e.what());
            }
            catch(...) {
                report("SDK worker failed");
            }
            watcher->deleteLater();
        });

        watcher->setFuture(QtConcurrent::run(std::move(work)));
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico")));
    initializeDataPaths();

    QVulkanInstance instance;
    if (!instance.create()) {
        std::cerr << "Vulkan initialization failed\n";
        return 1;
    }

    try {
        DemoWindow w(instance, "Globe — Sagrada Familia Building Styles");
        auto data=std::make_shared<Runtime::VectorImportResult>();
        auto rejectedFills=std::make_shared<int>(0);
        auto style=std::make_shared<QJsonObject>(
            QJsonObject{
                {"fill",true},
                {"labels",false},
                {"labelField","name"},
                {"categoryField","type"},
                {"baseColor","#efb84c"},
                {"labelSize",12}
            }
        );
        auto* fill=new QCheckBox("Fill");fill->setChecked(true);
        auto* labels=new QCheckBox("Labels");labels->setChecked(false);
        auto* categories=new QComboBox;categories->setToolTip("Color buildings by a source attribute");
        auto* labelField=new QComboBox;labelField->setToolTip("Label field from the shapefile");
        auto* opacity=new QSlider(Qt::Horizontal);opacity->setRange(15,100);opacity->setValue(60);opacity->setMaximumWidth(100);
        w.bar->addWidget(fill);
        w.bar->addWidget(labels);
        w.bar->addWidget(new QLabel(" Color by: "));
        w.bar->addWidget(categories);
        w.bar->addWidget(new QLabel(" Label: "));
        w.bar->addWidget(labelField);
        w.bar->addWidget(new QLabel(" Opacity: "));
        w.bar->addWidget(opacity);
        
        auto show=[&w,data,style,opacity]{
            if(!data->mesh)
                return;
            QString error;
            if(!Runtime::applyVectorPresentation(*data,*style,error)) {
                w.report(error);
                return;
            }

            data->mesh->opacity=opacity->value()/100.f;
            data->mesh->lineWidth=1.5f;
            if(data->labelMesh)
                data->labelMesh->opacity=1.f;
            w.globe->setVectorLayers({
                data->mesh,data->labelMesh
            });
        };

        QObject::connect(fill,&QCheckBox::toggled,&w,[style,show](bool on){(*style)["fill"]=on;show();});
        QObject::connect(labels,&QCheckBox::toggled,&w,[style,show](bool on){(*style)["labels"]=on;show();});
        QObject::connect(categories,&QComboBox::currentIndexChanged,&w,[data,style,categories,show](int){
            if(!data->mesh)
                return;
            const auto field=categories->currentData().toString();
            (*style)["categoryField"]=field;
            (*style)["categories"]=field.isEmpty()?QJsonObject{}:Runtime::vectorCategories(*data,field);
            show();
        });
        QObject::connect(labelField,&QComboBox::currentIndexChanged,&w,[style,labelField,show](int){
            (*style)["labelField"]=labelField->currentText();show();
        });
        QObject::connect(opacity,&QSlider::valueChanged,&w,[show](int){show();});
        w.action("Base color...",[&w,style,show]{
            auto c=QColorDialog::getColor(QColor((*style)["baseColor"].toString()),&w,"Uniform / fallback building color");
            if(c.isValid()){(*style)["baseColor"]=c.name();show();}
        });
        w.action("Zoom to buildings",[&w,data]{if(data->mesh)w.globe->setCamera(data->camera);});
        w.action("Esri on/off",[&w]{
            w.globe->setTileSource(w.globe->tileSource()<0?Runtime::runtimeTileSourceNames().indexOf("Esri Satellite"):-1);
        });
        auto* credit=new QLabel(Runtime::Basemaps::attribution(Runtime::runtimeTileSourceNames().indexOf("Esri Satellite")));
        w.statusBar()->addPermanentWidget(credit);
        w.globe->identifyEnabled=true;
        w.globe->featurePicked=[&w](auto pick) {
            w.statusBar()->showMessage(QString("Building record: %1").arg(pick.featureId));
        };
        const auto path=QCoreApplication::applicationDirPath()+"/data/buildings.shp";

        w.task([&w,path,rejectedFills] {

            auto result=Runtime::importVector(path,0,"EPSG:25831",w.cancelled);
            
            if(!result.error.isEmpty()||!result.mesh)
                return result;

            for(const auto& polygon:result.polygons){
                if(w.cancelled)return result;
                Runtime::GlobeVectorMesh scratch;scratch.origin=result.mesh->origin;
                QString error;
                if(Runtime::appendVectorPolygonFill(scratch,polygon,QColor("#ffffff"),error,&w.cancelled))
                    result.renderPolygons.push_back(polygon);
                else {
                    ++*rejectedFills;
                    std::cout<<"Outline-only: "<<error.toStdString()<<std::endl;
                }
            }

            if(result.renderPolygons.empty()) {
                result.error="No polygons passed SDK fill validation.";
                return result;
            }

            return result;
        },[&w,data,style,categories,labelField,show,rejectedFills](auto result) {
            require(result.error.isEmpty()&&result.mesh,result.error);
            require(!result.budgetExceeded&&!result.hasMorePages,"Building source did not load completely");
            result.camera.distance *= .28;
            result.camera.pitch = 1.32;
            result.camera.heading = .60;
            *data=std::move(result);
            const auto fields=Runtime::vectorFields(*data);
            require(fields.contains("type")&&fields.contains("name"),"Expected building fields missing");

            {
                QSignalBlocker a(categories),b(labelField);
                categories->addItem("Uniform",QString{});
                
                for(const auto& field:fields)
                    categories->addItem(field,field);
                
                labelField->addItems(fields);
                categories->setCurrentIndex(categories->findData("type"));
                labelField->setCurrentText("name");
            }
            (*style)["categories"]=Runtime::vectorCategories(*data,"type");
            show();
            w.globe->setCamera(data->camera);
            w.statusBar()->showMessage(QString("%1 buildings | %2 outline-only (SDK fill validation) | Click to identify").arg(data->features).arg(*rejectedFills));
            std::cout<<"Loaded "<<data->features<<" buildings; fields="<<fields.join(",").toStdString()<<std::endl;
        });

        w.show();
        return app.exec();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
