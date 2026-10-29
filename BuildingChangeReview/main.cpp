#include "Common/Gdal/GdalRasterProvider.h"
#include "CoordinateSystems/CoordinateSystemFactory.h"
#include "CoordinateSystems/CoordinateTransformer.h"
#include "GeoKernelAI.h"
#include "GeoKernelAnalysis.h"
#include "Raster/Tiff/GisLayerTIFF.h"
#include "Serialization/Wkb/GisWkbReader.h"
#include "Shapes/GisShapePolygon.h"
#include "Symbology/GisSymbolRenderer.h"
#include "Viewer/GisViewer.h"
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHash>
#include <QHeaderView>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLibrary>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

using GeoKernel::AI::AiRasterInferenceResult;
using GeoKernel::AI::AiTensorDataType;
using Cancel = std::function<bool()>;

inline void checkpoint(const Cancel& cancel)
{
    if (cancel && cancel())
        throw std::runtime_error("Analysis cancelled.");
}

struct MatchOptions
{
    double minimumAreaM2 = 50.;
    double maximumOverlap = .10;
    double toleranceM = 2.;
    double minimumBuildingScore = .75;
    double maximumOtherScore = .20;
    double weakBuildingThreshold = .25;
};

struct InstanceMatch
{
    qint64 pixels = 0;
    qint64 overlappingPixels = 0;
    bool touchesEdge = false;
    double buildingScoreSum = 0;
    double otherScoreSum = 0;
    bool hasScores = false;

    double overlap() const
    {
        return pixels ? double(overlappingPixels) / double(pixels) : 0.;
    }

    double buildingScore() const
    {
        return hasScores && pixels ? buildingScoreSum / double(pixels) : -1.;
    }

    double otherScore() const
    {
        return hasScores && pixels ? otherScoreSum / double(pixels) : -1.;
    }
};

struct Matches
{
    QHash<qint32, InstanceMatch> before;
    QHash<qint32, InstanceMatch> after;
    double pixelAreaM2 = 0;
    qint64 edgeInstances = 0;
};

inline void validateGrid(const AiRasterInferenceResult& raster)
{
    const qint64 count = qint64(raster.width) * raster.height;

    if (raster.width <= 0 || raster.height <= 0 || count > 16 * 1024 * 1024 || raster.bandCount != 1 ||
        raster.dataType != AiTensorDataType::Int32 || raster.data.size() != count * qint64(sizeof(qint32)) ||
        raster.geoTransform.size() != 6 || raster.projectionWkt.isEmpty() || raster.cancelled)
        throw std::invalid_argument("Expected a complete georeferenced single-band Int32 instance grid.");
    
    const auto& t = raster.geoTransform;
    
    for (double value : t)
        if (!std::isfinite(value))
            throw std::invalid_argument("Nonfinite raster transform.");
    
    if (t[1] <= 0 || t[5] >= 0 || t[2] != 0 || t[4] != 0)
        throw std::invalid_argument("The comparison requires north-up grids in a shared metric CRS.");
}

inline std::vector<unsigned char> expandedForeground(const AiRasterInferenceResult& raster, double toleranceM, const Cancel& cancel, const float* scores = nullptr, double threshold = .25)
{
    const int width = raster.width, height = raster.height;
    const double radiusX = std::ceil(toleranceM / raster.geoTransform[1]);
    const double radiusY = std::ceil(toleranceM / -raster.geoTransform[5]);

    if (!std::isfinite(radiusX) || !std::isfinite(radiusY) || radiusX > 100 || radiusY > 100)
        throw std::invalid_argument("Matching tolerance exceeds 100 pixels.");

    const int rx = int(radiusX), ry = int(radiusY);
    const auto* values = reinterpret_cast<const qint32*>(raster.data.constData());
    const auto present = [&](int index) {
        return scores ? scores[index] >= threshold : values[index] > 0;
    };

    std::vector<unsigned char> horizontal(size_t(width) * height), output(horizontal.size());

    for (int y = 0; y < height; ++y)
    {
        checkpoint(cancel);
        int count = 0;
        for (int x = 0; x < std::min(width, rx + 1); ++x)
            count += present(y * width + x);
        for (int x = 0; x < width; ++x)
        {
            horizontal[size_t(y) * width + x] = count > 0;
            if (x - rx >= 0)
                count -= present(y * width + x - rx);
            if (x + rx + 1 < width)
                count += present(y * width + x + rx + 1);
        }
    }

    for (int x = 0; x < width; ++x)
    {
        checkpoint(cancel);

        int count = 0;
        for (int y = 0; y < std::min(height, ry + 1); ++y)
            count += horizontal[size_t(y) * width + x];
        for (int y = 0; y < height; ++y)
        {
            output[size_t(y) * width + x] = count > 0;
            if (y - ry >= 0)
                count -= horizontal[size_t(y - ry) * width + x];
            if (y + ry + 1 < height)
                count += horizontal[size_t(y + ry + 1) * width + x];
        }
    }

    return output;
}

inline Matches compare(const AiRasterInferenceResult& before, const AiRasterInferenceResult& after, const MatchOptions& options, const Cancel& cancel = {}, const AiRasterInferenceResult* beforeScores = nullptr, const AiRasterInferenceResult* afterScores = nullptr)
{
    validateGrid(before);
    validateGrid(after);

    if (before.width != after.width || before.height != after.height || before.geoTransform != after.geoTransform ||
        before.projectionWkt != after.projectionWkt)
        throw std::invalid_argument("Two-date instance grids must have identical CRS, extent and pixel grid.");
    
    if (!std::isfinite(options.minimumAreaM2) || options.minimumAreaM2 <= 0 || !std::isfinite(options.maximumOverlap) ||
        options.maximumOverlap < 0 || options.maximumOverlap > 1 || !std::isfinite(options.toleranceM) ||
        options.toleranceM < 0 || options.toleranceM > 5 || !std::isfinite(options.minimumBuildingScore) ||
        options.minimumBuildingScore < 0 || options.minimumBuildingScore > 1 ||
        !std::isfinite(options.maximumOtherScore) || options.maximumOtherScore < 0 || options.maximumOtherScore > 1 ||
        !std::isfinite(options.weakBuildingThreshold) || options.weakBuildingThreshold < 0 ||
        options.weakBuildingThreshold > 1)
        throw std::invalid_argument("Invalid area, overlap or tolerance parameters.");
    
    if ((beforeScores == nullptr) != (afterScores == nullptr))
        throw std::invalid_argument("Both raw score grids are required together.");
    
    const auto validateScores = [&](const AiRasterInferenceResult* scores) -> const float* {
    
        if (!scores)
            return nullptr;
        
        const qint64 pixels = qint64(before.width) * before.height;
        
        if (scores->dataType != AiTensorDataType::Float32 || scores->bandCount != 1 || scores->width != before.width ||
            scores->height != before.height || scores->geoTransform != before.geoTransform ||
            scores->projectionWkt != before.projectionWkt || scores->data.size() != pixels * qint64(sizeof(float)) ||
            scores->cancelled)
            throw std::invalid_argument("Raw building score grids must match the instance grid.");

        const auto* data = reinterpret_cast<const float*>(scores->data.constData());

        for (qint64 i = 0; i < pixels; ++i)
        {
            if (i % before.width == 0)
                checkpoint(cancel);
            if (!std::isfinite(data[i]) || data[i] < 0 || data[i] > 1)
                throw std::invalid_argument("Invalid building score; expected finite values between 0 and 1.");
        }

        return data;
    };

    const auto* aScores = validateScores(beforeScores);
    const auto* bScores = validateScores(afterScores);
    const auto a = expandedForeground(before, options.toleranceM, cancel, aScores, options.weakBuildingThreshold);
    const auto b = expandedForeground(after, options.toleranceM, cancel, bScores, options.weakBuildingThreshold);

    Matches result;
    result.pixelAreaM2 = before.geoTransform[1] * -before.geoTransform[5];

    if (!std::isfinite(result.pixelAreaM2) || result.pixelAreaM2 <= 0)
        throw std::invalid_argument("Invalid source pixel area.");

    const auto collect = [&](const AiRasterInferenceResult& raster, const std::vector<unsigned char>& other, QHash<qint32, InstanceMatch>& stats, const float* ownScores, const float* otherScores) {
    
        const auto* labels = reinterpret_cast<const qint32*>(raster.data.constData());
        
        for (int y = 0; y < raster.height; ++y)
        {
            checkpoint(cancel);

            for (int x = 0; x < raster.width; ++x)
            {
                const auto i = size_t(y) * raster.width + x;
                const qint32 id = labels[i];
                if (id < 0)
                    throw std::invalid_argument("Instance labels must be nonnegative.");
                if (!id)
                    continue;
                auto& stat = stats[id];
                ++stat.pixels;
                stat.overlappingPixels += other[i] != 0;
                if (ownScores)
                {
                    stat.hasScores = true;
                    stat.buildingScoreSum += ownScores[i];
                    stat.otherScoreSum += otherScores[i];
                }
                stat.touchesEdge |= x == 0 || y == 0 || x == raster.width - 1 || y == raster.height - 1;
            }
        }
        for (const auto& stat : stats)
            result.edgeInstances += stat.touchesEdge;
    };

    collect(before, b, result.before, aScores, bScores);
    collect(after, a, result.after, bScores, aScores);

    return result;
}

inline bool isCandidate(const InstanceMatch& instance, double pixelArea, const MatchOptions& options)
{
    return !instance.touchesEdge && instance.pixels * pixelArea >= options.minimumAreaM2 &&
           instance.overlap() < options.maximumOverlap &&
           (!instance.hasScores || (instance.buildingScore() >= options.minimumBuildingScore &&
                                    instance.otherScore() <= options.maximumOtherScore));
}

inline constexpr auto DatasetUrl = "https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/"
                                   "almere_oosterwold_change_2016_2025_v1.zip";
inline constexpr auto DatasetArchive = "almere_oosterwold_change_2016_2025_v1.zip";
inline constexpr auto DatasetSha256 = "ecc35ef2c6bbdd586adc6ecc812ce1c74aee62fbf8e404de1d63b75bf7137a88";
inline constexpr auto DatasetCache = "oosterwold-2016-2025";
inline constexpr int BeforeYear = 2016, AfterYear = 2025;
inline constexpr auto BeforeFile = "oosterwold_2016-epsg25831-50cm.tif";
inline constexpr auto AfterFile = "oosterwold_2025-epsg25831-50cm.tif";
inline constexpr auto BeforeSha256 = "db723783db4c8738a716fe8b898de2e3099bab9394e335bd489db2412ae0c5c3";
inline constexpr auto AfterSha256 = "7b1b12d8b27cd0714f140415004daf9fd0000495eda31366c7cd4aab218caa82";
inline constexpr double DatasetBounds[] = {657533, 5800382, 658557, 5801406};
inline constexpr auto ModelUrl = "https://pub-00d214f25f754884b22349c19ac37bbf.r2.dev/geokernel-models/"
                                 "hotosm-dinov3s-buildings-geokernel-onnx-v1.zip";
