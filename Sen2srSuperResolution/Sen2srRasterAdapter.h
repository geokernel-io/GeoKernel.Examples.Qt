#pragma once
#include "GeoKernelAI.h"
#include <QElapsedTimer>
#include <QLibrary>
#include <QCoreApplication>
#include <QDir>
#include <memory>
#include <algorithm>
#include <cmath>
#include <stdexcept>

// Header-only application adapter for SDK1.5.36. Its single-scale convenience
// executor cannot stitch a128->512 graph. Uses only public native SDK APIs.
inline GeoKernel::AI::AiRasterInferenceResult sen2srUpscale(
    const QString &packageDirectory, const GeoKernel::AI::AiRasterInferenceResult &source,
    const GeoKernel::AI::AiSessionOptions &options = {}, std::function<bool()> cancel = {},
    GeoKernel::AI::AiRasterInferenceProgressCallback progress = {})
{
    using namespace GeoKernel::AI;
    if (source.width < 2 || source.height < 2 || source.width > 32768 || source.height > 32768 ||
        source.bandCount != 4 || source.dataType != AiTensorDataType::Float32 ||
        source.geoTransform.size() != 6 || source.projectionWkt.isEmpty() ||
        source.data.size() != qint64(source.width) * source.height * 4 * sizeof(float))
        throw std::invalid_argument("Expected georeferenced four-band Float32 raw-DN source.");
    const auto package = AiModelPackage::open(packageDirectory);
    if (!package.validationErrors().isEmpty() || package.manifest().id != "esaopensr-sen2srlite-rgbn-x4")
        throw std::invalid_argument("Wrong or invalid SEN2SRLite package.");
    QElapsedTimer timer;
    timer.start();
    AiRasterInferenceResult result;
    auto stopped = [&]
    {
        if (cancel && cancel())
        {
            result.cancelled = true;
            result.data.clear();
            return true;
        }
        return false;
    };
    if (stopped())
        return result;
    AiSession session(package.modelPath(), options);
    if (stopped())
        return result;
    result.width = source.width * 4;
    result.height = source.height * 4;
    result.bandCount = 4;
    result.dataType = AiTensorDataType::Float32;
    result.projectionWkt = source.projectionWkt;
    result.geoTransform = source.geoTransform;
    for (int i : {1, 2, 4, 5})
        result.geoTransform[i] /= 4.;
    const qint64 outputPixels = qint64(result.width) * result.height;
    if (outputPixels > qint64(512) * 1024 * 1024 / 16)
        throw std::invalid_argument("Output exceeds512MiB; stream larger scenes in the application.");
    result.data.resize(outputPixels * 4 * sizeof(float));
    result.provider = session.provider();
    auto *output = reinterpret_cast<float *>(result.data.data());
    const auto *input = reinterpret_cast<const float *>(source.data.constData());
    AiTensor tile;
    tile.name = "image";
    tile.shape = {1, 4, 128, 128};
    tile.dataType = AiTensorDataType::Float32;
    tile.data.resize(4 * 128 * 128 * sizeof(float));
    auto reflect = [](int i, int n)
    {
        while (i < 0 || i >= n)
        {
            if (i < 0)
                i = -i;
            else
                i = 2 * n - 2 - i;
        }
        return i;
    };
    const int total = ((source.width + 95) / 96) * ((source.height + 95) / 96);
    try
    {
        for (int y = 0; y < source.height; y += 96)
            for (int x = 0; x < source.width; x += 96)
            {
                if (stopped())
                    return result;
                auto *values = reinterpret_cast<float *>(tile.data.data());
                for (int c = 0; c < 4; ++c)
                    for (int row = 0; row < 128; ++row)
                        for (int col = 0; col < 128; ++col)
                        {
                            const float v =
                                input[(qint64(c) * source.height + reflect(y + row - 16, source.height)) *
                                          source.width +
                                      reflect(x + col - 16, source.width)];
                            if (!std::isfinite(v) || v < 0)
                                throw std::invalid_argument("Source must be finite nonnegative DN with "
                                                            "external nodata/cloud handling.");
                            values[(c * 128 + row) * 128 + col] = v;
                        }
                AiRunControl control;
                control.isCancellationRequested = cancel;
                const auto inference = session.run({tile}, control);
                if (stopped())
                    return result;
                if (inference.outputs.size() != 1 ||
                    inference.outputs[0].shape != QVector<qint64>{1, 4, 512, 512} ||
                    inference.outputs[0].data.size() != 4 * 512 * 512 * sizeof(float))
                    throw std::runtime_error("Invalid super-resolution output.");
                const auto *sr = reinterpret_cast<const float *>(inference.outputs[0].data.constData());
                for (int c = 0; c < 4; ++c)
                    for (int row = 0; row < std::min(96, source.height - y) * 4; ++row)
                        for (int col = 0; col < std::min(96, source.width - x) * 4; ++col)
                        {
                            const float v = sr[(c * 512 + row + 64) * 512 + col + 64];
                            if (!std::isfinite(v))
                                throw std::runtime_error("Nonfinite SR result.");
                            output[(qint64(c) * result.height + y * 4 + row) * result.width + x * 4 + col] =
                                v;
                        }
                ++result.processedTiles;
                if (progress)
                    progress({result.processedTiles, total, result.processedTiles * 100 / total,
                              "Super-resolving RGB+NIR..."});
            }
    }
    catch (...)
    {
        if (stopped())
            return result;
        throw;
    }
    result.elapsedMilliseconds = timer.elapsed();
    auto &p = result.provenance;
    const auto &m = package.manifest();
    p.modelId = m.id;
    p.modelVersion = m.version;
    p.modelRevision = m.provenance.revision;
    p.modelSha256 = m.modelSha256;
    p.modelPath = package.modelPath();
    p.sourcePath = source.provenance.sourcePath;
    p.sourceCrs = source.projectionWkt;
    p.provider = result.provider;
    p.runtimeVersion = AiSession::runtimeVersion();
    p.tiling = m.tiling;
    p.inputBands = {1, 2, 3, 4};
    p.outputMode = AiRasterOutputMode::FloatBands;
    p.outputWidth = result.width;
    p.outputHeight = result.height;
    p.outputBands = 4;
    p.outputDataType = AiTensorDataType::Float32;
    p.elapsedMilliseconds = result.elapsedMilliseconds;
    return result;
}

