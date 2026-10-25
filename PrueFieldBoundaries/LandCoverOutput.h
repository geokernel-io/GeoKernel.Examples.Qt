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
#include <QColor>
#include <array>
#include <QElapsedTimer>
#include <algorithm>
#include <QTemporaryDir>
#include "Common/Gdal/GdalRasterProvider.h"
#include "GeoKernelAI.h"
#include "Serialization/Wkb/GisWkbReader.h"
#include "CoordinateSystems/CoordinateSystemFactory.h"

// Use the GDAL runtime already shipped by the SDK through its stable C ABI.
// The adapter writes class polygons and exact Byte RGBA preview colors.
// No external GDAL headers, import libraries, installation or Python is needed.
class LandCoverOutput {
    QLibrary library;
    template<class T> T resolve(const char* name) {
        auto address = library.resolve(name);
        if (!address) throw std::runtime_error(QString("Missing GDAL function: %1").arg(name).toStdString());
        return reinterpret_cast<T>(address);
    }
public:
    LandCoverOutput() {
#ifdef Q_OS_WIN
        library.setFileName(QDir(QCoreApplication::applicationDirPath()).filePath("gdal.dll"));
#else
        library.setFileName("gdal");
#endif
        library.setLoadHints(QLibrary::PreventUnloadHint);
        if (!library.load()) throw std::runtime_error(library.errorString().toStdString());
        resolve<void(*)()>("GDALAllRegister")();
    }
    void writePreview(const GeoKernel::AI::AiRasterInferenceResult& classes,const QString& path,
                      const std::array<const char*,3>& colors) {
        auto driver=resolve<void*(*)(const char*)>("GDALGetDriverByName");
        auto create=resolve<void*(*)(void*,const char*,int,int,int,int,char**)>("GDALCreate");
        auto close=resolve<void(*)(void*)>("GDALClose");
        auto band=resolve<void*(*)(void*,int)>("GDALGetRasterBand");
        auto write=resolve<int(*)(void*,int,int,int,int,int,void*,int,int,int,int,int)>("GDALRasterIO");
        auto transform=resolve<int(*)(void*,double*)>("GDALSetGeoTransform");
        auto projection=resolve<int(*)(void*,const char*)>("GDALSetProjection");
        auto color=resolve<int(*)(void*,int)>("GDALSetRasterColorInterpretation");
        using Handle=std::unique_ptr<void,void(*)(void*)>;
        Handle output(create(driver("GTiff"),path.toUtf8().constData(),classes.width,classes.height,4,1,nullptr),close);
        if(!output || classes.geoTransform.size()!=6)throw std::runtime_error("Could not create classification preview.");
        auto affine=classes.geoTransform;
        if(transform(output.get(),affine.data()) || projection(output.get(),classes.projectionWkt.toUtf8().constData()))
            throw std::runtime_error("Could not georeference classification preview.");
        std::array<QColor,3> palette;for(int i=0;i<3;++i)palette[i]=QColor(colors[i]);
        const qint64 pixels=static_cast<qint64>(classes.width)*classes.height;
        const auto* values=reinterpret_cast<const qint32*>(classes.data.constData());
        QByteArray channel(pixels,char(0));
        for(int c=0;c<4;++c){
            for(qint64 i=0;i<pixels;++i){const int id=values[i];
                channel[i]=(id<1 || id>2)?0:char(c==3?255:c==0?palette[id].red():c==1?palette[id].green():palette[id].blue());}
            void* b=band(output.get(),c+1);
            if(color(b,c+3) || write(b,1,0,0,classes.width,classes.height,channel.data(),classes.width,classes.height,1,0,0))
                throw std::runtime_error("Could not write classification colors.");
        }
    }
    void saveShapefile(const QByteArray& geoJson, const QString& path) {
        QTemporaryDir temporary;
        if (!temporary.isValid()) throw std::runtime_error("Could not create export workspace.");
        QFile input(temporary.filePath("land-covers.geojson"));
        if (!input.open(QIODevice::WriteOnly) || input.write(geoJson)!=geoJson.size())
            throw std::runtime_error("Could not prepare land-covers for export.");
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
        for (qint64 i=0;i<pixels;++i) foreground[i]=values[i]==1 ? char(255) : char(0);
        if (rasterIO(getBand(mask.get(),1),1,0,0,labels.width,labels.height,foreground.data(),labels.width,labels.height,1,0,0)!=0)
            throw std::runtime_error("Could not write polygon selection mask.");
        Handle srs(newSrs(labels.projectionWkt.toUtf8().constData()),destroySrs);
        // Output closes before its spatial reference (reverse declaration order).
        QFile::remove(outputPath);
        Handle output(create(getDriver("GeoJSON"),outputPath.toUtf8().constData(),0,0,0,0,nullptr),close);
        if (!output) throw std::runtime_error("Could not create land-cover GeoJSON.");
        void* layer=createLayer(output.get(),"landcover",srs.get(),3,nullptr);
        if (!layer) throw std::runtime_error("Could not create land-cover layer.");
        Handle field(createField("class_id",12),destroyField);
        if (!field || addField(layer,field.get(),1)!=0) throw std::runtime_error("Could not create land-cover ID field.");
        auto callback=[](double,const char*,void* context)->int {
            const auto& check=*static_cast<const std::function<bool()>*>(context);
            return (!check || !check()) ? 1 : 0;
        };
        if (polygonize(getBand(source.get(),1),getBand(mask.get(),1),layer,0,nullptr,callback,const_cast<std::function<bool()>*>(&cancel))!=0)
            throw std::runtime_error("Land-cover polygonization failed or was cancelled.");
        // GeoJSON's write-only layer does not report its buffered feature count.
        // Close/flush it, then count from the completed dataset.
        output.reset();
        Handle completed(openVector(outputPath.toUtf8().constData(),4,nullptr,nullptr,nullptr),close);
        if (!completed) throw std::runtime_error("Could not reopen completed land-cover GeoJSON.");
        void* completedLayer=getLayer(completed.get(),0);
        if (!completedLayer) throw std::runtime_error("Completed land-cover layer is missing.");
        return featureCount(completedLayer,1);
    }
};