inline constexpr auto ModelArchive = "hotosm-dinov3s-buildings-geokernel-onnx-v1.zip";
inline constexpr auto ModelSha256 = "3a6cd11e013f81b46b3db700feddef2d98319780285b8320e1200573684b24bb";
inline constexpr auto ModelId = "hotosm-dinov3s-buildings";
inline constexpr auto Attribution =
    "Imagery: Beeldmateriaal Nederland / PDOK, 2016 / 2025, CC BY 4.0. WMS JPEG crop/reprojection rendered at 0.5 m "
    "and converted to GeoTIFF. Model: HOTOSM / Meta DINOv3; see model package licenses.";

class FootprintOutput
{
    QLibrary library;
    template <class T> T resolve(const char* name)
    {
        auto address = library.resolve(name);
        if (!address)
            throw std::runtime_error(QString("Missing GDAL function: %1").arg(name).toStdString());
        return reinterpret_cast<T>(address);
    }

  public:

    FootprintOutput()
    {
#ifdef Q_OS_WIN
        library.setFileName(QDir(QCoreApplication::applicationDirPath()).filePath("gdal.dll"));
#else
        library.setFileName("gdal");
#endif
        library.setLoadHints(QLibrary::PreventUnloadHint);
        if (!library.load())
            throw std::runtime_error(library.errorString().toStdString());
    }

    std::unique_ptr<GeoKernel::Core::Layers::GisLayerVector> loadOverlay(const QString& path, const QString& projectionWkt)
    {
        using namespace GeoKernel::Core;
        auto open =
            resolve<void* (*)(const char*, unsigned int, const char* const*, const char* const*, const char* const*)>(
                "GDALOpenEx");
        auto close = resolve<void (*)(void*)>("GDALClose");
        auto getLayer = resolve<void* (*)(void*, int)>("GDALDatasetGetLayer");
        auto next = resolve<void* (*)(void*)>("OGR_L_GetNextFeature");
        auto destroyFeature = resolve<void (*)(void*)>("OGR_F_Destroy");
        auto getGeometry = resolve<void* (*)(void*)>("OGR_F_GetGeometryRef");
        auto getId = resolve<qint64 (*)(void*, int)>("OGR_F_GetFieldAsInteger64");
        auto wkbSize = resolve<int (*)(void*)>("OGR_G_WkbSize");
        auto exportWkb = resolve<int (*)(void*, int, unsigned char*)>("OGR_G_ExportToWkb");
        using Handle = std::unique_ptr<void, void (*)(void*)>;

        Handle dataset(open(path.toUtf8().constData(), 4, nullptr, nullptr, nullptr), close);

        if (!dataset)
            throw std::runtime_error("Could not open footprint output for display.");

        void* source = getLayer(dataset.get(), 0);

        if (!source)
            throw std::runtime_error("Footprint output has no layer.");

        auto layer = Layers::GisLayerVector::createInMemory("Building prediction polygons", Shapes::GisShapeType::Polygon);
        layer->setCoordinateSystem(CoordinateSystems::CoordinateSystemFactory::fromUserInput(projectionWkt));
        layer->addAttributeDefinition({"instance_id", Layers::Defs::GisAttributeType::Integer, 0, 0});
        
        while (void* raw = next(source))
        {
            Handle feature(raw, destroyFeature);
            void* geometry = getGeometry(raw);

            if (!geometry)
                continue;

            QByteArray wkb(wkbSize(geometry), Qt::Uninitialized);

            if (exportWkb(geometry, 1, reinterpret_cast<unsigned char*>(wkb.data())) != 0)
                throw std::runtime_error("Could not read footprint geometry for display.");

            auto shape = Serialization::Wkb::GisWkbReader::read(wkb);
            if (!shape)
                throw std::runtime_error("Footprint geometry was not decoded.");

            shape->attributes().insert("instance_id", getId(raw, 0));
            if (!layer->addShape(std::move(shape)))
                throw std::runtime_error("Could not add footprint geometry to overlay.");
        }

        return layer;
    }

    qint64 write(const GeoKernel::AI::AiRasterInferenceResult& labels, const QString& outputPath, const std::function<bool()>& cancel)
    {
        auto getDriver = resolve<void* (*)(const char*)>("GDALGetDriverByName");
        auto setTransform = resolve<int (*)(void*, double*)>("GDALSetGeoTransform");
        auto close = resolve<void (*)(void*)>("GDALClose");
        auto create = resolve<void* (*)(void*, const char*, int, int, int, int, char**)>("GDALCreate");
        auto getBand = resolve<void* (*)(void*, int)>("GDALGetRasterBand");
        auto rasterIO = resolve<int (*)(void*, int, int, int, int, int, void*, int, int, int, int, int)>("GDALRasterIO");
        auto newSrs = resolve<void* (*)(const char*)>("OSRNewSpatialReference");
        auto destroySrs = resolve<void (*)(void*)>("OSRDestroySpatialReference");
        auto createLayer = resolve<void* (*)(void*, const char*, void*, int, char**)>("GDALDatasetCreateLayer");
        auto createField = resolve<void* (*)(const char*, int)>("OGR_Fld_Create");
        auto destroyField = resolve<void (*)(void*)>("OGR_Fld_Destroy");
        auto addField = resolve<int (*)(void*, void*, int)>("OGR_L_CreateField");
        auto polygonize = resolve<int (*)(void*, void*, void*, int, char**, int (*)(double, const char*, void*), void*)>("GDALPolygonize");
        auto featureCount = resolve<qint64 (*)(void*, int)>("OGR_L_GetFeatureCount");
        auto openVector = resolve<void* (*)(const char*, unsigned int, const char* const*, const char* const*, const char* const*)>("GDALOpenEx");
        auto getLayer = resolve<void* (*)(void*, int)>("GDALDatasetGetLayer");
        using Handle = std::unique_ptr<void, void (*)(void*)>;
        
        Handle source(create(getDriver("MEM"), "", labels.width, labels.height, 1, 5, nullptr), close);
        Handle mask(create(getDriver("MEM"), "", labels.width, labels.height, 1, 1, nullptr), close);
        
        if (!source || !mask)
            throw std::runtime_error("Could not allocate polygonization buffers.");
        
        const qint64 pixels = static_cast<qint64>(labels.width) * labels.height;
        
        if (labels.dataType != GeoKernel::AI::AiTensorDataType::Int32 || labels.data.size() != pixels * sizeof(qint32))
            throw std::runtime_error("Expected a single-band Int32 label raster.");
        
        if (labels.geoTransform.size() != 6)
            throw std::runtime_error("Missing raster geotransform.");

        auto transform = labels.geoTransform;

        if (setTransform(source.get(), transform.data()) != 0 ||
            rasterIO(getBand(source.get(), 1), 1, 0, 0, labels.width, labels.height,
                     const_cast<char*>(labels.data.constData()), labels.width, labels.height, 5, 0, 0) != 0)
            throw std::runtime_error("Could not prepare in-memory polygonization source.");

        QByteArray foreground(pixels, Qt::Uninitialized);
        const auto* values = reinterpret_cast<const qint32*>(labels.data.constData());

        for (qint64 i = 0; i < pixels; ++i)
            foreground[i] = values[i] > 0 ? char(255) : char(0);

        if (rasterIO(getBand(mask.get(), 1), 1, 0, 0, labels.width, labels.height, foreground.data(), labels.width, labels.height, 1, 0, 0) != 0)
            throw std::runtime_error("Could not write polygon selection mask.");
        
        Handle srs(newSrs(labels.projectionWkt.toUtf8().constData()), destroySrs);
        
        QFile::remove(outputPath);
        Handle output(create(getDriver("GeoJSON"), outputPath.toUtf8().constData(), 0, 0, 0, 0, nullptr), close);
        if (!output)
            throw std::runtime_error("Could not create footprint GeoJSON.");

        void* layer = createLayer(output.get(), "buildings", srs.get(), 3, nullptr);
        if (!layer)
            throw std::runtime_error("Could not create footprint layer.");
        
        Handle field(createField("instance_id", 12), destroyField);
        if (!field || addField(layer, field.get(), 1) != 0)
            throw std::runtime_error("Could not create footprint ID field.");
        
        auto callback = [](double, const char*, void* context) -> int {
            const auto& check = *static_cast<const std::function<bool()>*>(context);
            return (!check || !check()) ? 1 : 0;
        };
        
        if (polygonize(getBand(source.get(), 1), getBand(mask.get(), 1), layer, 0, nullptr, callback, const_cast<std::function<bool()>*>(&cancel)) != 0)
            throw std::runtime_error("Footprint polygonization failed or was cancelled.");
        
        output.reset();
        Handle completed(openVector(outputPath.toUtf8().constData(), 4, nullptr, nullptr, nullptr), close);
        if (!completed)
            throw std::runtime_error("Could not reopen completed footprint GeoJSON.");
        
        void* completedLayer = getLayer(completed.get(), 0);        
        if (!completedLayer)
            throw std::runtime_error("Completed footprint layer is missing.");

        return featureCount(completedLayer, 1);
    }
};

struct DemoInferenceResult
{
    GeoKernel::AI::AiInstanceSegmentationResult inference;
    qint64 polygonCount = 0;
};

