#pragma once
#include "Sen2srRasterAdapter.h"
#include <array>
#include <QColor>
struct SrSample
{
    GeoKernel::AI::AiRasterInferenceResult raster;
    std::array<float, 3> low, high;
};
inline SrSample readSrSample(const QString &path)
{
    using namespace GeoKernel::AI;
    Sen2srGdalApi api;
    auto close = api.call<void (*)(void *)>("GDALClose");
    std::unique_ptr<void, void (*)(void *)> ds(
        api.call<void *(*)(const char *, int)>("GDALOpen")(path.toUtf8().constData(), 0), close);
    if (!ds)
        throw std::runtime_error("Cannot read Sentinel-2 sample.");
    SrSample sample;
    auto &r = sample.raster;
    r.width = api.call<int (*)(void *)>("GDALGetRasterXSize")(ds.get());
    r.height = api.call<int (*)(void *)>("GDALGetRasterYSize")(ds.get());
    if (r.width != 578 || r.height != 379 || api.call<int (*)(void *)>("GDALGetRasterCount")(ds.get()) != 8)
        throw std::runtime_error("Expected fixed578x379 eight-band FTW example.");
    r.geoTransform.resize(6);
    r.projectionWkt = QString::fromUtf8(api.call<const char *(*)(void *)>("GDALGetProjectionRef")(ds.get()));
    if (api.call<int (*)(void *, double *)>("GDALGetGeoTransform")(ds.get(), r.geoTransform.data()) ||
        r.projectionWkt.isEmpty())
        throw std::runtime_error("Missing sample georeferencing.");
    r.bandCount = 4;
    r.dataType = AiTensorDataType::Float32;
    r.data.resize(r.width * r.height * 4 * sizeof(float));
    r.provenance.sourcePath = path;
    const int pixels = r.width * r.height;
    for (int c = 0; c < 4; ++c)
    {
        void *b = api.call<void *(*)(void *, int)>("GDALGetRasterBand")(ds.get(), c + 1);
        float *v = reinterpret_cast<float *>(r.data.data()) + c * pixels;
        if (api.call<int (*)(void *, int, int, int, int, int, void *, int, int, int, int, int)>(
                "GDALRasterIO")(b, 0, 0, 0, r.width, r.height, v, r.width, r.height, 6, 0, 0))
            throw std::runtime_error("Cannot read raw RGBN.");
        int defined = 0;
        double nodata = api.call<double (*)(void *, int *)>("GDALGetRasterNoDataValue")(b, &defined);
        for (int i = 0; i < pixels; ++i)
            if (!std::isfinite(v[i]) || v[i] < 0 || (defined && v[i] == nodata))
                throw std::runtime_error("This fixed example requires valid pixels; external nodata/cloud "
                                         "handling needed for other scenes.");
        if (c < 3)
        {
            QVector<float> sorted(v, v + pixels);
            std::sort(sorted.begin(), sorted.end());
            sample.low[c] = sorted[pixels * 2 / 100];
            sample.high[c] = std::max(sample.low[c] + 1, sorted[pixels * 98 / 100]);
        }
    }
    return sample;
}
inline void writeSrDisplay(const GeoKernel::AI::AiRasterInferenceResult &r, const SrSample &stretch,
                           const QString &path, bool reflectanceOutput)
{
    Sen2srGdalApi api;
    auto close = api.call<void (*)(void *)>("GDALClose");
    auto driver = api.call<void *(*)(const char *)>("GDALGetDriverByName");
    std::unique_ptr<void, void (*)(void *)> out(
        api.call<void *(*)(void *, const char *, int, int, int, int, char **)>("GDALCreate")(
            driver("GTiff"), path.toUtf8().constData(), r.width, r.height, 4, 1, nullptr),
        close);
    auto affine = r.geoTransform;
    if (!out || api.call<int (*)(void *, double *)>("GDALSetGeoTransform")(out.get(), affine.data()) ||
        api.call<int (*)(void *, const char *)>("GDALSetProjection")(out.get(),
                                                                     r.projectionWkt.toUtf8().constData()))
        throw std::runtime_error("Cannot create display raster.");
    const int pixels = r.width * r.height;
    QByteArray bytes(pixels, char(255));
    const float *data = reinterpret_cast<const float *>(r.data.constData());
    for (int c = 0; c < 4; ++c)
    {
        if (c < 3)
            for (int i = 0; i < pixels; ++i)
            {
                float dn = data[c * pixels + i] * (reflectanceOutput ? 10000.f : 1.f);
                bytes[i] = char(qRound(
                    std::clamp((dn - stretch.low[c]) / (stretch.high[c] - stretch.low[c]), 0.f, 1.f) * 255));
            }
        else
            bytes.fill(char(255));
        void *b = api.call<void *(*)(void *, int)>("GDALGetRasterBand")(out.get(), c + 1);
        if (api.call<int (*)(void *, int)>("GDALSetRasterColorInterpretation")(b, c + 3) ||
            api.call<int (*)(void *, int, int, int, int, int, void *, int, int, int, int, int)>(
                "GDALRasterIO")(b, 1, 0, 0, r.width, r.height, bytes.data(), r.width, r.height, 1, 0, 0))
            throw std::runtime_error("Cannot write display colors.");
    }
}
