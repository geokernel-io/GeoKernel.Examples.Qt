#pragma once
#include "LandCoverOutput.h"
#include <QPointF>

// Stable GDAL C API supplied by the SDK; no additional GDAL installation.
struct FloodRaster {
    int width=0,height=0;
    QVector<double> affine;
    QString crs;
    QVector<QVector<float>> bands;
    QVector<uchar> valid;
};
class FloodRasterIO {
    QLibrary library;
    template<class T>T fn(const char* name){auto p=library.resolve(name);if(!p)throw std::runtime_error(name);return reinterpret_cast<T>(p);}
public:
    FloodRasterIO(){
#ifdef Q_OS_WIN
        library.setFileName(QDir(QCoreApplication::applicationDirPath()).filePath("gdal.dll"));
#else
        library.setFileName("gdal");
#endif
        library.setLoadHints(QLibrary::PreventUnloadHint);if(!library.load())throw std::runtime_error(library.errorString().toStdString());fn<void(*)()>("GDALAllRegister")();
    }
    FloodRaster read(const QString& path){
        auto close=fn<void(*)(void*)>("GDALClose");
        std::unique_ptr<void,void(*)(void*)> ds(fn<void*(*)(const char*,int)>("GDALOpen")(path.toUtf8().constData(),0),close);
        if(!ds)throw std::runtime_error("Cannot read flood sample.");
        FloodRaster r;r.width=fn<int(*)(void*)>("GDALGetRasterXSize")(ds.get());r.height=fn<int(*)(void*)>("GDALGetRasterYSize")(ds.get());
        if(r.width!=512 || r.height!=512 || fn<int(*)(void*)>("GDALGetRasterCount")(ds.get())!=13)throw std::runtime_error("Expected the fixed 512px / 13-band flood sample.");
        r.affine.resize(6);if(fn<int(*)(void*,double*)>("GDALGetGeoTransform")(ds.get(),r.affine.data()))throw std::runtime_error("Missing sample georeferencing.");
        r.crs=QString::fromUtf8(fn<const char*(*)(void*)>("GDALGetProjectionRef")(ds.get()));if(r.crs.isEmpty())throw std::runtime_error("Missing sample CRS.");
        const int pixels=r.width*r.height;r.valid.fill(255,pixels);r.bands.resize(6);
        const int mapping[]={2,3,4,9,12,13};
        for(int c=0;c<6;++c){void* b=fn<void*(*)(void*,int)>("GDALGetRasterBand")(ds.get(),mapping[c]);r.bands[c].resize(pixels);
            if(fn<int(*)(void*,int,int,int,int,int,void*,int,int,int,int,int)>("GDALRasterIO")(b,0,0,0,r.width,r.height,r.bands[c].data(),r.width,r.height,6,0,0))throw std::runtime_error("Cannot read source bands.");
            int hasNoData=0;const double nodata=fn<double(*)(void*,int*)>("GDALGetRasterNoDataValue")(b,&hasNoData);
            for(int i=0;i<pixels;++i){float& v=r.bands[c][i];if(!std::isfinite(v) || v<0 || (hasNoData && v==nodata)){r.valid[i]=0;v=0;}}
        }
        return r;
    }
    QPointF location(const FloodRaster& r,double column,double row){
        auto destroy=fn<void(*)(void*)>("OSRDestroySpatialReference");using Handle=std::unique_ptr<void,void(*)(void*)>;
        auto create=fn<void*(*)(const char*)>("OSRNewSpatialReference");Handle source(create(r.crs.toUtf8().constData()),destroy),target(create(nullptr),destroy);
        if(!source || !target || fn<int(*)(void*,int)>("OSRImportFromEPSG")(target.get(),4326))throw std::runtime_error("Cannot create geographic CRS.");
        auto axis=fn<void(*)(void*,int)>("OSRSetAxisMappingStrategy");axis(source.get(),0);axis(target.get(),0);
        Handle transform(fn<void*(*)(void*,void*)>("OCTNewCoordinateTransformation")(source.get(),target.get()),fn<void(*)(void*)>("OCTDestroyCoordinateTransformation"));
        double x=r.affine[0]+column*r.affine[1]+row*r.affine[2],y=r.affine[3]+column*r.affine[4]+row*r.affine[5],z=0;
        if(!transform || !fn<int(*)(void*,int,double*,double*,double*)>("OCTTransform")(transform.get(),1,&x,&y,&z) || !std::isfinite(x) || !std::isfinite(y) || std::abs(y)>90 || std::abs(x)>180)throw std::runtime_error("Cannot transform tile location to WGS84.");
        return {y,x}; // model explicitly expects latitude, longitude
    }
    void rgb(const FloodRaster& r,const QString& path){
        auto close=fn<void(*)(void*)>("GDALClose");auto driver=fn<void*(*)(const char*)>("GDALGetDriverByName");
        std::unique_ptr<void,void(*)(void*)> ds(fn<void*(*)(void*,const char*,int,int,int,int,char**)>("GDALCreate")(driver("GTiff"),path.toUtf8().constData(),r.width,r.height,4,1,nullptr),close);
        auto affine=r.affine;if(!ds || fn<int(*)(void*,double*)>("GDALSetGeoTransform")(ds.get(),affine.data()) || fn<int(*)(void*,const char*)>("GDALSetProjection")(ds.get(),r.crs.toUtf8().constData()))throw std::runtime_error("Cannot create RGB preview.");
        const int pixels=r.width*r.height;QByteArray bytes(pixels,0);const int channels[]={2,1,0};
        for(int c=0;c<4;++c){
            float low=0,high=1;
            if(c<3){QVector<float> values;for(int i=0;i<pixels;++i)if(r.valid[i])values.append(r.bands[channels[c]][i]);if(values.isEmpty())throw std::runtime_error("No valid source pixels.");std::sort(values.begin(),values.end());low=values[values.size()*2/100];high=std::max(low+1,values[values.size()*98/100]);}
            for(int i=0;i<pixels;++i)bytes[i]=c==3?char(r.valid[i]):char(qRound(std::clamp((r.bands[channels[c]][i]-low)/(high-low),0.f,1.f)*255));
            void* b=fn<void*(*)(void*,int)>("GDALGetRasterBand")(ds.get(),c+1);
            if(fn<int(*)(void*,int)>("GDALSetRasterColorInterpretation")(b,c+3) || fn<int(*)(void*,int,int,int,int,int,void*,int,int,int,int,int)>("GDALRasterIO")(b,1,0,0,r.width,r.height,bytes.data(),r.width,r.height,1,0,0))throw std::runtime_error("Cannot write RGB preview.");
        }
    }
};