inline DemoInferenceResult runDemoInference(const GeoKernel::Analysis::AiInstanceVectorizationRequest& request, GeoKernel::Analysis::AnalysisProgressCallback progress = {}, double foregroundThreshold = -1)
{
    DemoInferenceResult result;
    using namespace GeoKernel::AI;
    const auto package = AiModelPackage::open(request.inference.modelPackagePath);
    const auto errors = package.validationErrors();

    if (!errors.isEmpty())
        throw std::runtime_error(errors.join(" ").toStdString());

    const auto& manifest = package.manifest();
    AiRasterInferenceRequest raster;
    raster.modelPath = package.modelPath();
    raster.modelId = manifest.id;
    raster.modelVersion = manifest.version;
    raster.modelRevision = manifest.provenance.revision;
    raster.modelSha256 = manifest.modelSha256;
    raster.rasterPath = request.inference.rasterPath;
    raster.tiling = manifest.tiling;
    raster.normalization = manifest.normalization;
    raster.sessionOptions = request.inference.sessionOptions;
    raster.isCancellationRequested = request.inference.isCancellationRequested;
    raster.inputName = manifest.inputs[0].name;
    raster.bands = manifest.inputAdapters[0].bands;
    auto decoded = AiRasterInferenceExecutor::runDecodedOutputs(
        raster, manifest.outputDecoders, [progress](const GeoKernel::AI::AiRasterInferenceProgress& p) {
            if (progress)
            {
                GeoKernel::Analysis::AnalysisProgress state;
                state.percent = p.percent * 9 / 10;
                state.message = p.message;
                progress(state);
            }
        });

    if (decoded.isEmpty())
        throw std::runtime_error("No model outputs were produced.");

    result.inference.decodedOutputs = decoded;
    result.inference.labels = decoded[0].raster;

    if (result.inference.labels.cancelled)
        return result;

    auto tensor = [](const AiDecodedRasterOutput& output) {
        AiTensor tensor;
        tensor.dataType = AiTensorDataType::Float32;
        tensor.shape = {1, 1, output.raster.height, output.raster.width};
        tensor.data = output.raster.data;
        return tensor;
    };

    if (decoded.size() != 3)
        throw std::runtime_error("HOTOSM requires mask, boundary and distance outputs.");

    const auto foreground = tensor(decoded[0]), boundary = tensor(decoded[1]), distance = tensor(decoded[2]);
    
    AiInstanceSeparationOptions separation;
    separation.foregroundThreshold = foregroundThreshold >= 0 ? foregroundThreshold : manifest.outputDecoders[0].threshold;
    separation.boundaryThreshold = .5;
    separation.seedThreshold = .1;
    separation.minimumRegionPixels = 28;
    separation.connectivity = 4;
    auto separated = AiInstanceSeparator::separate(foreground, &boundary, &distance, separation);
    result.inference.instanceCount = separated.instanceCount;
    result.inference.labels.bandCount = 1;
    result.inference.labels.dataType = AiTensorDataType::Int32;
    result.inference.labels.data = separated.labels.data;

    if (result.inference.labels.cancelled ||
        (request.inference.isCancellationRequested && request.inference.isCancellationRequested()))
        return result;
    FootprintOutput writer;
    result.polygonCount = writer.write(result.inference.labels, request.outputPath, request.inference.isCancellationRequested);
    return result;
}

class GeometrySupport
{
    QLibrary library;
    template <class T> T get(const char* name)
    {
        const auto address = library.resolve(name);
        if (!address)
            throw std::runtime_error(QString("Missing runtime function: %1").arg(name).toStdString());
        return reinterpret_cast<T>(address);
    }

  public:

    GeometrySupport()
    {
#ifdef Q_OS_WIN
        library.setFileName(QDir(QCoreApplication::applicationDirPath()).filePath("gdal.dll"));
#else
        library.setFileName("gdal");
#endif
        library.setLoadHints(QLibrary::PreventUnloadHint);
        if (!library.load())
            throw std::runtime_error(library.errorString().toStdString());
    }

    double area(const GeoKernel::Core::Shapes::GisShape& shape)
    {
        const auto bytes = shape.toWkb();
        void* raw = nullptr;
        const auto create = get<int (*)(const void*, void*, void**, int)>("OGR_G_CreateFromWkb");
        const auto destroy = get<void (*)(void*)>("OGR_G_DestroyGeometry");

        if (create(bytes.constData(), nullptr, &raw, bytes.size()) != 0 || !raw)
            throw std::runtime_error("Could not decode candidate polygon.");

        std::unique_ptr<void, void (*)(void*)> geometry(raw, destroy);
        if (!get<int (*)(void*)>("OGR_G_IsValid")(raw))
            throw std::runtime_error("Candidate polygon is invalid. Correct its vertices before approval.");

        const double value = get<double (*)(void*)>("OGR_G_Area")(raw);

        if (!std::isfinite(value) || value <= 0)
            throw std::runtime_error("Candidate polygon has no valid area.");
        return value;
    }

    QJsonObject geographicGeometry(const GeoKernel::Core::Shapes::GisShape& shape)
    {
        using namespace GeoKernel::Core;
        const auto* polygon = dynamic_cast<const Shapes::GisShapePolygon*>(&shape);
        if (!polygon)
            throw std::runtime_error("A polygon is required for export.");
        const auto source = CoordinateSystems::CoordinateSystemFactory::fromEpsg(25831);
        const auto target = CoordinateSystems::CoordinateSystemFactory::fromEpsg(4326);
        CoordinateSystems::CoordinateTransformer transformer(*source, *target);
        Shapes::GisShapePolygon transformed;
        
        for (const auto& part : polygon->parts())
        {
            QVector<Shapes::GisShapePoint> ring;
            for (const auto& point : part)
                ring.push_back(transformer.transform(point));
            transformed.parts().push_back(std::move(ring));
        }
        
        const auto json = QJsonDocument::fromJson(transformed.toGeoJson().toUtf8());
        
        if (!json.isObject())
            throw std::runtime_error("Could not encode exported geometry.");
        
        return json.object();
    }
};

inline QString prepareInferenceRaster(const QString& input, const QString& output, int maximumSide, const GeoKernel::Core::Shapes::GisExtent& region = {})
{
    using namespace GeoKernel::Formats::Common::Gdal;
    using namespace GeoKernel::AI;
    GdalRasterProvider provider;
    QString error;
    if (!provider.open(input, &error))
        throw std::runtime_error(error.toStdString());
    const auto metadata = provider.metadata();
    if (!metadata.hasWorldTransform || metadata.bandCount < 3)
        throw std::runtime_error("The demo requires a georeferenced RGB raster.");
    int left = 0, top = 0, sourceWidth = metadata.width, sourceHeight = metadata.height;
    const auto& t = metadata.worldTransform;
    const double originX = t.upperLeftCenterX - (t.pixelSizeX + t.rotationY) * .5;
    const double originY = t.upperLeftCenterY - (t.rotationX + t.pixelSizeY) * .5;

    if (!region.isEmpty())
    {
        const double determinant = t.pixelSizeX * t.pixelSizeY - t.rotationX * t.rotationY;
        if (determinant == 0)
            throw std::runtime_error("Raster transform is singular.");
        double minX = metadata.width, minY = metadata.height, maxX = 0, maxY = 0;
        for (double x : {region.xMin(), region.xMax()})
            for (double y : {region.yMin(), region.yMax()})
            {
                const double dx = x - originX, dy = y - originY;
                const double px = (dx * t.pixelSizeY - dy * t.rotationY) / determinant;
                const double py = (dy * t.pixelSizeX - dx * t.rotationX) / determinant;
                minX = std::min(minX, px);
                maxX = std::max(maxX, px);
                minY = std::min(minY, py);
                maxY = std::max(maxY, py);
            }
        left = std::clamp(static_cast<int>(std::floor(minX)), 0, metadata.width);
        top = std::clamp(static_cast<int>(std::floor(minY)), 0, metadata.height);
        sourceWidth = std::clamp(static_cast<int>(std::ceil(maxX)), 0, metadata.width) - left;
        sourceHeight = std::clamp(static_cast<int>(std::ceil(maxY)), 0, metadata.height) - top;
        if (sourceWidth <= 0 || sourceHeight <= 0)
            throw std::runtime_error("The current view does not intersect the raster.");
    }

    if (region.isEmpty() && metadata.width <= maximumSide && metadata.height <= maximumSide)
        return input;

    const double scale = std::min(1., static_cast<double>(maximumSide) / std::max(sourceWidth, sourceHeight));
    const int width = std::max(1, qRound(sourceWidth * scale));
    const int height = std::max(1, qRound(sourceHeight * scale));
    
    GdalRasterWindowRequest request;
    request.sourceX = left;
    request.sourceY = top;
    request.sourceWidth = sourceWidth;
    request.sourceHeight = sourceHeight;
    request.targetWidth = width;
    request.targetHeight = height;
    
    const QImage image = provider.readWindow(request, &error).convertToFormat(QImage::Format_RGB888);
    if (image.isNull())
        throw std::runtime_error(error.toStdString());
    
    AiRasterInferenceResult raster;
    raster.width = width;
    raster.height = height;
    raster.bandCount = 3;
    raster.dataType = AiTensorDataType::Float32;
    const qint64 plane = static_cast<qint64>(width) * height;
    raster.data.resize(plane * 3 * sizeof(float));
    float* values = reinterpret_cast<float*>(raster.data.data());    
    for (int y = 0; y < height; ++y)
    {
        const uchar* row = image.constScanLine(y);
        for (int x = 0; x < width; ++x)
            for (int c = 0; c < 3; ++c)
                values[c * plane + y * width + x] = row[x * 3 + c];
    }
    const double sx = static_cast<double>(sourceWidth) / width;
    const double sy = static_cast<double>(sourceHeight) / height;
    raster.geoTransform = {originX + left * t.pixelSizeX + top * t.rotationY, t.pixelSizeX * sx, t.rotationY * sy, originY + left * t.rotationX + top * t.pixelSizeY, t.rotationX * sx,  t.pixelSizeY * sy};
    raster.projectionWkt = metadata.projectionWkt;
    AiRasterInferenceExecutor::writeGeoTiff(raster, output);
    return output;
}

struct Candidate
{
    QString id;
    QString kind;
    QString review = "pending";
    QString note;
    QString reviewedBy;
    qint64 instanceId = 0;
    double overlap = 0;
    double areaM2 = 0;
    double buildingScore = -1, otherScore = -1;
    QByteArray reviewedGeometry;
    std::unique_ptr<GeoKernel::Core::Shapes::GisShape> geometry;
};

struct JobResult
{
    QString error;
    bool cancelled = false;
    std::vector<Candidate> candidates;
    QJsonObject provenance;
    int beforeInstances = 0, afterInstances = 0;
    qint64 edgeInstances = 0, invalidPolygons = 0;
    qint64 uncertainInstances = 0;
    QJsonArray comparisonEvidence;
    qint64 elapsedMilliseconds = 0;
};

struct JobOptions
{
    QString before, after, model;
    GeoKernel::Core::Shapes::GisExtent region;
    GeoKernel::AI::AiExecutionProvider provider = GeoKernel::AI::AiExecutionProvider::Auto;
    double foregroundThreshold = .4371;
    MatchOptions matching;
};

using Progress = std::function<void(int, const QString&)>;

inline GeoKernel::Core::Shapes::GisExtent demoRegion()
{
    return {657885, 5800734, 658205, 5801054};
}

