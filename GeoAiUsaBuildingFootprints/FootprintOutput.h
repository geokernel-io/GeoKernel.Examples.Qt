#pragma once
#include <QLibrary>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <memory>
#include <vector>
#include <QThread>
#include <QEventLoop>
#include <QProgressDialog>
#include <atomic>
#include <cmath>
#include <QFileInfo>
#include <QElapsedTimer>
#include <algorithm>
#include <QTemporaryDir>
#include "Common/Gdal/GdalRasterProvider.h"
#include "GeoKernelAI.h"
#include "GeoKernelAnalysis.h"
#include "Serialization/Wkb/GisWkbReader.h"
#include "CoordinateSystems/CoordinateSystemFactory.h"

// Use the GDAL runtime already shipped by the SDK through its stable C ABI.
// SDK 1.5.36's C++ geometry validation path crashes on these rasters. This
// adapter writes four-connected polygons without that geometry-transform path.
// No external GDAL headers, import libraries, installation or Python is needed.
class FootprintOutput {
    QLibrary library;
    template<class T> T resolve(const char* name) {
        auto address = library.resolve(name);
        if (!address) throw std::runtime_error(QString("Missing GDAL function: %1").arg(name).toStdString());
        return reinterpret_cast<T>(address);
    }
public:
    FootprintOutput() {
#ifdef Q_OS_WIN
        library.setFileName(QDir(QCoreApplication::applicationDirPath()).filePath("gdal.dll"));
#else
        library.setFileName("gdal");
#endif
        library.setLoadHints(QLibrary::PreventUnloadHint);
        if (!library.load()) throw std::runtime_error(library.errorString().toStdString());
    }
    QString metricRaster(const QString& path, QWidget* parent=nullptr) {
        using namespace GeoKernel::Formats::Common::Gdal;
        GdalRasterProvider provider; QString error;
        if(!provider.open(path,&error)) throw std::runtime_error(error.toStdString());
        const auto metadata=provider.metadata();
        if(metadata.epsgCode!=4326) return path;
        const auto& t=metadata.worldTransform;
        const double longitude=t.upperLeftCenterX+metadata.width*t.pixelSizeX*.5;
        const double latitude=t.upperLeftCenterY+metadata.height*t.pixelSizeY*.5;
        const int zone=std::clamp(static_cast<int>(std::floor((longitude+180)/6))+1,1,60);
        const int epsg=(latitude>=0?32600:32700)+zone;
        const QString destination=path+QString(".display-epsg%1.vrt").arg(epsg);
        if(QFileInfo::exists(destination)) return destination;
        auto open=resolve<void*(*)(const char*,unsigned int,const char* const*,const char* const*,const char* const*)>("GDALOpenEx");
        auto close=resolve<void(*)(void*)>("GDALClose");
        auto newOptions=resolve<void*(*)(char**,void*)>("GDALWarpAppOptionsNew");
        auto freeOptions=resolve<void(*)(void*)>("GDALWarpAppOptionsFree");
        auto setProgress=resolve<void(*)(void*,int(*)(double,const char*,void*),void*)>("GDALWarpAppOptionsSetProgress");
        auto warp=resolve<void*(*)(const char*,void*,int,void**,const void*,int*)>("GDALWarp");
        QString failure; std::atomic_bool cancel{false};
        QProgressDialog progress("Preparing geographic raster in a metric display CRS...","Cancel",0,0,parent);
        progress.setWindowModality(Qt::ApplicationModal);progress.setMinimumDuration(0);
        QObject::connect(&progress,&QProgressDialog::canceled,[&]{cancel.store(true);});
        const QString temporary=destination+".part.vrt";
        QEventLoop loop;
        auto* worker=QThread::create([&]{
            using Handle=std::unique_ptr<void,void(*)(void*)>;
            Handle source(open(path.toUtf8().constData(),2,nullptr,nullptr,nullptr),close);
            QByteArray target=QString("EPSG:%1").arg(epsg).toUtf8();
            char* args[]={const_cast<char*>("-t_srs"),target.data(),const_cast<char*>("-r"),const_cast<char*>("bilinear"),
                const_cast<char*>("-of"),const_cast<char*>("VRT"),nullptr};
            Handle options(newOptions(args,nullptr),freeOptions);
            if(!source || !options){failure="Could not prepare geographic raster warp.";return;}
            auto callback=[](double,const char*,void* context)->int{return static_cast<std::atomic_bool*>(context)->load()?0:1;};
            setProgress(options.get(),callback,&cancel);
            void* sources[]={source.get()};int usage=0;
            Handle result(warp(temporary.toUtf8().constData(),nullptr,1,sources,options.get(),&usage),close);
            if(!result || usage || cancel.load()) failure="Geographic raster preparation failed or was cancelled.";
        });
        QObject::connect(worker,&QThread::finished,&loop,&QEventLoop::quit);
        worker->start();loop.exec();worker->wait();delete worker;
        if(!failure.isEmpty()){QFile::remove(temporary);throw std::runtime_error(failure.toStdString());}
        if(!QFile::rename(temporary,destination)) throw std::runtime_error("Could not commit projected display raster.");
        return destination;
    }
    std::unique_ptr<GeoKernel::Core::Layers::GisLayerVector> loadOverlay(
        const QString& path, const QString& projectionWkt) {
        using namespace GeoKernel::Core;
        auto open = resolve<void*(*)(const char*,unsigned int,const char* const*,const char* const*,const char* const*)>("GDALOpenEx");
        auto close = resolve<void(*)(void*)>("GDALClose");
        auto getLayer = resolve<void*(*)(void*,int)>("GDALDatasetGetLayer");
        auto next = resolve<void*(*)(void*)>("OGR_L_GetNextFeature");
        auto destroyFeature = resolve<void(*)(void*)>("OGR_F_Destroy");
        auto getGeometry = resolve<void*(*)(void*)>("OGR_F_GetGeometryRef");
        auto getId = resolve<qint64(*)(void*,int)>("OGR_F_GetFieldAsInteger64");
        auto wkbSize = resolve<int(*)(void*)>("OGR_G_WkbSize");
        auto exportWkb = resolve<int(*)(void*,int,unsigned char*)>("OGR_G_ExportToWkb");
        using Handle = std::unique_ptr<void,void(*)(void*)>;
        Handle dataset(open(path.toUtf8().constData(),4,nullptr,nullptr,nullptr),close);
        if (!dataset) throw std::runtime_error("Could not open footprint output for display.");
        void* source=getLayer(dataset.get(),0);
        if (!source) throw std::runtime_error("Footprint output has no layer.");
        auto layer=Layers::GisLayerVector::createInMemory("Building prediction polygons",Shapes::GisShapeType::Polygon);
        layer->setCoordinateSystem(CoordinateSystems::CoordinateSystemFactory::fromUserInput(projectionWkt));
        layer->addAttributeDefinition({"instance_id", Layers::Defs::GisAttributeType::Integer,0,0});
        while (void* raw=next(source)) {
            Handle feature(raw,destroyFeature);
            void* geometry=getGeometry(raw);
            if (!geometry) continue;
            QByteArray wkb(wkbSize(geometry),Qt::Uninitialized);
            if (exportWkb(geometry,1,reinterpret_cast<unsigned char*>(wkb.data()))!=0)
                throw std::runtime_error("Could not read footprint geometry for display.");
            auto shape=Serialization::Wkb::GisWkbReader::read(wkb);
            if (!shape) throw std::runtime_error("Footprint geometry was not decoded.");
            shape->attributes().insert("instance_id",getId(raw,0));
            if (!layer->addShape(std::move(shape))) throw std::runtime_error("Could not add footprint geometry to overlay.");
        }
        return layer;
    }
    void saveShapefile(const QByteArray& geoJson, const QString& path) {
        QTemporaryDir temporary;
        if (!temporary.isValid()) throw std::runtime_error("Could not create export workspace.");
        QFile input(temporary.filePath("buildings.geojson"));
        if (!input.open(QIODevice::WriteOnly) || input.write(geoJson)!=geoJson.size())
            throw std::runtime_error("Could not prepare buildings for export.");
        input.close();
        auto open = resolve<void*(*)(const char*,unsigned int,const char* const*,const char* const*,const char* const*)>("GDALOpenEx");
        auto close = resolve<void(*)(void*)>("GDALClose");
        auto optionsNew = resolve<void*(*)(char**,void*)>("GDALVectorTranslateOptionsNew");
        auto optionsFree = resolve<void(*)(void*)>("GDALVectorTranslateOptionsFree");
        auto translate = resolve<void*(*)(const char*,void*,int,void**,const void*,int*)>("GDALVectorTranslate");
        using Handle=std::unique_ptr<void,void(*)(void*)>;
        Handle source(open(input.fileName().toUtf8().constData(),4,nullptr,nullptr,nullptr),close);
        char* args[]={const_cast<char*>("-f"),const_cast<char*>("ESRI Shapefile"),
                      const_cast<char*>("-overwrite"),const_cast<char*>("-lco"),const_cast<char*>("ENCODING=UTF-8"),nullptr};
        Handle options(optionsNew(args,nullptr),optionsFree);
        if (!source || !options) throw std::runtime_error("Could not prepare Shapefile export.");
        void* sources[]={source.get()};int usage=0;
        Handle output(translate(path.toUtf8().constData(),nullptr,1,sources,options.get(),&usage),close);
        if (!output || usage) throw std::runtime_error("Shapefile export failed. Check the output location and file permissions.");
        output.reset();
        for(const QString& suffix:{QString("shp"),QString("shx"),QString("dbf"),QString("prj")})
            if (!QFileInfo::exists(QFileInfo(path).dir().filePath(QFileInfo(path).completeBaseName()+"."+suffix)))
                throw std::runtime_error("Shapefile export is incomplete.");
    }
    qint64 write(const GeoKernel::AI::AiRasterInferenceResult& labels,
                 const QString& outputPath, const std::function<bool()>& cancel) {
        auto getDriver = resolve<void*(*)(const char*)>("GDALGetDriverByName");
        auto setTransform = resolve<int(*)(void*,double*)>("GDALSetGeoTransform");
        auto close = resolve<void(*)(void*)>("GDALClose");
        auto create = resolve<void*(*)(void*,const char*,int,int,int,int,char**)>("GDALCreate");
        auto getBand = resolve<void*(*)(void*,int)>("GDALGetRasterBand");
        auto rasterIO = resolve<int(*)(void*,int,int,int,int,int,void*,int,int,int,int,int)>("GDALRasterIO");
        auto newSrs = resolve<void*(*)(const char*)>("OSRNewSpatialReference");
        auto destroySrs = resolve<void(*)(void*)>("OSRDestroySpatialReference");
        auto createLayer = resolve<void*(*)(void*,const char*,void*,int,char**)>("GDALDatasetCreateLayer");
        auto createField = resolve<void*(*)(const char*,int)>("OGR_Fld_Create");
        auto destroyField = resolve<void(*)(void*)>("OGR_Fld_Destroy");
        auto addField = resolve<int(*)(void*,void*,int)>("OGR_L_CreateField");
        auto polygonize = resolve<int(*)(void*,void*,void*,int,char**,int(*)(double,const char*,void*),void*)>("GDALPolygonize");
        auto featureCount = resolve<qint64(*)(void*,int)>("OGR_L_GetFeatureCount");
        auto openVector = resolve<void*(*)(const char*,unsigned int,const char* const*,const char* const*,const char* const*)>("GDALOpenEx");
        auto getLayer = resolve<void*(*)(void*,int)>("GDALDatasetGetLayer");
        using Handle = std::unique_ptr<void,void(*)(void*)>;
        Handle source(create(getDriver("MEM"),"",labels.width,labels.height,1,5,nullptr), close);
        Handle mask(create(getDriver("MEM"),"",labels.width,labels.height,1,1,nullptr), close);
        if (!source || !mask) throw std::runtime_error("Could not allocate polygonization buffers.");
        const qint64 pixels = static_cast<qint64>(labels.width)*labels.height;
        if (labels.dataType != GeoKernel::AI::AiTensorDataType::Int32 || labels.data.size()!=pixels*sizeof(qint32))
            throw std::runtime_error("Expected a single-band Int32 label raster.");
        if (labels.geoTransform.size()!=6) throw std::runtime_error("Missing raster geotransform.");
        auto transform=labels.geoTransform;
        if (setTransform(source.get(),transform.data())!=0 ||
            rasterIO(getBand(source.get(),1),1,0,0,labels.width,labels.height,
                     const_cast<char*>(labels.data.constData()),labels.width,labels.height,5,0,0)!=0)
            throw std::runtime_error("Could not prepare in-memory polygonization source.");
        QByteArray foreground(pixels, Qt::Uninitialized);
        const auto* values=reinterpret_cast<const qint32*>(labels.data.constData());
        for (qint64 i=0;i<pixels;++i) foreground[i]=values[i]>0 ? char(255) : char(0);
        if (rasterIO(getBand(mask.get(),1),1,0,0,labels.width,labels.height,foreground.data(),labels.width,labels.height,1,0,0)!=0)
            throw std::runtime_error("Could not write polygon selection mask.");
        Handle srs(newSrs(labels.projectionWkt.toUtf8().constData()),destroySrs);
        // Output closes before its spatial reference (reverse declaration order).
        QFile::remove(outputPath);
        Handle output(create(getDriver("GeoJSON"),outputPath.toUtf8().constData(),0,0,0,0,nullptr),close);
        if (!output) throw std::runtime_error("Could not create footprint GeoJSON.");
        void* layer=createLayer(output.get(),"buildings",srs.get(),3,nullptr);
        if (!layer) throw std::runtime_error("Could not create footprint layer.");
        Handle field(createField("instance_id",12),destroyField);
        if (!field || addField(layer,field.get(),1)!=0) throw std::runtime_error("Could not create footprint ID field.");
        auto callback=[](double,const char*,void* context)->int {
            const auto& check=*static_cast<const std::function<bool()>*>(context);
            return (!check || !check()) ? 1 : 0;
        };
        if (polygonize(getBand(source.get(),1),getBand(mask.get(),1),layer,0,nullptr,callback,const_cast<std::function<bool()>*>(&cancel))!=0)
            throw std::runtime_error("Footprint polygonization failed or was cancelled.");
        // GeoJSON's write-only layer does not report its buffered feature count.
        // Close/flush it, then count from the completed dataset.
        output.reset();
        Handle completed(openVector(outputPath.toUtf8().constData(),4,nullptr,nullptr,nullptr),close);
        if (!completed) throw std::runtime_error("Could not reopen completed footprint GeoJSON.");
        void* completedLayer=getLayer(completed.get(),0);
        if (!completedLayer) throw std::runtime_error("Completed footprint layer is missing.");
        return featureCount(completedLayer,1);
    }
};

