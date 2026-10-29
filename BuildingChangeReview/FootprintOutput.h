#pragma once
#include <QLibrary>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <memory>
#include <vector>
#include <atomic>
#include <cmath>
#include <QFileInfo>
#include <QTemporaryDir>
#include "Common/Gdal/GdalRasterProvider.h"
#include "GeoKernelAI.h"
#include "GeoKernelAnalysis.h"
#include "Serialization/Wkb/GisWkbReader.h"
#include "CoordinateSystems/CoordinateSystemFactory.h"

// Use the GDAL runtime already shipped by the SDK through its stable C ABI.
// Four-connected instance polygonization preserves source IDs and the raster CRS.
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

inline DemoInferenceResult runDemoInference(
    const GeoKernel::Analysis::AiInstanceVectorizationRequest& request,
    GeoKernel::Analysis::AnalysisProgressCallback progress = {}, double foregroundThreshold = -1) {
    DemoInferenceResult result;
    using namespace GeoKernel::AI;
    const auto package=AiModelPackage::open(request.inference.modelPackagePath);
    const auto errors=package.validationErrors();
    if(!errors.isEmpty()) throw std::runtime_error(errors.join(" ").toStdString());
    const auto& manifest=package.manifest();
    AiRasterInferenceRequest raster;
    raster.modelPath=package.modelPath();raster.modelId=manifest.id;raster.modelVersion=manifest.version;
    raster.modelRevision=manifest.provenance.revision;raster.modelSha256=manifest.modelSha256;
    raster.rasterPath=request.inference.rasterPath;raster.tiling=manifest.tiling;raster.normalization=manifest.normalization;
    raster.sessionOptions=request.inference.sessionOptions;raster.isCancellationRequested=request.inference.isCancellationRequested;
    raster.inputName=manifest.inputs[0].name;raster.bands=manifest.inputAdapters[0].bands;
    auto decoded=AiRasterInferenceExecutor::runDecodedOutputs(raster,manifest.outputDecoders,
        [progress](const GeoKernel::AI::AiRasterInferenceProgress& p) {
            if (progress) {
                GeoKernel::Analysis::AnalysisProgress state;
                state.percent=p.percent*9/10; state.message=p.message; progress(state);
            }
        });
    if(decoded.isEmpty()) throw std::runtime_error("No model outputs were produced.");
    result.inference.decodedOutputs=decoded;result.inference.labels=decoded[0].raster;
    if(result.inference.labels.cancelled) return result;
    auto tensor=[](const AiDecodedRasterOutput& output) {
        AiTensor tensor;tensor.dataType=AiTensorDataType::Float32;
        tensor.shape={1,1,output.raster.height,output.raster.width};tensor.data=output.raster.data;return tensor;
    };
    if(decoded.size()!=3) throw std::runtime_error("HOTOSM requires mask, boundary and distance outputs.");
    const auto foreground=tensor(decoded[0]),boundary=tensor(decoded[1]),distance=tensor(decoded[2]);
    AiInstanceSeparationOptions separation;
    separation.foregroundThreshold=foregroundThreshold>=0?foregroundThreshold:manifest.outputDecoders[0].threshold;
    separation.boundaryThreshold=.5;separation.seedThreshold=.1;
    separation.minimumRegionPixels=28;separation.connectivity=4;
    auto separated=AiInstanceSeparator::separate(foreground,&boundary,&distance,separation);
    result.inference.instanceCount=separated.instanceCount;
    result.inference.labels.bandCount=1;result.inference.labels.dataType=AiTensorDataType::Int32;
    result.inference.labels.data=separated.labels.data;
    if (result.inference.labels.cancelled ||
        (request.inference.isCancellationRequested && request.inference.isCancellationRequested())) return result;
    FootprintOutput writer;
    result.polygonCount=writer.write(result.inference.labels,request.outputPath,request.inference.isCancellationRequested);
    return result;
}