inline GeoKernel::Core::Shapes::GisExtent datasetRegion()
{
    return {DatasetBounds[0], DatasetBounds[1], DatasetBounds[2], DatasetBounds[3]};
}

inline void runAnalysis(const JobOptions& options, JobResult& result, const Cancel& cancel, const Progress& progress)
{
    using namespace GeoKernel::AI;
    using namespace GeoKernel::Analysis;
    QElapsedTimer timer;
    timer.start();
    QTemporaryDir workspace;
    if (!workspace.isValid())
        throw std::runtime_error("Could not create analysis workspace.");
    const auto package = AiModelPackage::open(options.model);
    const auto errors = package.validationErrors();
    if (!errors.isEmpty())
        throw std::runtime_error(errors.join("\n").toStdString());
    const auto manifest = package.manifest();
    if (manifest.id != ModelId)
        throw std::runtime_error("The HOTOSM DINOv3-S building package is required.");
    checkpoint(cancel);
    progress(1, "Preparing the same analysis extent for both dates...");
    const QString before =
        prepareInferenceRaster(options.before, workspace.filePath("before.tif"), 2048, options.region);
    const QString after = prepareInferenceRaster(options.after, workspace.filePath("after.tif"), 2048, options.region);
    checkpoint(cancel);
    const auto infer = [&](const QString& raster, const QString& output, int start, const QString& year) {
        AiInstanceVectorizationRequest request;
        request.inference.modelPackagePath = options.model;
        request.inference.rasterPath = raster;
        request.inference.sessionOptions.provider = options.provider;
        request.inference.sessionOptions.intraOpThreads = 2;
        request.inference.sessionOptions.interOpThreads = 1;
        request.inference.isCancellationRequested = cancel;
        request.outputPath = output;
        return runDemoInference(
            request,
            [&](const AnalysisProgress& state) {
                progress(start + std::clamp(state.percent, 0, 100) * 43 / 100, year + ": " + state.message);
            },
            options.foregroundThreshold);
    };
    const QString beforePolygons = workspace.filePath("before.geojson");
    const QString afterPolygons = workspace.filePath("after.geojson");
    auto a = infer(before, beforePolygons, 3, QString::number(BeforeYear));
    checkpoint(cancel);
    auto b = infer(after, afterPolygons, 47, QString::number(AfterYear));
    checkpoint(cancel);
    progress(91, "Comparing building instances across dates...");

    GeoKernel::Formats::Common::Gdal::GdalRasterProvider source;
    QString error;
    if (!source.open(before, &error) || source.metadata().epsgCode != 25831)
        throw std::runtime_error("This example requires the pinned EPSG:25831 imagery.");
    const auto matches = compare(a.inference.labels, b.inference.labels, options.matching, cancel,
                                 &a.inference.decodedOutputs[0].raster, &b.inference.decodedOutputs[0].raster);
    result.beforeInstances = matches.before.size();
    result.afterInstances = matches.after.size();
    result.edgeInstances = matches.edgeInstances;
    const auto recordEvidence = [&](const QHash<qint32, InstanceMatch>& stats, const QString& kind) {
        for (auto it = stats.cbegin(); it != stats.cend(); ++it)
        {
            const auto& stat = it.value();
            if (stat.touchesEdge || stat.pixels * matches.pixelAreaM2 < options.matching.minimumAreaM2)
                continue;
            const bool selected = isCandidate(stat, matches.pixelAreaM2, options.matching);
            result.uncertainInstances += !selected;
            result.comparisonEvidence.append(QJsonObject{{"kind", kind},
                                                         {"instance_id", it.key()},
                                                         {"area_m2", stat.pixels * matches.pixelAreaM2},
                                                         {"coverage", stat.overlap()},
                                                         {"building_score", stat.buildingScore()},
                                                         {"other_date_score", stat.otherScore()},
                                                         {"selected", selected}});
        }
    };
    recordEvidence(matches.before, "removed");
    recordEvidence(matches.after, "new");
    FootprintOutput reader;
    GeometrySupport geometry;
    const auto append = [&](const QString& path, const QHash<qint32, InstanceMatch>& stats, const QString& kind) {
        auto layer = reader.loadOverlay(path, a.inference.labels.projectionWkt);
        for (const auto* shape : layer->items())
        {
            checkpoint(cancel);
            const qint32 instanceId = shape->attributes().value("instance_id").toInt();
            const auto found = stats.constFind(instanceId);
            if (found == stats.cend() || !isCandidate(found.value(), matches.pixelAreaM2, options.matching))
                continue;
            double area = 0;
            try
            {
                area = geometry.area(*shape);
            }
            catch (const std::exception&)
            {
                ++result.invalidPolygons;
                continue;
            }
            if (area < options.matching.minimumAreaM2)
                continue;
            Candidate item;
            item.id = QString("%1-%2").arg(kind).arg(result.candidates.size() + 1, 4, 10, QChar('0'));
            item.kind = kind;
            item.instanceId = instanceId;
            item.overlap = found->overlap();
            item.areaM2 = area;
            item.geometry = shape->clone();
            item.buildingScore = found->buildingScore();
            item.otherScore = found->otherScore();
            result.candidates.push_back(std::move(item));
        }
    };
    append(afterPolygons, matches.after, "new");
    append(beforePolygons, matches.before, "removed");
    result.elapsedMilliseconds = timer.elapsed();
    QJsonArray transform;
    for (const auto value : a.inference.labels.geoTransform)
        transform.append(value);
    result.provenance = {{"created_at_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                         {"before_year", BeforeYear},
                         {"after_year", AfterYear},
                         {"source_crs", "EPSG:25831"},
                         {"before_sha256", BeforeSha256},
                         {"after_sha256", AfterSha256},
                         {"dataset_url", DatasetUrl},
                         {"dataset_archive_sha256", DatasetSha256},
                         {"model_id", manifest.id},
                         {"model_version", manifest.version},
                         {"model_sha256", manifest.modelSha256},
                         {"model_archive_sha256", ModelSha256},
                         {"execution_provider", aiExecutionProviderName(a.inference.labels.provider)},
                         {"foreground_threshold", options.foregroundThreshold},
                         {"minimum_area_m2", options.matching.minimumAreaM2},
                         {"maximum_other_date_coverage", options.matching.maximumOverlap},
                         {"matching_tolerance_m", options.matching.toleranceM},
                         {"minimum_building_score", options.matching.minimumBuildingScore},
                         {"maximum_other_date_score", options.matching.maximumOtherScore},
                         {"weak_building_threshold", options.matching.weakBuildingThreshold},
                         {"analysis_width", a.inference.labels.width},
                         {"analysis_height", a.inference.labels.height},
                         {"analysis_geotransform", transform},
                         {"elapsed_ms", result.elapsedMilliseconds},
                         {"edge_instances_excluded", result.edgeInstances},
                         {"invalid_polygons_excluded", result.invalidPolygons},
                         {"uncertain_or_matched_instances_excluded", result.uncertainInstances},
                         {"attribution", Attribution},
                         {"imagery_license", "https://creativecommons.org/licenses/by/4.0/"},
                         {"method", "Independent building predictions; strong own-date mean score, low other-date mean "
                                    "score and low buffered weak-score coverage propose new/removed candidates. Raw "
                                    "scores are not calibrated change probabilities. No image registration."}};
    checkpoint(cancel);
    progress(100, "Candidate analysis complete. Review before exporting.");
}

inline bool updateGeometry(Candidate& item, const GeoKernel::Core::Shapes::GisShape& shape)
{
    if (shape.toWkb() == item.geometry->toWkb())
        return false;
    item.geometry = shape.clone();
    item.review = "pending";
    item.reviewedGeometry.clear();
    item.reviewedBy.clear();
    item.note = "Geometry edited; review again.";

    try
    {
        GeometrySupport geometry;
        item.areaM2 = geometry.area(*item.geometry);
    }
    catch (const std::exception&)
    {
        item.areaM2 = 0;
    }

    return true;
}

inline void reviewCandidate(Candidate& item, const QString& decision, const QString& actor, const QString& note)
{
    if (actor.trimmed().isEmpty())
        throw std::invalid_argument("A reviewer name is required.");
    if (decision != "approved" && decision != "rejected" && decision != "pending")
        throw std::invalid_argument("Unknown review decision.");
    if (decision == "approved")
    {
        GeometrySupport geometry;
        item.areaM2 = geometry.area(*item.geometry);
    }
    item.review = decision;
    item.note = note.trimmed();
    item.reviewedBy = actor.trimmed();
    item.reviewedGeometry = decision == "approved" ? item.geometry->toWkb() : QByteArray{};
}

inline QJsonObject approvedCollection(const JobResult& result, const QJsonArray& audit)
{
    GeometrySupport geometry;
    QJsonArray features;
    for (const auto& item : result.candidates)
    {
        if (item.review != "approved")
            continue;
        if (item.reviewedGeometry != item.geometry->toWkb())
            throw std::runtime_error("An approved geometry changed; review it again.");
        const double area = geometry.area(*item.geometry);
        features.append(QJsonObject{{"type", "Feature"},
                                    {"id", item.id},
                                    {"geometry", geometry.geographicGeometry(*item.geometry)},
                                    {"properties", QJsonObject{{"candidate_id", item.id},
                                                               {"change_type", item.kind},
                                                               {"review_status", item.review},
                                                               {"review_note", item.note},
                                                               {"reviewed_by", item.reviewedBy},
                                                               {"area_m2", area},
                                                               {"source_instance_id", item.instanceId},
                                                               {"other_date_coverage", item.overlap},
                                                               {"building_score", item.buildingScore},
                                                               {"other_date_score", item.otherScore},
                                                               {"before_year", BeforeYear},
                                                               {"after_year", AfterYear},
                                                               {"model_id", result.provenance["model_id"]}}}});
    }
    return {{"type", "FeatureCollection"},
            {"features", features},
            {"geokernel_provenance", result.provenance},
            {"geokernel_review_audit", audit},
            {"coordinate_note",
             "GeoJSON coordinates are WGS84 longitude/latitude. area_m2 is measured in source EPSG:25831."}};
}

inline void sampleError(QWidget* parent, const QString& title, const QString& message)
{
    QMessageBox::critical(parent, title, message);
}

inline QString sampleDataPath()
{
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("data"));
}