class Sen2srGdalApi
{
    QLibrary library;

  public:
    Sen2srGdalApi()
    {
#ifdef Q_OS_WIN
        library.setFileName(QDir(QCoreApplication::applicationDirPath()).filePath("gdal.dll"));
#else
        library.setFileName("gdal");
#endif
        library.setLoadHints(QLibrary::PreventUnloadHint);
        if (!library.load())
            throw std::runtime_error(library.errorString().toStdString());
        call<void (*)()>("GDALAllRegister")();
    }
    template <class T> T call(const char *name)
    {
        auto p = library.resolve(name);
        if (!p)
            throw std::runtime_error(name);
        return reinterpret_cast<T>(p);
    }
};
// Explicit RGB+NIR writer: the SDK generic four-band writer treats band4 as alpha.
// Here band4 remains NIR with undefined color interpretation and its own name.
inline void sen2srWriteGeoTiff(const GeoKernel::AI::AiRasterInferenceResult &r, const QString &path)
{
    using namespace GeoKernel::AI;
    if (r.cancelled || r.width <= 0 || r.height <= 0 || r.bandCount != 4 ||
        r.dataType != AiTensorDataType::Float32 || r.geoTransform.size() != 6 ||
        r.data.size() != qint64(r.width) * r.height * 4 * sizeof(float))
        throw std::invalid_argument("Invalid RGBN output.");
    Sen2srGdalApi api;
    auto close = api.call<void (*)(void *)>("GDALClose");
    auto driver = api.call<void *(*)(const char *)>("GDALGetDriverByName");
    char tiled[] = "TILED=YES", compressed[] = "COMPRESS=DEFLATE";
    char *options[] = {tiled, compressed, nullptr};
    std::unique_ptr<void, void (*)(void *)> out(
        api.call<void *(*)(void *, const char *, int, int, int, int, char **)>("GDALCreate")(
            driver("GTiff"), path.toUtf8().constData(), r.width, r.height, 4, 6, options),
        close);
    auto affine = r.geoTransform;
    if (!out || api.call<int (*)(void *, double *)>("GDALSetGeoTransform")(out.get(), affine.data()) ||
        api.call<int (*)(void *, const char *)>("GDALSetProjection")(out.get(),
                                                                     r.projectionWkt.toUtf8().constData()))
        throw std::runtime_error("Cannot create georeferenced SR output.");
    auto metadata =
        api.call<int (*)(void *, const char *, const char *, const char *)>("GDALSetMetadataItem");
    if (metadata(out.get(), "AI_GENERATED", "YES", nullptr) ||
        metadata(out.get(), "AI_MODEL_ID", "esaopensr-sen2srlite-rgbn-x4", nullptr) ||
        metadata(out.get(), "AI_OUTPUT_SCOPE",
                 "Estimated detail;2.5m output grid is not a2.5m sensor measurement", nullptr) ||
        metadata(out.get(), "GEOKERNEL_PROVENANCE", r.provenance.canonicalJson().toUtf8().constData(),
                 nullptr))
        throw std::runtime_error("Cannot write SR provenance.");
    const int colors[] = {3, 4, 5, 0};
    const char *names[] = {"B4 Red", "B3 Green", "B2 Blue", "B8 NIR"};
    for (int c = 0; c < 4; ++c)
    {
        void *band = api.call<void *(*)(void *, int)>("GDALGetRasterBand")(out.get(), c + 1);
        if (api.call<int (*)(void *, int)>("GDALSetRasterColorInterpretation")(band, colors[c]))
            throw std::runtime_error("Cannot set RGBN interpretation.");
        api.call<void (*)(void *, const char *)>("GDALSetDescription")(band, names[c]);
        if (api.call<int (*)(void *, int, int, int, int, int, void *, int, int, int, int, int)>(
                "GDALRasterIO")(band, 1, 0, 0, r.width, r.height,
                                const_cast<char *>(r.data.constData()) +
                                    qint64(c) * r.width * r.height * sizeof(float),
                                r.width, r.height, 6, 0, 0))
            throw std::runtime_error("Cannot write SR output band.");
    }
}
