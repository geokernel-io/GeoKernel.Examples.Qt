#pragma once

#include "TerrainSupport.h"
#include <Common/Gdal/GdalRasterProvider.h>
#include <GeoKernelAI.h>
#include <cstring>

using namespace GeoKernel::AI;

QRect comparisonWindow(const Scene& scene)
{
    const auto& dsm = *scene.dsm;
    if (scene.manifest.value("standard_full_tile").toBool())
        return QRect(200, 800, 500, 500);
    const int left = dsm.width / 5, top = dsm.height / 5;
    return QRect(left, top, dsm.width - 2 * left, dsm.height - 2 * top);
}
Score areaScore(const Raster& dsm, const Values& z, const Raster* reference, const QRect& area)
{
    Score m;
    if (!reference)
        return m;
    for (int y = area.top(); y < area.top() + area.height(); ++y)
        for (int x = area.left(); x < area.left() + area.width(); ++x)
        {
            const int i = y * dsm.width + x;
            if (!dsm.valid[i] || !reference->valid[i])
                continue;
            const double e = double(z[i]) - reference->z[i];
            ++m.count;
            m.rmse += e * e;
            m.mae += std::abs(e);
            m.bias += e;
        }
    if (m.count)
    {
        m.rmse = std::sqrt(m.rmse / m.count);
        m.mae /= m.count;
        m.bias /= m.count;
    }
    return m;
}
Raster cutRaster(const Raster& source, const QRect& rect)
{
    Raster output = source;
    output.width = rect.width();
    output.height = rect.height();
    output.transform[0] += rect.left() * source.pixel;
    output.transform[3] -= rect.top() * source.pixel;
    output.z.resize(output.width * output.height);
    output.valid.resize(output.z.size());
    for (int y = 0; y < output.height; ++y)
        for (int x = 0; x < output.width; ++x)
        {
            const int a = y * output.width + x, b = (rect.top() + y) * source.width + rect.left() + x;
            output.z[a] = source.z[b];
            output.valid[a] = source.valid[b];
        }
    return output;
}
Values cutValues(const Values& source, int sourceWidth, const QRect& rect)
{
    Values values(rect.width() * rect.height());
    for (int y = 0; y < rect.height(); ++y)
        std::copy_n(source.constData() + (rect.top() + y) * sourceWidth + rect.left(), rect.width(),
                    values.data() + y * rect.width());
    return values;
}
struct OrthoData
{
    QString path, sha256, contextPath;
    QImage image;
    int filledContextPixels = 0;
};
OrthoData readOrtho(const Raster& dsm, const QString& path, const QString& directory, std::atomic_bool& cancelled)
{
    checkpoint(cancelled);
    RasterRuntime runtime;
    auto open = runtime.function<void* (*)(const char*, int)>("GDALOpen");
    auto close = runtime.function<void (*)(void*)>("GDALClose");
    const auto bytes = path.toUtf8();
    std::unique_ptr<void, void (*)(void*)> dataset(open(bytes.constData(), 0), close);
    require(bool(dataset), "Cannot open RGB orthophoto.");
    require(runtime.function<int (*)(void*)>("GDALGetRasterCount")(dataset.get()) == 3,
            "Orthophoto must contain exactly three RGB Byte bands.");
    GdalRasterProvider provider;
    QString error;
    require(provider.open(path, &error), error);
    const auto m = provider.metadata();
    const auto& t = m.worldTransform;
    const QVector<double> transform = {t.upperLeftCenterX - t.pixelSizeX * .5, t.pixelSizeX, t.rotationY,
                                       t.upperLeftCenterY - t.pixelSizeY * .5, t.rotationX,  t.pixelSizeY};
    const auto a = CoordinateSystemFactory::fromUserInput(dsm.projection),
               b = CoordinateSystemFactory::fromUserInput(m.projectionWkt);
    require(
        a && b && CoordinateSystemFactory::areEquivalent(*a, *b) && m.hasWorldTransform && m.width == dsm.width &&
            m.height == dsm.height && transform == dsm.transform,
        "RGB and DSM must have the same dimensions, exact affine grid and equivalent CRS. Align them before loading.");
    auto band = runtime.function<void* (*)(void*, int)>("GDALGetRasterBand");
    QVector<uchar> valid(dsm.z.size(), 1), mask(dsm.z.size());
    for (int channel = 0; channel < 3; ++channel)
    {
        checkpoint(cancelled);
        auto* source = band(dataset.get(), channel + 1);
        require(runtime.function<int (*)(void*)>("GDALGetRasterDataType")(source) == 1 &&
                    runtime.function<int (*)(void*)>("GDALGetRasterColorInterpretation")(source) == channel + 3,
                "Use RGB-ordered Byte bands; no implicit spectral or color conversion is performed.");
        int present = 0;
        const double scale = runtime.function<double (*)(void*, int*)>("GDALGetRasterScale")(source, &present);
        require(!present || std::abs(scale - 1) < 1e-12, "Apply RGB scale before loading.");
        const double offset = runtime.function<double (*)(void*, int*)>("GDALGetRasterOffset")(source, &present);
        require(!present || std::abs(offset) < 1e-12, "Apply RGB offset before loading.");
        auto* maskBand = runtime.function<void* (*)(void*)>("GDALGetMaskBand")(source);
        require(
            runtime.function<int (*)(void*, int, int, int, int, int, void*, int, int, int, int, int)>("GDALRasterIO")(
                maskBand, 0, 0, 0, dsm.width, dsm.height, mask.data(), dsm.width, dsm.height, 1, 0, 0) == 0,
            "Cannot read RGB validity.");
        for (int i = 0; i < valid.size(); ++i)
            valid[i] = valid[i] && mask[i];
    }
    for (int i = 0; i < valid.size(); ++i)
        require(!dsm.valid[i] || valid[i],
                "RGB does not cover every valid DSM pixel. Crop or prepare common source coverage explicitly.");
    GdalRasterWindowRequest request;
    request.sourceWidth = request.targetWidth = dsm.width;
    request.sourceHeight = request.targetHeight = dsm.height;
    request.forcedOverviewLevel = -1;
    OrthoData output;
    output.path = QFileInfo(path).absoluteFilePath();
    output.contextPath = output.path;
    output.image = provider.readWindow(request, &error).convertToFormat(QImage::Format_RGB888);
    require(!output.image.isNull(), error);
    output.sha256 = QString::fromLatin1(hashFile(path));
    for (uchar value : valid)
        output.filledContextPixels += !value;
    if (output.filledContextPixels)
    {
        const auto nearest = supportMap(valid, dsm.width, dsm.height, cancelled);
        auto filled = output.image.copy();
        for (int i = 0; i < valid.size(); ++i)
            if (!valid[i])
                filled.setPixelColor(i % dsm.width, i / dsm.width,
                                     output.image.pixelColor(nearest[i] % dsm.width, nearest[i] / dsm.width));
        output.contextPath = QDir(directory).filePath("rgb-context.tif");
        runtime.write(dsm, output.contextPath, nullptr, 1, &filled);
    }
    checkpoint(cancelled);
    return output;
}
struct OrthoScene : Scene
{
    OrthoData rgb;
};
struct AiHeight
{
    Values raw, dtm;
    QJsonObject provenance;
    int tiles = 0, contextFilled = 0, clamped = 0;
};
AiHeight inferHeight(const Raster& dsm, const QString& modelDirectory, const QString& rgbContext, int overlap,
                     int threads, const QString& directory, std::atomic_bool& cancelled, const Progress& progress)
{
    const auto package = AiModelPackage::open(modelDirectory);
    const QString expected = rgbContext.isEmpty() ? "e400e27f6afc75bcf4e1a61432ee9557529683903996dd953d3e8e3fcbe90eb6"
                                                  : "5042d5abac64d6b1764265924c9910807e3a5efee7e1b9517fc9d86bf4d0e3a3";
    require(package.manifest().modelSha256 == expected, "Unexpected AI checkpoint for the declared input profile.");
    AiHeight output;
    QString context = dsm.path;
    for (uchar valid : dsm.valid)
        output.contextFilled += !valid;
    if (output.contextFilled)
    {
        const auto nearest = supportMap(dsm.valid, dsm.width, dsm.height, cancelled);
        auto filled = dsm.z;
        for (int i = 0; i < filled.size(); ++i)
            if (!dsm.valid[i])
                filled[i] = dsm.z[nearest[i]];
        context = QDir(directory).filePath(rgbContext.isEmpty() ? "pix-dsm-context.tif" : "rgb-dsm-context.tif");
        RasterRuntime runtime;
        runtime.write(dsm, context, filled.constData(), 6);
    }
    AiSessionOptions session;
    session.provider = AiExecutionProvider::CPU;
    session.intraOpThreads = threads;
    session.interOpThreads = 1;
    auto request = AiRasterInferenceExecutor::requestFromPackage(package, dsm.path, session);
    request.sourceBands = {{context, 1}};
    if (!rgbContext.isEmpty())
        for (int band = 1; band <= 3; ++band)
            request.sourceBands.push_back({rgbContext, band});
    request.preserveSourceMask = true;
    request.outputNoData = NoData;
    request.tiling.overlapX = request.tiling.overlapY = overlap;
    request.isCancellationRequested = [&cancelled]
    {
        return cancelled.load();
    };
    const auto result = AiRasterInferenceExecutor::run(
        request,
        [&](const AiRasterInferenceProgress& p)
        {
            progress(p.percent, QString("tiles %1 / %2 · %3").arg(p.completedTiles).arg(p.totalTiles).arg(p.message));
        });
    checkpoint(cancelled);
    require(!result.cancelled && result.width == dsm.width && result.height == dsm.height && result.bandCount == 1 &&
                result.dataType == AiTensorDataType::Float32 &&
                result.data.size() == dsm.z.size() * qint64(sizeof(float)),
            "AI result does not match the source grid/type.");
    const auto a = CoordinateSystemFactory::fromUserInput(dsm.projection),
               b = CoordinateSystemFactory::fromUserInput(result.projectionWkt);
    require(a && b && CoordinateSystemFactory::areEquivalent(*a, *b) && result.geoTransform == dsm.transform,
            "AI result changed the CRS or affine grid.");
    output.raw.resize(dsm.z.size());
    std::memcpy(output.raw.data(), result.data.constData(), size_t(result.data.size()));
    output.dtm = output.raw;
    for (int i = 0; i < dsm.z.size(); ++i)
    {
        if (!dsm.valid[i])
        {
            output.raw[i] = output.dtm[i] = NoData;
            continue;
        }
        require(std::isfinite(output.raw[i]) && output.raw[i] != NoData,
                "AI returned an invalid height on a valid DSM pixel.");
        if (output.dtm[i] > dsm.z[i])
        {
            output.dtm[i] = dsm.z[i];
            ++output.clamped;
        }
    }
    output.provenance = result.provenance.toJson();
    output.tiles = result.processedTiles;
    return output;
}
const std::array<QString, 3> MethodIds = {"classical", "pix_grad", "rgb_grad_fm"};
struct OrthoComparison
{
    Terrain classical;
    std::array<AiHeight, 2> ai;
    int overlap = 64, threads = 4;
    std::array<QString, 2> modelDirectories;
    std::array<QJsonObject, 2> modelManifests;
    std::array<std::array<QString, 3>, 3> maps;
    std::array<Score, 3> full, crop;
    QJsonObject files, report;
    QString directory;
    const Values& heights(int method) const
    {
        return method ? ai[method - 1].dtm : classical.dtm;
    }
};
void evaluateOrtho(const OrthoScene& scene, OrthoComparison& result, const std::array<QJsonObject, 2>& modelAssets)
{
    const auto& dsm = *scene.dsm;
    const auto rect = comparisonWindow(scene);
    QJsonObject full, crop, models;
    for (int method = 0; method < 3; ++method)
    {
        result.full[method] = score(dsm, result.heights(method), scene.reference.get());
        result.crop[method] = areaScore(dsm, result.heights(method), scene.reference.get(), rect);
        full.insert(MethodIds[method], result.full[method].json());
        crop.insert(MethodIds[method], result.crop[method].json());
    }
    crop.insert(
        "pixel_window",
        QJsonObject{{"column", rect.x()}, {"row", rect.y()}, {"width", rect.width()}, {"height", rect.height()}});
    for (int model = 0; model < 2; ++model)
        models.insert(MethodIds[model + 1], QJsonObject{{"archive_sha256", modelAssets[model].value("sha256")},
                                                        {"manifest", result.modelManifests[model]},
                                                        {"sdk_inference", result.ai[model].provenance},
                                                        {"context_filled_dsm_pixels", result.ai[model].contextFilled},
                                                        {"upper_bound_clamped_pixels", result.ai[model].clamped}});
    QJsonArray transform;
    for (double value : dsm.transform)
        transform.append(value);
    result.report = {
        {"schema_version", 1},
        {"experiment", "independent classical / DSM-only pix_grad / DSM+RGB rgb_grad_fm"},
        {"source_dsm", dsm.path},
        {"source_dsm_sha256", dsm.sha256},
        {"source_rgb", scene.rgb.path},
        {"source_rgb_sha256", scene.rgb.sha256},
        {"rgb_context_filled_pixels", scene.rgb.filledContextPixels},
        {"reference", scene.reference ? QJsonValue(scene.reference->path) : QJsonValue()},
        {"reference_sha256", scene.reference ? QJsonValue(scene.reference->sha256) : QJsonValue()},
        {"reference_used_for_processing", false},
        {"methods_feed_each_other", false},
        {"model_input_order", "pix_grad: DSM; rgb_grad_fm: DSM, R, G, B"},
        {"ai_options",
         QJsonObject{{"overlap_pixels", result.overlap}, {"cpu_threads", result.threads}, {"tile_pixels", 256}}},
        {"models", models},
        {"classical", QJsonObject{{"method", Method},
                                  {"parameters", result.classical.parameters.json()},
                                  {"summary", result.classical.summary}}},
        {"grid",
         QJsonObject{
             {"width", dsm.width}, {"height", dsm.height}, {"transform", transform}, {"crs_wkt", dsm.projection}}},
        {"evaluation", QJsonObject{{"full", full}, {"comparison_crop", crop}}},
        {"source_manifest", scene.manifest},
        {"output_files", result.files},
        {"attribution",
         "Federal Office of Topography swisstopo. AI weights: David Perez, Zenodo 7973800 / 7973886, CC BY 4.0; independent deterministic ONNX adaptations."},
        {"interpretation",
         "Different pretrained checkpoints/training objectives; observed differences do not isolate the causal contribution of RGB. Fixed development scene, not a held-out benchmark. Survey years and training GSD differ or remain unverified."}};
}
std::shared_ptr<OrthoComparison> compareOrtho(const OrthoScene& scene, const std::array<QString, 2>& modelDirectories,
                                              Parameters parameters, int overlap, int threads, const QString& directory,
                                              const std::array<QJsonObject, 2>& modelAssets,
                                              std::atomic_bool& cancelled, const Progress& progress)
{
    auto output = std::make_shared<OrthoComparison>();
    auto& result = *output;
    const auto& dsm = *scene.dsm;
    result.directory = directory;
    result.modelDirectories = modelDirectories;
    result.overlap = overlap;
    result.threads = threads;
    for (int model = 0; model < 2; ++model)
        result.modelManifests[model] = readJson(QDir(modelDirectories[model]).filePath("geokernel-model.json"));
    result.classical = estimate(dsm, parameters, cancelled,
                                [&](int p, const QString& text)
                                {
                                    progress(int(p * .24), "Classical: " + text);
                                });
    for (int model = 0; model < 2; ++model)
        result.ai[model] = inferHeight(dsm, modelDirectories[model], model ? scene.rgb.contextPath : QString(), overlap,
                                       threads, directory, cancelled,
                                       [&, model](int p, const QString& text)
                                       {
                                           progress(25 + model * 29 + int(p * .28), MethodIds[model + 1] + ": " + text);
                                       });
    RasterRuntime runtime;
    const auto numeric = [&](QString name, const Values& values)
    {
        checkpoint(cancelled);
        runtime.write(dsm, QDir(directory).filePath(name), values.constData(), 6);
        result.files.insert(name, name);
    };
    for (int method = 0; method < 3; ++method)
    {
        progress(84 + method * 3, "Preparing " + MethodIds[method] + " outputs...");
        Terrain display;
        display.dtm = result.heights(method);
        display.removed.resize(dsm.z.size());
        for (int i = 0; i < dsm.z.size(); ++i)
            display.removed[i] = dsm.valid[i] ? std::max(0.f, dsm.z[i] - display.dtm[i]) : NoData;
        numeric(MethodIds[method] + "-dtm.tif", display.dtm);
        numeric(MethodIds[method] + "-removed.tif", display.removed);
        if (method)
            numeric(MethodIds[method] + "-raw.tif", result.ai[method - 1].raw);
        for (int mode = 0; mode < 3; ++mode)
        {
            const auto image = mode == 0
                                   ? relief(dsm, display.dtm, cancelled)
                                   : diagnostic(dsm, display, mode == 1 ? 0 : 2, scene.reference.get(), cancelled);
            const auto file = QDir(directory).filePath(QString("view-%1-%2.tif").arg(method).arg(mode));
            runtime.write(dsm, file, nullptr, 1, &image);
            result.maps[method][mode] = file;
        }
    }
    runtime.write(dsm, QDir(directory).filePath("classical-qa.tif"), result.classical.quality.constData(), 1);
    result.files.insert("classical-qa.tif", "classical-qa.tif");
    progress(96, "Writing identical source-pixel comparison crops...");
    const auto rect = comparisonWindow(scene);
    const auto raster = cutRaster(dsm, rect);
    const auto cut = [&](QString name, const Values& values)
    {
        const auto pixels = cutValues(values, dsm.width, rect);
        runtime.write(raster, QDir(directory).filePath(name), pixels.constData(), 6);
        result.files.insert(name, name);
    };
    cut("comparison-dsm.tif", dsm.z);
    for (int method = 0; method < 3; ++method)
        cut("comparison-" + MethodIds[method] + ".tif", result.heights(method));
    if (scene.reference)
        cut("comparison-reference.tif", scene.reference->z);
    auto rgb = scene.rgb.image.copy(rect);
    runtime.write(raster, QDir(directory).filePath("comparison-rgb.tif"), nullptr, 1, &rgb);
    result.files.insert("comparison-rgb.tif", "comparison-rgb.tif");
    evaluateOrtho(scene, result, modelAssets);
    checkpoint(cancelled);
    return output;
}
struct OrthoOutcome
{
    std::shared_ptr<OrthoScene> scene;
    std::shared_ptr<OrthoComparison> comparison;
    QString error, message;
};