inline bool downloadFile(const QUrl& url, const QString& targetPath, QWidget* parent)
{
    QDir().mkpath(QFileInfo(targetPath).absolutePath());
    const QString partialPath = targetPath + QStringLiteral(".part");
    QFile partialFile(partialPath);
    if (!partialFile.open(QIODevice::WriteOnly))
        return false;

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    request.setTransferTimeout(60000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GeoKernel Project"));
    QNetworkReply* reply = manager.get(request);
    QProgressDialog progress(QStringLiteral("Downloading building-change sample assets..."), QStringLiteral("Cancel"),
                             0, 0, parent);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::readyRead, [&] {
        partialFile.write(reply->readAll());
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, [&](qint64 received, qint64 total) {
        if (total > 0)
        {
            progress.setRange(0, 100);
            progress.setValue(static_cast<int>((received * 100) / total));
        }
    });
    QObject::connect(&progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    deadline.start(120000);
    loop.exec();
    deadline.stop();

    partialFile.write(reply->readAll());
    partialFile.close();
    const bool succeeded = reply->error() == QNetworkReply::NoError;
    const QString error = reply->errorString();
    reply->deleteLater();
    if (!succeeded)
    {
        QFile::remove(partialPath);
        sampleError(parent, QStringLiteral("BuildingChangeReview"),
                    QStringLiteral("Sample download failed:\n%1").arg(error));
        return false;
    }
    QFile::remove(targetPath);
    return QFile::rename(partialPath, targetPath);
}

inline QString findFile(const QString& folder, const QString& fileName)
{
    QDirIterator iterator(folder, QStringList() << fileName, QDir::Files, QDirIterator::Subdirectories);
    return iterator.hasNext() ? iterator.next() : QString();
}

inline QString ensureSampleFile(const QUrl& url, const QString& archiveName, const QString& folderName,
                                const QString& requiredFileName, QWidget* parent, const QByteArray& expectedSha256)
{
    const QString dataRoot = sampleDataPath();
    const QString targetDir = QDir(dataRoot).absoluteFilePath(folderName);
    const QString existing = findFile(targetDir, requiredFileName);
    if (!existing.isEmpty())
    {
        QFile marker(QDir(targetDir).filePath(".archive-sha256"));
        if (marker.open(QIODevice::ReadOnly) && marker.readAll().trimmed() == expectedSha256)
            return existing;
    }

    QDir().mkpath(targetDir);
    const QString archivePath = QDir(dataRoot).absoluteFilePath(archiveName);
    if (!QFileInfo::exists(archivePath) && !downloadFile(url, archivePath, parent))
        return {};

    QFile archive(archivePath);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!archive.open(QIODevice::ReadOnly) || !hash.addData(&archive) || hash.result().toHex() != expectedSha256)
    {
        archive.close();
        QFile::remove(archivePath);
        sampleError(parent, "Download integrity", "ZIP SHA-256 mismatch. Download again.");
        return {};
    }
    archive.close();
#ifdef Q_OS_WIN
    const QString extractorProgram = QStandardPaths::findExecutable(QStringLiteral("tar"));
    const QStringList extractorArguments = {QStringLiteral("-xf"), QDir::toNativeSeparators(archivePath)};
#else
    const QString extractorProgram = QStandardPaths::findExecutable(QStringLiteral("unzip"));
    const QStringList extractorArguments = {QStringLiteral("-o"), archivePath, QStringLiteral("-d"), targetDir};
#endif
    if (extractorProgram.isEmpty())
    {
        sampleError(parent, QStringLiteral("BuildingChangeReview"),
                    QStringLiteral("Sample ZIP extractor was not found."));
        return {};
    }

    QProcess extractor;
    extractor.setWorkingDirectory(targetDir);
    extractor.start(extractorProgram, extractorArguments);
    QEventLoop extractionLoop;
    QObject::connect(&extractor, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &extractionLoop,
                     &QEventLoop::quit);
    QObject::connect(&extractor, &QProcess::errorOccurred, &extractionLoop, &QEventLoop::quit);
    if (extractor.state() != QProcess::NotRunning)
        extractionLoop.exec();
    const bool finished = extractor.state() == QProcess::NotRunning;
    if (!finished || extractor.exitStatus() != QProcess::NormalExit || extractor.exitCode() != 0)
    {
        const QString details = QString::fromLocal8Bit(extractor.readAllStandardError()).trimmed();
        sampleError(parent, QStringLiteral("BuildingChangeReview"),
                    details.isEmpty() ? QStringLiteral("Sample ZIP could not be extracted.")
                                      : QStringLiteral("Sample ZIP could not be extracted:\n%1").arg(details));
        return {};
    }
    QFile marker(QDir(targetDir).filePath(".archive-sha256"));
    if (marker.open(QIODevice::WriteOnly))
        marker.write(expectedSha256);
    QFile::remove(archivePath);
    return findFile(targetDir, requiredFileName);
}

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Core::CoordinateSystems;
using namespace GeoKernel::Core::Symbology;
using namespace GeoKernel::Formats::Raster::Tiff;

class ChangeRenderer final : public GisSymbolRenderer
{
    bool beforeDate;

  public:
    explicit ChangeRenderer(bool before = false) : beforeDate(before) {}
    GisSymbolRendererType type() const override
    {
        return GisSymbolRendererType::RuleBased;
    }
    bool usesFeatureAttributes() const override
    {
        return true;
    }
    std::unique_ptr<GisSymbolRenderer> clone() const override
    {
        return std::make_unique<ChangeRenderer>(beforeDate);
    }
    GisLayerStyle styleForFeature(const GisSymbolContext& context) const override
    {
        const auto status = context.attributes.value("review").toString();
        const QString color = status == "rejected"                        ? "#82909f"
                              : context.attributes.value("kind") == "new" ? "#20bf73"
                                                                          : "#ee554f";
        GisLayerStyle style;
        const bool belongsHere =
            beforeDate ? context.attributes.value("kind") == "removed" : context.attributes.value("kind") == "new";
        if (!belongsHere)
        {
            style.setFillOpacity(0);
            style.setLineColor("transparent");
            style.setLineWidth(0);
            return style;
        }
        style.setFillColor(color);
        style.setLineColor(color);
        style.setFillOpacity(status == "rejected" ? 10 : status == "approved" ? 90 : 35);
        style.setLineWidth(status == "approved" ? 3.f : 2.f);
        return style;
    }
};

class DemoWindow final : public QMainWindow
{
    GisViewer *beforeView = nullptr, *afterView = nullptr;
    GisLayerVector *beforeOverlay = nullptr, *afterOverlay = nullptr;
    QComboBox *provider = nullptr, *scope = nullptr, *profile = nullptr;
    QDoubleSpinBox *threshold = nullptr, *minimumArea = nullptr, *overlap = nullptr, *tolerance = nullptr,
                   *minimumScore = nullptr, *maximumOtherScore = nullptr;
    QCheckBox* showCandidates = nullptr;
    QPushButton *run = nullptr, *cancelButton = nullptr, *approve = nullptr, *reject = nullptr, *pending = nullptr,
                *save = nullptr, *locate = nullptr;
    QProgressBar* progress = nullptr;
    QLabel *summary = nullptr, *details = nullptr;
    QToolButton* infoButton = nullptr;
    QTableWidget* table = nullptr;
    QLineEdit *reviewer = nullptr, *note = nullptr;
    QAction *editAction = nullptr, *panAction = nullptr, *undoAction = nullptr, *redoAction = nullptr;
    QString beforePath, afterPath, modelPath;
    std::shared_ptr<JobResult> result;
    QJsonArray audit;
    QString previousAuditHash;
    std::shared_ptr<std::atomic_bool> cancellation = std::make_shared<std::atomic_bool>(false);
    bool busy = false, syncing = false, selecting = false;
    GisExtent datasetExtent = datasetRegion();

    bool overlaysReady() const
    {
        return beforeOverlay && afterOverlay && beforeView->mapLayerIndex(beforeOverlay) >= 0 &&
               afterView->mapLayerIndex(afterOverlay) >= 0;
    }
    void clearCandidateOverlays()
    {
        QScopedValueRollback<bool> guard(selecting, true);
        beforeView->clearSelectedFeatures();
        afterView->clearSelectedFeatures();
        const auto remove = [](GisViewer* view, GisLayerVector*& overlay) {
            const auto* old = overlay;
            overlay = nullptr;
            const int index = old ? view->mapLayerIndex(old) : -1;
            if (index >= 0)
                view->removeLayer(index);
        };
        remove(beforeView, beforeOverlay);
        remove(afterView, afterOverlay);
    }

    QDoubleSpinBox* spin(QFormLayout* form, const QString& title, double low, double high, double value, double step, int decimals = 2)
    {
        auto* box = new QDoubleSpinBox;
        box->setRange(low, high);
        box->setDecimals(decimals);
        box->setSingleStep(step);
        box->setValue(value);
        form->addRow(title, box);
        return box;
    }

    void setBusy(bool value, bool inference = false)
    {
        busy = value;
        for (auto* control : std::array<QWidget*, 10>{run, provider, scope, profile, threshold, minimumArea, overlap,
                                                      tolerance, minimumScore, maximumOtherScore})
            control->setEnabled(!value);
        cancelButton->setEnabled(value && inference);
        cancelButton->setVisible(value && inference);
        editAction->setEnabled(!value && overlaysReady() && showCandidates->isChecked());
        undoAction->setEnabled(!value && overlaysReady() && showCandidates->isChecked());
        redoAction->setEnabled(!value && overlaysReady() && showCandidates->isChecked());
        table->setEnabled(!value);
        reviewer->setEnabled(!value);
        note->setEnabled(!value);
        refreshButtons();
    }

    int row() const
    {
        const int index = table->currentRow();
        return result && index >= 0 && size_t(index) < result->candidates.size() ? index : -1;
    }

    void refreshButtons()
    {
        const bool selected = !busy && overlaysReady() && row() >= 0;
        for (auto* button : {approve, reject, pending, locate})
            button->setEnabled(selected);
        int count = 0;
        if (result)
            for (const auto& item : result->candidates)
                count += item.review == "approved";
        save->setEnabled(!busy && overlaysReady() && count > 0);
    }

    void refreshMaps()
    {
        beforeView->invalidateRenderCache(true, true);
        beforeView->refreshLayers();
        afterView->invalidateRenderCache(true, true);
        afterView->refreshLayers();
    }

    void setDetails(const QString& text, bool error = false)
    {
        details->setText(text);
        if (error)
            infoButton->setChecked(true);
    }

    void applyOverlayVisibility()
    {
        for (auto* view : {beforeView, afterView})
        {
            auto* layer = view == beforeView ? beforeOverlay : afterOverlay;
            const int index = layer ? view->mapLayerIndex(layer) : -1;
            if (index >= 0)
                view->setLayerVisible(index, showCandidates->isChecked());
            if (!showCandidates->isChecked())
                view->clearSelectedFeatures();
        }
        if (!showCandidates->isChecked())
        {
            panAction->trigger();
        }
        setBusy(busy, busy && cancelButton->isEnabled());
        refreshMaps();
    }

    void refreshTable()
    {
        if (!result)
            return;
        int approvedCount = 0, rejectedCount = 0, newCount = 0;
        for (size_t index = 0; index < result->candidates.size(); ++index)
        {
            const auto& item = result->candidates[index];
            const QStringList values{item.id, item.kind, QString::number(item.areaM2, 'f', 1), item.review};
            for (int column = 0; column < values.size(); ++column)
            {
                auto* cell = table->item(int(index), column);
                if (!cell)
                {
                    cell = new QTableWidgetItem;
                    table->setItem(int(index), column, cell);
                }
                cell->setText(values[column]);
                cell->setToolTip(QString("Mean building score: %1; other date: %2\nOther-date buffered coverage: "
                                         "%3%\nRaw scores are not change probabilities.\n%4")
                                     .arg(item.buildingScore, 0, 'f', 3)
                                     .arg(item.otherScore, 0, 'f', 3)
                                     .arg(item.overlap * 100, 0, 'f', 1)
                                     .arg(item.note));
            }
            approvedCount += item.review == "approved";
            rejectedCount += item.review == "rejected";
            newCount += item.kind == "new";
        }
        summary->setText(QString("%1 new · %2 removed\n%3 approved · %4 rejected · %5 pending")
                             .arg(newCount)
                             .arg(result->candidates.size() - newCount)
                             .arg(approvedCount)
                             .arg(rejectedCount)
                             .arg(result->candidates.size() - approvedCount - rejectedCount));
        if (result->candidates.empty())
            summary->setText("No candidates passed the filters.");
        refreshButtons();
    }

    void appendAudit(const Candidate& item, const QString& action)
    {
        QJsonObject event{
            {"sequence", audit.size() + 1},
            {"recorded_at_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"candidate_id", item.id},
            {"action", action},
            {"actor", reviewer->text().trimmed()},
            {"note", item.note},
            {"geometry_sha256",
             QString(QCryptographicHash::hash(item.geometry->toWkb(), QCryptographicHash::Sha256).toHex())},
            {"previous_hash", previousAuditHash}};
        previousAuditHash =
            QCryptographicHash::hash(QJsonDocument(event).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
                .toHex();
        event["hash"] = previousAuditHash;
        audit.append(event);
    }

    void updateAttributes(int index)
    {
        auto& item = result->candidates[size_t(index)];
        for (auto* layer : {beforeOverlay, afterOverlay})
        {
            if (auto* shape = layer ? layer->getShape(index + 1) : nullptr)
            {
                shape->attributes()["review"] = item.review;
                shape->attributes()["kind"] = item.kind;
            }
        }
    }

    void captureEdits()
    {
        if (busy || !result || !overlaysReady())
            return;
        bool changed = false;
        for (size_t index = 0; index < result->candidates.size(); ++index)
        {
            auto& item = result->candidates[index];
            auto* current = afterOverlay->getShape(int(index) + 1);
            if (!current || !::updateGeometry(item, *current))
                continue;
            beforeView->clearSelectedFeatures();
            beforeOverlay->setShapeGeometryEdit(int(index) + 1, item.geometry->clone());
            updateAttributes(int(index));
            appendAudit(item, "geometry-edited");
            changed = true;
        }
        if (changed)
        {
            refreshTable();
            refreshMaps();
        }
    }

    void decide(const QString& decision)
    {
        captureEdits();
        const int index = row();
        if (index < 0 || busy)
            return;
        if (reviewer->text().trimmed().isEmpty())
        {
            QMessageBox::warning(this, "Review", "Enter a reviewer name before recording a decision.");
            return;
        }
        auto& item = result->candidates[size_t(index)];
        try
        {
            reviewCandidate(item, decision, reviewer->text(), note->text());
            updateAttributes(index);
            appendAudit(item, decision);
            refreshTable();
            refreshMaps();
            statusBar()->showMessage(item.id + ": " + decision);
        }
        catch (const std::exception& error)
        {
            QMessageBox::warning(this, "Review", error.what());
        }
    }

    void selectCandidate(int index, bool zoom)
    {
        if (busy || selecting || !result || !overlaysReady() || index < 0 || size_t(index) >= result->candidates.size())
            return;
        if (!beforeOverlay->getShape(index + 1) || !afterOverlay->getShape(index + 1))
            return;
        QScopedValueRollback<bool> guard(selecting, true);
        if (table->currentRow() != index)
            table->selectRow(index);
        note->setText(result->candidates[size_t(index)].note);
        const auto selectedExtent = result->candidates[size_t(index)].geometry->extent();
        for (auto* view : {beforeView, afterView})
        {
            if (!showCandidates->isChecked())
                break;
            auto* layer = view == beforeView ? beforeOverlay : afterOverlay;
            FeatureSources::FeatureHitTestResult hit;
            hit.layer = layer;
            hit.shape = layer->getShape(index + 1);
            hit.featureId = index + 1;
            hit.layerIndex = view->mapLayerIndex(layer);
            view->setSelectedFeature(hit);
        }
        if (zoom)
        {
            const auto extent = selectedExtent.inflate(std::max(10., selectedExtent.width() * .5),
                                                       std::max(10., selectedExtent.height() * .5));
            beforeView->setViewExtent(extent);
            afterView->setViewExtent(extent);
        }
        refreshButtons();
    }

    void installResults()
    {
        const auto extent = beforeView->viewExtent();
        std::array<std::unique_ptr<GisLayerVector>, 2> layers;
        for (int side = 0; side < 2; ++side)
        {
            if (result->candidates.empty())
                break;
            auto& layer = layers[side];
            layer = GisLayerVector::createInMemory("Building change candidates", GisShapeType::Polygon);
            layer->setCoordinateSystem(CoordinateSystemFactory::fromEpsg(25831));
            layer->addAttributeDefinition({"candidate_id", GisAttributeType::String, 64, 0});
            layer->addAttributeDefinition({"kind", GisAttributeType::String, 16, 0});
            layer->addAttributeDefinition({"review", GisAttributeType::String, 16, 0});
            layer->setSymbolRenderer(std::make_unique<ChangeRenderer>(side == 0));
            for (const auto& candidate : result->candidates)
            {
                if (!candidate.geometry || candidate.geometry->isEmpty())
                    throw std::runtime_error("Candidate geometry is missing or empty.");
                auto shape = candidate.geometry->clone();
                shape->attributes() = {
                    {"candidate_id", candidate.id}, {"kind", candidate.kind}, {"review", candidate.review}};
                if (!layer->addShape(std::move(shape)))
                    throw std::runtime_error("Could not add candidate polygon.");
            }
        }
        clearCandidateOverlays();
        for (int side = 0; side < 2; ++side)
        {
            if (!layers[side])
                continue;
            auto* view = side == 0 ? beforeView : afterView;
            auto* raw = layers[side].get();
            view->addLayer(layers[side]);
            const int index = view->mapLayerIndex(raw);
            if (index < 0)
                throw std::runtime_error("Candidate layer was not accepted by the viewer.");
            if (side == 0)
                beforeOverlay = raw;
            else
                afterOverlay = raw;
            if (!view->beginEditLayer(index) || !view->setActiveEditLayerIndex(index))
                throw std::runtime_error("Could not start the candidate edit session.");
        }
        beforeView->setViewExtent(extent);
        afterView->setViewExtent(extent);
        table->setRowCount(int(result->candidates.size()));
        refreshTable();
        applyOverlayVisibility();
        if (!result->candidates.empty())
            selectCandidate(0, false);
        setDetails(QString("%8: %1 instances · %9: %2 instances\nElapsed: %3 s · provider: %4\n"
                           "Excluded: %5 edge, %6 invalid, %7 matched/uncertain instances.\nNo reference labels; "
                           "candidate counts are not accuracy.")
                       .arg(result->beforeInstances)
                       .arg(result->afterInstances)
                       .arg(result->elapsedMilliseconds / 1000., 0, 'f', 1)
                       .arg(result->provenance["execution_provider"].toString())
                       .arg(result->edgeInstances)
                       .arg(result->invalidPolygons)
                       .arg(result->uncertainInstances)
                       .arg(BeforeYear)
                       .arg(AfterYear));
        refreshMaps();
    }

    void finishAnalysis(const std::shared_ptr<JobResult>& job)
    {
        if (cancellation->load() || job->cancelled)
        {
            progress->setValue(0);
            progress->setFormat("Cancelled");
            statusBar()->showMessage("Analysis cancelled. Previous completed results, if any, are retained.");
        }
        else if (!job->error.isEmpty())
        {
            progress->setValue(0);
            progress->setFormat("Analysis failed");
            setDetails(job->error, true);
            statusBar()->showMessage("Analysis failed. Previous completed results, if any, are retained.");
        }
        else
        {
            result = job;
            audit = {};
            previousAuditHash.clear();
            try
            {
                installResults();
                progress->setValue(100);
                progress->setFormat("Complete");
                statusBar()->showMessage("Analysis complete. Select a candidate to review.");
            }
            catch (const std::exception& error)
            {
                clearCandidateOverlays();
                result.reset();
                table->setRowCount(0);
                summary->setText("Candidate display failed. Run analysis to retry.");
                setDetails(error.what(), true);
                progress->setValue(0);
                progress->setFormat("Display failed");
                statusBar()->showMessage("Could not display candidate results. Source imagery is retained.");
            }
        }
        setBusy(false);
        if (result && !result->candidates.empty() && overlaysReady())
            selectCandidate(0, false);
    }

    void prepare()
    {
        if (busy)
            return;
        setBusy(true);
        progress->setRange(0, 0);
        statusBar()->showMessage("Preparing verified sample data...");
        try
        {
            QString first;
            for (int attempt = 0; attempt < 2; ++attempt)
            {
                first = ensureSampleFile(QUrl(DatasetUrl), DatasetArchive, DatasetCache, BeforeFile, this, DatasetSha256);
                if (first.isEmpty())
                    throw std::runtime_error("Sample download interrupted. Click Run to retry.");
                
                beforePath = first;
                
                afterPath = findFile(QFileInfo(first).absolutePath(), AfterFile);
                const auto valid = [](const QString& path, const QByteArray& expected) {
                    QFile file(path);
                    QCryptographicHash hash(QCryptographicHash::Sha256);
                    return file.open(QIODevice::ReadOnly) && hash.addData(&file) && hash.result().toHex() == expected;
                };

                if (valid(beforePath, BeforeSha256) && valid(afterPath, AfterSha256))
                    break;

                QFile::remove(QDir(sampleDataPath()).filePath(QString(DatasetCache) + "/.archive-sha256"));

                first.clear();
            }

            if (first.isEmpty())
                throw std::runtime_error("Sample rasters failed their individual SHA-256 checks.");

            if (beforeView->layerCount() == 0)
            {
                for (auto* view : {beforeView, afterView})
                {
                    auto layer = std::make_unique<GisLayerTIFF>(view == beforeView ? beforePath : afterPath);
                    layer->setName(QString("PDOK %1").arg(view == beforeView ? BeforeYear : AfterYear));
                    layer->open();
                    view->setCoordinateSystem(CoordinateSystemFactory::fromEpsg(25831));
                    view->addLayer(layer);
                    view->setViewExtent(demoRegion());
                }
            }

            statusBar()->showMessage("Preparing the HOTOSM building model...");

            const auto manifest = ensureSampleFile(QUrl(ModelUrl), ModelArchive, "hotosm-model", "geokernel-model.json", this, ModelSha256);
            if (manifest.isEmpty())
                throw std::runtime_error("Model preparation interrupted. Click Run to retry.");

            modelPath = QFileInfo(manifest).absolutePath();
            statusBar()->showMessage("Ready. Pan/zoom to choose an area, then run the two-date analysis.");
            progress->setRange(0, 100);
            progress->setValue(0);
            progress->setFormat("Ready");
        }
        catch (const std::exception& error)
        {
            statusBar()->showMessage(QString::fromUtf8(error.what()));
            setDetails(QString::fromUtf8(error.what()), true);
            progress->setRange(0, 100);
            progress->setValue(0);
            progress->setFormat("Not ready");
        }

        setBusy(false);
    }

    void startAnalysis()
    {
        if (busy)
            return;

        if (modelPath.isEmpty() || beforePath.isEmpty() || afterPath.isEmpty())
            prepare();

        if (modelPath.isEmpty() || beforePath.isEmpty() || afterPath.isEmpty())
            return;

        captureEdits();

        if (result && !result->candidates.empty() &&
            QMessageBox::question(
                this, "Run analysis",
                "A completed new run replaces the current session's candidates and review decisions. Continue?") !=
                QMessageBox::Yes)
            return;

        JobOptions options;
        options.before = beforePath;
        options.after = afterPath;
        options.model = modelPath;
        options.region = scope->currentIndex() == 0   ? demoRegion()
                         : scope->currentIndex() == 1 ? beforeView->viewExtent()
                                                      : datasetExtent;
        options.foregroundThreshold = threshold->value();
        options.provider = static_cast<GeoKernel::AI::AiExecutionProvider>(provider->currentData().toInt());
        options.matching = {minimumArea->value(),  overlap->value(),           tolerance->value(),
                            minimumScore->value(), maximumOtherScore->value(), .25};
        const double x0 = std::max(options.region.xMin(), datasetExtent.xMin());
        const double y0 = std::max(options.region.yMin(), datasetExtent.yMin());
        const double x1 = std::min(options.region.xMax(), datasetExtent.xMax());
        const double y1 = std::min(options.region.yMax(), datasetExtent.yMax());
        if (x1 <= x0 || y1 <= y0)
        {
            QMessageBox::warning(this, "Analysis area", "The view does not intersect the imagery.");
            return;
        }
        options.region = GisExtent(x0, y0, x1, y1);
        cancellation->store(false);
        setBusy(true, true);
        panAction->setChecked(true);
        beforeView->setActiveTool(GisViewerTool::Pan);
        afterView->setActiveTool(GisViewerTool::Pan);
        progress->setValue(0);
        progress->setFormat("Starting...");
        statusBar()->showMessage("Analysing both dates. Candidate results will appear after completion.");
        const auto job = std::make_shared<JobResult>();
        const auto cancel = cancellation;
        auto* worker = QThread::create([this, options, job, cancel] {
            try
            {
                runAnalysis(
                    options, *job,
                    [cancel] {
                        return cancel->load();
                    },
                    [this](int value, const QString& message) {
                        QMetaObject::invokeMethod(
                            this,
                            [this, value, message] {
                                progress->setValue(value);
                                progress->setFormat("%p%");
                                progress->setToolTip(message);
                                statusBar()->showMessage(message);
                            },
                            Qt::QueuedConnection);
                    });
            }
            catch (const std::exception& error)
            {
                if (cancel->load())
                    job->cancelled = true;
                else
                    job->error = QString::fromUtf8(error.what());
            }
        });
        connect(worker, &QThread::finished, this, [this, worker, job] {
            worker->deleteLater();
            finishAnalysis(job);
        });
        worker->start();
    }

    void exportApproved()
    {
        captureEdits();
        if (busy || !result)
            return;
        try
        {
            const auto collection = approvedCollection(*result, audit);
            const auto features = collection["features"].toArray();
            if (features.isEmpty())
                return;
            const auto path = QFileDialog::getSaveFileName(this, "Save approved building changes",
                                                           "building_changes.geojson", "GeoJSON (*.geojson)");
            if (path.isEmpty())
                return;
            const QString output = path.endsWith(".geojson", Qt::CaseInsensitive) ? path : path + ".geojson";
            const auto bytes = QJsonDocument(collection).toJson(QJsonDocument::Indented);
            QSaveFile file(output);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
                throw std::runtime_error("Could not save the approved change dataset.");
            statusBar()->showMessage(
                QString("Saved %1 approved polygons: %2").arg(features.size()).arg(QDir::toNativeSeparators(output)));
        }
        catch (const std::exception& error)
        {
            QMessageBox::critical(this, "Export", error.what());
        }
    }

  protected:
    void closeEvent(QCloseEvent* event) override
    {
        if (busy)
        {
            cancellation->store(true);
            statusBar()->showMessage("Cancelling; wait for work to finish before closing.");
            event->ignore();
        }
        else
            QMainWindow::closeEvent(event);
    }

  public:
    DemoWindow()
    {
        resize(1460, 860);
        setWindowTitle("BuildingChangeReview");
        setWindowIcon(QIcon(":/icons/geokernel.ico"));
        auto* splitter = new QSplitter(this);
        setCentralWidget(splitter);
        for (int i = 0; i < 2; ++i)
        {
            auto* pane = new QWidget;
            auto* layout = new QVBoxLayout(pane);
            layout->setContentsMargins(0, 0, 0, 0);
            auto* label = new QLabel(i == 0 ? QString("%1 — BEFORE / REMOVAL CANDIDATES").arg(BeforeYear)
                                            : QString("%1 — AFTER / ADDITION CANDIDATES").arg(AfterYear));
            label->setAlignment(Qt::AlignCenter);
            label->setMargin(6);
            layout->addWidget(label);
            auto* view = new GisViewer(pane);
            view->setActiveTool(GisViewerTool::Pan);
            layout->addWidget(view, 1);
            if (!i)
                beforeView = view;
            else
                afterView = view;
            splitter->addWidget(pane);
        }
        splitter->setChildrenCollapsible(false);
        connect(beforeView, &GisViewer::visibleExtentChanged, this, [this](const GisExtent& extent) {
            if (!syncing)
            {
                syncing = true;
                afterView->setViewExtent(extent);
                syncing = false;
            }
        });
        connect(afterView, &GisViewer::visibleExtentChanged, this, [this](const GisExtent& extent) {
            if (!syncing)
            {
                syncing = true;
                beforeView->setViewExtent(extent);
                syncing = false;
            }
        });
        auto* toolbar = addToolBar("Map navigation");
        toolbar->setMovable(false);
        toolbar->setIconSize(QSize(32, 32));
        auto* zoomIn = toolbar->addAction(QIcon(":/icons/zoom-in.svg"), "Zoom In");
        auto* zoomOut = toolbar->addAction(QIcon(":/icons/zoom-out.svg"), "Zoom Out");
        auto* full = toolbar->addAction(QIcon(":/icons/full-extent.svg"), "Full Extent");
        connect(zoomIn, &QAction::triggered, beforeView, [this] {
            beforeView->zoomIn();
        });
        connect(zoomOut, &QAction::triggered, beforeView, [this] {
            beforeView->zoomOut();
        });
        connect(full, &QAction::triggered, beforeView, [this] {
            beforeView->setViewExtent(datasetExtent);
        });
        auto* demoArea = toolbar->addAction("Demo area");
        connect(demoArea, &QAction::triggered, beforeView, [this] {
            beforeView->setViewExtent(demoRegion());
        });
        auto* tools = new QActionGroup(this);
        tools->setExclusive(true);
        panAction = toolbar->addAction(QIcon(":/icons/pan.svg"), "Pan");
        auto* box = toolbar->addAction(QIcon(":/icons/zoom-box.svg"), "Zoom Rect");
        editAction = toolbar->addAction(QString("Edit Vertices (%1)").arg(AfterYear));
        editAction->setShortcut(QKeySequence("Ctrl+E"));
        for (auto* action : {panAction, box, editAction})
        {
            action->setCheckable(true);
            tools->addAction(action);
        }
        panAction->setChecked(true);
        connect(panAction, &QAction::triggered, this, [this] {
            beforeView->setActiveTool(GisViewerTool::Pan);
            afterView->setActiveTool(GisViewerTool::Pan);
        });
        connect(box, &QAction::triggered, this, [this] {
            beforeView->setActiveTool(GisViewerTool::ZoomBox);
            afterView->setActiveTool(GisViewerTool::ZoomBox);
        });
        connect(editAction, &QAction::triggered, this, [this] {
            beforeView->setActiveTool(GisViewerTool::Pan);
            afterView->setActiveTool(GisViewerTool::EditVertices);
        });
        toolbar->addSeparator();
        undoAction = toolbar->addAction("Undo edit");
        redoAction = toolbar->addAction("Redo edit");
        connect(undoAction, &QAction::triggered, this, [this] {
            const int index = afterOverlay ? afterView->mapLayerIndex(afterOverlay) : -1;
            if (index >= 0)
            {
                afterView->undoEditLayer(index);
                captureEdits();
            }
        });
        connect(redoAction, &QAction::triggered, this, [this] {
            const int index = afterOverlay ? afterView->mapLayerIndex(afterOverlay) : -1;
            if (index >= 0)
            {
                afterView->redoEditLayer(index);
                captureEdits();
            }
        });
        auto* panel = new QWidget;
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(8);
        auto* title = new QLabel(QString("<b>Oosterwold</b> · %1 → %2").arg(BeforeYear).arg(AfterYear));
        layout->addWidget(title);
        auto* form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        scope = new QComboBox;
        scope->addItems({"Demo area (fixed)", "Current view (both dates)", "Full dataset"});
        form->addRow("Area", scope);
        profile = new QComboBox;
        profile->addItems({"Conservative", "Balanced", "Custom"});
        form->addRow("Filter", profile);
        layout->addLayout(form);
        auto* settingsToggle = new QToolButton;
        settingsToggle->setText("Settings");
        settingsToggle->setCheckable(true);
        settingsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        settingsToggle->setArrowType(Qt::RightArrow);
        settingsToggle->setAutoRaise(true);
        settingsToggle->setShortcut(QKeySequence("Alt+S"));
        settingsToggle->setToolTip("Analysis parameters (Alt+S)");
        layout->addWidget(settingsToggle);
        auto* advanced = new QWidget;
        auto* advancedForm = new QFormLayout(advanced);
        advancedForm->setContentsMargins(0, 0, 0, 0);
        advancedForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        advancedForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
        provider = new QComboBox;
        using GeoKernel::AI::AiExecutionProvider;
        for (auto choice : {AiExecutionProvider::Auto, AiExecutionProvider::CPU, AiExecutionProvider::CUDA,
                            AiExecutionProvider::DirectML})
            provider->addItem(GeoKernel::AI::aiExecutionProviderName(choice), int(choice));
        advancedForm->addRow("Provider", provider);
        threshold = spin(advancedForm, "Building threshold", .05, .95, .4371, .025, 4);
        threshold->setToolTip(
            "Model package default: 0.4371. A higher threshold can miss existing buildings and create false changes.");
        minimumArea = spin(advancedForm, "Min. area (m²)", 1, 1000, 50, 5, 1);
        tolerance = spin(advancedForm, "Tolerance (m)", 0, 5, 2, .5, 1);
        tolerance->setToolTip(
            "Square buffering of weak building evidence (score >= 0.25). Does not register the imagery.");
        overlap = spin(advancedForm, "Other-date coverage <", .01, .95, .10, .05);
        minimumScore = spin(advancedForm, "Own-date mean ≥", 0, 1, .75, .05);
        maximumOtherScore = spin(advancedForm, "Other-date mean ≤", 0, 1, .20, .05);
        overlap->setToolTip(
            "Buffered coverage of raw building scores >= 0.25, including buildings missed by instance separation.");
        minimumScore->setToolTip(
            "Raw mean building score within the predicted instance; not a calibrated change probability.");
        maximumOtherScore->setToolTip(
            "Suppress the candidate when the same footprint has substantial building evidence in the other date.");
        layout->addWidget(advanced);
        advanced->hide();
        connect(settingsToggle, &QToolButton::toggled, this, [advanced, settingsToggle](bool expanded) {
            advanced->setVisible(expanded);
            settingsToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        });
        connect(profile, qOverload<int>(&QComboBox::activated), this, [this](int index) {
            if (index > 1)
                return;
            threshold->setValue(.4371);
            minimumArea->setValue(index == 0 ? 50 : 20);
            tolerance->setValue(index == 0 ? 2 : 1);
            overlap->setValue(index == 0 ? .10 : .20);
            minimumScore->setValue(index == 0 ? .75 : .55);
            maximumOtherScore->setValue(index == 0 ? .20 : .35);
            profile->setCurrentIndex(index);
        });
        for (auto* box : {threshold, minimumArea, tolerance, overlap, minimumScore, maximumOtherScore})
            connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] {
                profile->setCurrentIndex(2);
            });
        run = new QPushButton("Run analysis");
        cancelButton = new QPushButton("Cancel");
        run->setShortcut(QKeySequence("Ctrl+R"));
        run->setToolTip("Run analysis (Ctrl+R)");
        auto* runRow = new QHBoxLayout;
        runRow->addWidget(run, 1);
        runRow->addWidget(cancelButton);
        layout->addLayout(runRow);
        connect(run, &QPushButton::clicked, this, [this] {
            startAnalysis();
        });
        connect(cancelButton, &QPushButton::clicked, this, [this] {
            cancellation->store(true);
            cancelButton->setEnabled(false);
            progress->setFormat("Cancelling...");
        });
        progress = new QProgressBar;
        progress->setRange(0, 100);
        progress->setValue(0);
        progress->setFormat("Ready");
        progress->setFixedHeight(18);
        progress->setAlignment(Qt::AlignCenter);
        progress->setStyleSheet("QProgressBar { border: 1px solid #cbd0d5; border-radius: 3px; background: #f1f3f5; "
                                "text-align: center; } QProgressBar::chunk { background: #20bf73; }");
        layout->addWidget(progress);
        summary = new QLabel("Run analysis to find candidates.");
        summary->setWordWrap(true);
        layout->addWidget(summary);
        summary->setToolTip("Model predictions require human review before export.");
        table = new QTableWidget(0, 4);
        table->setHorizontalHeaderLabels({"Candidate", "Type", "m²", "Review"});
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->verticalHeader()->hide();
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->horizontalHeader()->setStretchLastSection(false);
        table->verticalHeader()->setDefaultSectionSize(26);
        table->setMinimumHeight(200);
        layout->addWidget(table, 1);
        connect(table, &QTableWidget::itemSelectionChanged, this, [this] {
            selectCandidate(row(), false);
        });
        connect(table, &QTableWidget::cellDoubleClicked, this, [this](int index, int) {
            selectCandidate(index, true);
        });
        auto* reviewForm = new QFormLayout;
        reviewer = new QLineEdit("demo-reviewer");
        note = new QLineEdit;
        reviewForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        reviewForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
        reviewer->setMaxLength(100);
        note->setMaxLength(512);
        reviewForm->addRow("Reviewer", reviewer);
        reviewForm->addRow("Note", note);
        layout->addLayout(reviewForm);
        auto* reviewRow = new QHBoxLayout;
        approve = new QPushButton("Approve");
        reject = new QPushButton("Reject");
        pending = new QPushButton("Pending");
        approve->setShortcut(QKeySequence("Ctrl+Return"));
        approve->setToolTip("Approve selected candidate (Ctrl+Enter)");
        for (auto* button : {approve, reject, pending})
            reviewRow->addWidget(button);
        layout->addLayout(reviewRow);
        locate = new QPushButton("Locate");
        locate->setToolTip("Zoom to the selected candidate on both maps; double-clicking a table row does the same.");
        connect(approve, &QPushButton::clicked, this, [this] {
            decide("approved");
        });
        connect(reject, &QPushButton::clicked, this, [this] {
            decide("rejected");
        });
        connect(pending, &QPushButton::clicked, this, [this] {
            decide("pending");
        });
        connect(locate, &QPushButton::clicked, this, [this] {
            selectCandidate(row(), true);
        });
        save = new QPushButton("Export GeoJSON");
        save->setToolTip("Export approved candidates only.");
        auto* exportRow = new QHBoxLayout;
        exportRow->addWidget(locate);
        exportRow->addWidget(save, 1);
        layout->addLayout(exportRow);
        connect(save, &QPushButton::clicked, this, [this] {
            exportApproved();
        });
        save->setShortcut(QKeySequence::Save);
        showCandidates = new QCheckBox("Overlays");
        showCandidates->setChecked(true);
        showCandidates->setToolTip("Show candidate polygons. Uncheck to inspect the imagery.");
        auto* legend = new QLabel("<font color='#20bf73'>● New</font> &nbsp; <font color='#ee554f'>● Removed</font> "
                                  "&nbsp; <font color='#ef9b00'>● Selected</font>");
        legend->setToolTip(
            QString("Green: addition candidates in %1. Red: removal candidates in %2. Orange: selected location on "
                    "both dates. Stronger fill: approved; gray: rejected. Geometry edits require approval again.")
                .arg(AfterYear)
                .arg(BeforeYear));

        auto* overlayRow = new QHBoxLayout;
        overlayRow->addWidget(showCandidates);
        overlayRow->addStretch();
        overlayRow->addWidget(legend);
        layout->addLayout(overlayRow);
        connect(showCandidates, &QCheckBox::toggled, this, [this] {
            applyOverlayVisibility();
        });
        auto* credits = new QLabel("<a href='https://www.beeldmateriaal.nl/dataroom'>Beeldmateriaal / PDOK</a> · <a "
                                   "href='https://creativecommons.org/licenses/by/4.0/'>CC BY 4.0</a>");
        credits->setOpenExternalLinks(true);
        credits->setToolTip(Attribution);
        infoButton = new QToolButton;
        infoButton->setText("Details");
        infoButton->setCheckable(true);
        infoButton->setAutoRaise(true);
        infoButton->setShortcut(QKeySequence("Alt+D"));
        infoButton->setToolTip("Run details, source and model information (Alt+D)");
        auto* footer = new QHBoxLayout;
        footer->addWidget(credits);
        footer->addStretch();
        footer->addWidget(infoButton);
        layout->addLayout(footer);
        auto* infoPanel = new QWidget;
        auto* infoLayout = new QVBoxLayout(infoPanel);
        infoLayout->setContentsMargins(0, 0, 0, 0);
        details = new QLabel("Run analysis to see processing details.");
        details->setWordWrap(true);
        infoLayout->addWidget(details);
        auto* attribution = new QLabel(Attribution);
        attribution->setWordWrap(true);
        infoLayout->addWidget(attribution);
        layout->addWidget(infoPanel);
        infoPanel->hide();
        connect(infoButton, &QToolButton::toggled, infoPanel, &QWidget::setVisible);
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(panel);
        auto* dock = new QDockWidget("Change review", this);
        dock->setMinimumWidth(350);
        dock->setWidget(scroll);
        addDockWidget(Qt::RightDockWidgetArea, dock);
        resizeDocks({dock}, {390}, Qt::Horizontal);
        auto* watcher = new QTimer(this);
        watcher->setInterval(250);
        connect(watcher, &QTimer::timeout, this, [this] {
            captureEdits();
        });
        watcher->start();
        connect(afterView, &GisViewer::selectionChanged, this, [this](int) {
            if (selecting || busy || !result)
                return;
            const auto selected = afterView->selectedFeatures();
            if (selected.isEmpty() || selected.first().layer != afterOverlay)
                return;
            const auto id = selected.first().attributes().value("candidate_id").toString();
            for (size_t i = 0; i < result->candidates.size(); ++i)
                if (result->candidates[i].id == id)
                {
                    selectCandidate(int(i), false);
                    break;
                }
        });
        setBusy(false);
        QTimer::singleShot(0, this, [this] {
            prepare();
        });
    }
};

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("BuildingChangeReview");
    QApplication::setWindowIcon(QIcon(":/icons/geokernel.ico"));
    DemoWindow window;
    window.show();
    return app.exec();
}