struct DemoInferenceResult {
    GeoKernel::AI::AiInstanceSegmentationResult inference;
    qint64 polygonCount=0;
};

namespace MaskRcnn {
struct Candidate {
    double x1=0,y1=0,x2=0,y2=0,score=0;
    int x=0,y=0,width=0,height=0;
    QByteArray mask;
};
inline double iou(const Candidate& a,const Candidate& b) {
    const double intersection=std::max(0.,std::min(a.x2,b.x2)-std::max(a.x1,b.x1))*
        std::max(0.,std::min(a.y2,b.y2)-std::max(a.y1,b.y1));
    const double area=(a.x2-a.x1)*(a.y2-a.y1)+(b.x2-b.x1)*(b.y2-b.y1)-intersection;
    return area>0?intersection/area:0;
}
inline QVector<int> starts(int size) {
    QVector<int> result;
    for(int position=0;position+512<size;position+=384) result.append(position);
    const int last=std::max(0,size-512);
    if(result.isEmpty() || result.last()!=last) result.append(last);
    return result;
}
}

// Mask R-CNN outputs variable per-instance masks. They must not pass through
// semantic probability blending or connected-component instance separation.
inline DemoInferenceResult runDemoInference(
    const GeoKernel::Analysis::AiInstanceVectorizationRequest& request,
    GeoKernel::Analysis::AnalysisProgressCallback progress = {}, double confidenceThreshold = .5) {
    using namespace GeoKernel::AI;
    using namespace GeoKernel::Formats::Common::Gdal;
    DemoInferenceResult result;
    QElapsedTimer timer;timer.start();
    auto cancelled=[&] {
        const bool stop=request.inference.isCancellationRequested && request.inference.isCancellationRequested();
        if(stop) result.inference.labels.cancelled=true;
        return stop;
    };
    auto notify=[&](int value,const QString& message) {
        if(progress){GeoKernel::Analysis::AnalysisProgress state;state.percent=value;state.message=message;progress(state);}
    };
    const auto package=AiModelPackage::open(request.inference.modelPackagePath);
    const auto errors=package.validationErrors();
    if(!errors.isEmpty()) throw std::runtime_error(errors.join(" ").toStdString());
    if(package.manifest().id!="giswqs-geoai-usa-maskrcnn-resnet50-fpn")
        throw std::runtime_error("Select the GeoAI USA Mask R-CNN model package.");
    GdalRasterProvider raster;QString error;
    if(!raster.open(request.inference.rasterPath,&error)) throw std::runtime_error(error.toStdString());
    const auto metadata=raster.metadata();
    if(!metadata.hasWorldTransform || metadata.bandCount<3 || metadata.width<=0 || metadata.height<=0)
        throw std::runtime_error("A georeferenced RGB raster is required.");
    const auto& t=metadata.worldTransform;
    auto& labels=result.inference.labels;
    labels.width=metadata.width;labels.height=metadata.height;labels.bandCount=1;labels.dataType=AiTensorDataType::Int32;
    labels.projectionWkt=metadata.projectionWkt;
    labels.geoTransform={t.upperLeftCenterX-(t.pixelSizeX+t.rotationY)*.5,t.pixelSizeX,t.rotationY,
        t.upperLeftCenterY-(t.rotationX+t.pixelSizeY)*.5,t.rotationX,t.pixelSizeY};
    const qint64 pixels=static_cast<qint64>(metadata.width)*metadata.height;
    if(pixels>64000000) throw std::runtime_error("This example limits inputs to 64 million pixels.");
    labels.data=QByteArray(pixels*sizeof(qint32),char(0));
    AiSession session(package.modelPath(),request.inference.sessionOptions);
    labels.provider=session.provider();
    const auto columns=MaskRcnn::starts(metadata.width),rows=MaskRcnn::starts(metadata.height);
    const int total=columns.size()*rows.size();
    std::vector<MaskRcnn::Candidate> candidates;
    for(int top:rows) for(int left:columns) {
        if(cancelled()) return result;
        const int width=std::min(512,metadata.width-left),height=std::min(512,metadata.height-top);
        GdalRasterWindowRequest window;window.sourceX=left;window.sourceY=top;
        window.sourceWidth=width;window.sourceHeight=height;window.targetWidth=width;window.targetHeight=height;
        window.forcedOverviewLevel=-1;
        const QImage image=raster.readWindow(window,&error).convertToFormat(QImage::Format_RGB888);
        if(image.isNull()) throw std::runtime_error(error.toStdString());
        AiTensor input;input.name="image";input.dataType=AiTensorDataType::Float32;input.shape={1,3,512,512};
        input.data=QByteArray(3*512*512*sizeof(float),char(0));
        auto* values=reinterpret_cast<float*>(input.data.data());
        for(int y=0;y<height;++y){const uchar* row=image.constScanLine(y);
            for(int x=0;x<width;++x)for(int c=0;c<3;++c)values[c*512*512+y*512+x]=row[x*3+c]/255.F;}
        AiRunControl control;control.isCancellationRequested=request.inference.isCancellationRequested;
        AiInferenceResult predicted;
        try { predicted=session.run({input},control); }
        catch(const AiInferenceInterrupted&) { if(cancelled())return result;throw; }
        auto output=[&](const char* name)->const AiTensor& {
            for(const auto& tensor:predicted.outputs)if(tensor.name==name) return tensor;
            throw std::runtime_error(std::string("Missing output: ")+name);
        };
        const auto& boxes=output("boxes");const auto& scores=output("scores");
        const auto& classes=output("classes");const auto& masks=output("masks");
        const qint64 count=scores.shape.value(0,-1);
        if(count<0 || boxes.shape!=QVector<qint64>{count,4} || scores.shape!=QVector<qint64>{count} ||
           classes.shape!=QVector<qint64>{count} || masks.shape!=QVector<qint64>{count,1,512,512})
            throw std::runtime_error("Unexpected Mask R-CNN output shapes.");
        for(const auto* outputTensor:{&boxes,&scores,&classes,&masks})
            if(outputTensor->dataType!=AiTensorDataType::Float32 || (count>0 ? !outputTensor->validationErrors().isEmpty() : !outputTensor->data.isEmpty()))
                throw std::runtime_error("Invalid Mask R-CNN output tensor.");
        const auto* b=reinterpret_cast<const float*>(boxes.data.constData());
        const auto* score=reinterpret_cast<const float*>(scores.data.constData());
        const auto* cls=reinterpret_cast<const float*>(classes.data.constData());
        const auto* mask=reinterpret_cast<const float*>(masks.data.constData());
        for(qint64 i=0;i<count;++i) {
            if(!std::isfinite(score[i]) || score[i]<confidenceThreshold || cls[i]!=1.F) continue;
            if(!std::all_of(b+i*4,b+i*4+4,[](float v){return std::isfinite(v);})) continue;
            MaskRcnn::Candidate item;item.score=score[i];
            item.x1=left+std::clamp(double(b[i*4]),0.,double(width));
            item.y1=top+std::clamp(double(b[i*4+1]),0.,double(height));
            item.x2=left+std::clamp(double(b[i*4+2]),0.,double(width));
            item.y2=top+std::clamp(double(b[i*4+3]),0.,double(height));
            item.x=std::max(left,int(std::floor(item.x1))-1);item.y=std::max(top,int(std::floor(item.y1))-1);
            const int right=std::min(left+width,int(std::ceil(item.x2))+1),bottom=std::min(top+height,int(std::ceil(item.y2))+1);
            item.width=right-item.x;item.height=bottom-item.y;
            if(item.width<=0 || item.height<=0) continue;
            item.mask=QByteArray(item.width*item.height,char(0));int area=0;
            for(int y=0;y<item.height;++y)for(int x=0;x<item.width;++x) {
                const bool foreground=mask[i*512*512+(item.y-top+y)*512+item.x-left+x]>=.5F;
                item.mask[y*item.width+x]=foreground?1:0;area+=foreground;
            }
            if(area>=16)candidates.push_back(std::move(item));
        }
        ++labels.processedTiles;
        notify(labels.processedTiles*85/total,QString("Inference tile %1 / %2").arg(labels.processedTiles).arg(total));
    }
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.score>b.score;});
    std::vector<size_t> kept;
    for(size_t i=0;i<candidates.size();++i) {
        if(cancelled()) return result;
        bool duplicate=false;
        for(size_t previous:kept)if(MaskRcnn::iou(candidates[i],candidates[previous])>.5){duplicate=true;break;}
        if(!duplicate)kept.push_back(i);
    }
    notify(90,"Building instance polygons...");
    auto* canvas=reinterpret_cast<qint32*>(labels.data.data());
    for(size_t index:kept) {
        if(cancelled())return result;
        const auto& item=candidates[index];int available=0;
        for(int y=0;y<item.height;++y)for(int x=0;x<item.width;++x)
            if(item.mask[y*item.width+x] && canvas[(item.y+y)*metadata.width+item.x+x]==0)++available;
        if(available<16)continue;
        const auto id=static_cast<qint32>(++result.inference.instanceCount);
        for(int y=0;y<item.height;++y)for(int x=0;x<item.width;++x) {
            auto& pixel=canvas[(item.y+y)*metadata.width+item.x+x];
            if(item.mask[y*item.width+x] && pixel==0)pixel=id;
        }
    }
    if(cancelled())return result;
    FootprintOutput writer;
    result.polygonCount=writer.write(labels,request.outputPath,request.inference.isCancellationRequested);
    labels.elapsedMilliseconds=timer.elapsed();
    notify(100,"Inference complete");
    return result;
}
