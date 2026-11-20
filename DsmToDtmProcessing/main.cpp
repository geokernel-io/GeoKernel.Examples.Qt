#include <Common/Gdal/GdalElevationProvider.h>
#include <CoordinateSystems/CoordinateSystemFactory.h>
#include <Shapes/GisExtent.h>
#include <Viewer/GisViewer.h>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLibrary>
#include <QLineF>
#include <QLinearGradient>
#include <QMainWindow>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <cstdlib>
#include <deque>
#include <limits>
#include <vector>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>

using namespace GeoKernel::Formats::Common::Gdal;
using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Shapes;
using GeoKernel::Core::CoordinateSystems::CoordinateSystemFactory;
using GeoKernel::Core::Layers::GisLayer;

void require(bool condition, const QString& message)
{
    if (!condition)
        throw std::runtime_error(message.toStdString());
}

QJsonObject readJson(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read " + path);

    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    require(error.error == QJsonParseError::NoError && document.isObject(), "Invalid JSON: " + path);

    return document.object();
}

QByteArray hashFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QCryptographicHash hash(QCryptographicHash::Sha256);

    return hash.addData(&file) ? hash.result().toHex() : QByteArray();
}

void checkpoint(const std::atomic_bool& cancelled)
{
    require(!cancelled.load(), "Operation cancelled.");
}

bool validSample(const QString& folder, const QJsonObject& assets)
{
    const auto files = assets.value("files").toObject();
    if (files.isEmpty())
        return false;

    for (auto it = files.begin(); it != files.end(); ++it)
        if (hashFile(QDir(folder).filePath(it.key())) != it.value().toString().toLatin1())
            return false;

    return true;
}

using Progress = std::function<void(int, const QString&)>;

void download(const QUrl& url, const QString& destination, std::atomic_bool& cancelled, const Progress& progress)
{
    require(url.scheme() == "https", "Sample URL must use HTTPS.");

    QSaveFile file(destination);
    require(file.open(QIODevice::WriteOnly), "Cannot write sample cache: " + destination);

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "GeoKernel-DsmToDtmProcessing/1.0");
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    request.setTransferTimeout(60000);

    auto* reply = manager.get(request);

    QEventLoop loop;
    bool writeFailed = false;

    QTimer cancelPoll;
    cancelPoll.setInterval(50);

    QObject::connect(&cancelPoll, &QTimer::timeout, reply,
                     [&]
                     {
                         if (cancelled)
                             reply->abort();
                     });

    QObject::connect(reply, &QNetworkReply::readyRead, &loop,
                     [&]
                     {
                         const auto data = reply->readAll();
                         if (file.write(data) != data.size())
                         {
                             writeFailed = true;
                             reply->abort();
                         }
                     });

    QObject::connect(reply, &QNetworkReply::downloadProgress, &loop,
                     [&](qint64 done, qint64 total)
                     {
                         progress(total > 0 ? int(100 * done / total) : 0, "Downloading sample data...");
                     });

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    cancelPoll.start();
    loop.exec();

    const auto tail = reply->readAll();
    if (file.write(tail) != tail.size())
        writeFailed = true;

    checkpoint(cancelled);
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    require(!writeFailed && reply->error() == QNetworkReply::NoError && status == 200,
            QString("Sample download failed (HTTP %1): %2\n%3\n"
                    "The sample ZIP must be uploaded to the SampleData v1 release. Use Retry after upload.")
                .arg(status)
                .arg(reply->errorString(), url.toString()));

    require(file.commit(), "Could not save the downloaded sample archive.");
}

QString prepareSample(const QJsonObject& assets, const QString& root, const QString& localArchive, const QUrl& url,
                      std::atomic_bool& cancelled, const Progress& progress)
{
    QDir().mkpath(root);
    const QString folder = QDir(root).filePath(assets.value("cache").toString());
    progress(0, "Checking cached sample files...");
    if (validSample(folder, assets))
        return folder;

    const QString archive =
        localArchive.isEmpty() ? QDir(root).filePath(assets.value("archive").toString()) : localArchive;
    const QByteArray expected = assets.value("sha256").toString().toLatin1();

    if (localArchive.isEmpty() && hashFile(archive) != expected)
        download(url, archive, cancelled, progress);

    checkpoint(cancelled);

    require(hashFile(archive) == expected, "Sample ZIP SHA-256 mismatch. The existing data was retained.");

    QTemporaryDir staging(QDir(root).filePath(".extract-XXXXXX"));
    require(staging.isValid(), "Cannot create the sample extraction directory.");

#ifdef Q_OS_WIN
    const QString program = QStandardPaths::findExecutable("tar");
    const QStringList arguments = {"-xf", QDir::toNativeSeparators(QFileInfo(archive).absoluteFilePath())};
#else
    const QString program = QStandardPaths::findExecutable("unzip");
    const QStringList arguments = {"-o", QFileInfo(archive).absoluteFilePath()};
#endif
    require(!program.isEmpty(), "Archive extractor not found (Windows: tar; Linux: unzip).");
    progress(0, "Extracting verified sample ZIP...");
    QProcess extractor;
    extractor.setWorkingDirectory(staging.path());
    QEventLoop loop;
    QTimer timer, timeout;
    timer.setInterval(50);
    timeout.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop,
                     [&]
                     {
                         if (cancelled)
                             extractor.kill();
                     });
    QObject::connect(&timeout, &QTimer::timeout, &extractor, &QProcess::kill);
    QObject::connect(&extractor, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &loop, &QEventLoop::quit);
    QObject::connect(&extractor, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
    extractor.start(program, arguments);
    timer.start();
    timeout.start(120000);

    if (extractor.state() != QProcess::NotRunning)
        loop.exec();

    checkpoint(cancelled);

    require(extractor.state() == QProcess::NotRunning && extractor.exitStatus() == QProcess::NormalExit &&
                extractor.exitCode() == 0,
            "Sample ZIP extraction failed: " + QString::fromLocal8Bit(extractor.readAllStandardError()));
    require(validSample(staging.path(), assets), "Extracted sample files failed SHA-256 verification.");

    if (QFileInfo::exists(folder))
        require(QDir().rename(folder, folder + ".invalid-" + QString::number(QDateTime::currentMSecsSinceEpoch())),
                "Cannot preserve the old sample cache.");
    require(QDir().rename(staging.path(), folder), "Cannot install the verified sample data.");
    staging.setAutoRemove(false);

    return folder;
}

constexpr float NoData = -9999.f;
constexpr const char* Method = "native-multiscale-morphological-tin-v1";
using Values = QVector<float>;

struct Raster
{
    QString path, projection, sha256;
    int width = 0, height = 0;
    double pixel = 0;
    QVector<double> transform;
    Values z;
    QVector<uchar> valid;
    GisExtent extent() const
    {
        return GisExtent(transform[0], transform[3] - height * pixel, transform[0] + width * pixel, transform[3]);
    }
};

void configureRuntime(const QString& sample)
{
    const QDir bin(QCoreApplication::applicationDirPath());
    const QString sdk = bin.filePath("proj-data");
    const QString proj = QFileInfo::exists(QDir(sdk).filePath("proj.db")) ? sdk : QDir(sample).filePath("proj-data");
    require(QFileInfo::exists(QDir(proj).filePath("proj.db")), "A matching PROJ database is required.");
    qputenv("PROJ_DATA", QDir::toNativeSeparators(proj).toUtf8());
    qputenv("PROJ_LIB", QDir::toNativeSeparators(proj).toUtf8());
    qputenv("PROJ_NETWORK", "OFF");
    if (QDir(bin.filePath("gdal-data")).exists())
        qputenv("GDAL_DATA", QDir::toNativeSeparators(bin.filePath("gdal-data")).toUtf8());
}

// C API bridge to the GDAL runtime shipped with the public SDK.
class RasterRuntime
{
public:
    RasterRuntime()
    {
#ifdef Q_OS_WIN
        library_.setFileName(QDir(QCoreApplication::applicationDirPath()).filePath("gdal"));
#else
        library_.setFileName("gdal");
#endif
        library_.setLoadHints(QLibrary::PreventUnloadHint);
        require(library_.load(), "Cannot load SDK GDAL: " + library_.errorString());
        function<void (*)()>("GDALAllRegister")();
        require(std::atoi(function<const char* (*)(const char*)>("GDALVersionInfo")("VERSION_NUM")) >= 3060000,
                "The gridding bridge requires GDAL 3.6 or newer.");
    }
    template <class T> T function(const char* name)
    {
        auto address = library_.resolve(name);
        require(address, QString("SDK GDAL entry point missing: %1").arg(name));
        return reinterpret_cast<T>(address);
    }
    void validateHeight(const QString& path)
    {
        auto open = function<void* (*)(const char*, int)>("GDALOpen");
        auto close = function<void (*)(void*)>("GDALClose");
        const QByteArray bytes = path.toUtf8();
        std::unique_ptr<void, void (*)(void*)> dataset(open(bytes.constData(), 0), close);
        require(bool(dataset), "Cannot open height raster.");
        require(function<int (*)(void*)>("GDALGetRasterCount")(dataset.get()) == 1, "Use a single-band height raster.");
        auto* band = function<void* (*)(void*, int)>("GDALGetRasterBand")(dataset.get(), 1);
        int present = 0;
        const double scale = function<double (*)(void*, int*)>("GDALGetRasterScale")(band, &present);
        require(!present || std::abs(scale - 1) < 1e-12, "Apply raster scale to metre heights before opening.");
        const double offset = function<double (*)(void*, int*)>("GDALGetRasterOffset")(band, &present);
        require(!present || std::abs(offset) < 1e-12, "Apply raster offset before opening.");
        const QString units =
            QString::fromUtf8(function<const char* (*)(void*)>("GDALGetRasterUnitType")(band)).toLower();
        require(units.isEmpty() || units == "m" || units == "metre" || units == "meter" || units == "metres" ||
                    units == "meters",
                "Height units must be metres. Unspecified units are assumed to be metres.");
    }
    QVector<uchar> readMask(const QString& path, int width, int height)
    {
        auto open = function<void* (*)(const char*, int)>("GDALOpen");
        auto close = function<void (*)(void*)>("GDALClose");
        const auto bytes = path.toUtf8();
        std::unique_ptr<void, void (*)(void*)> dataset(open(bytes.constData(), 0), close);
        require(bool(dataset), "Cannot read source validity mask.");
        auto* band = function<void* (*)(void*, int)>("GDALGetRasterBand")(dataset.get(), 1);
        auto* mask = function<void* (*)(void*)>("GDALGetMaskBand")(band);
        QVector<uchar> pixels(width * height);
        require(mask && function<int (*)(void*, int, int, int, int, int, void*, int, int, int, int, int)>(
                            "GDALRasterIO")(mask, 0, 0, 0, width, height, pixels.data(), width, height, 1, 0, 0) == 0,
                "Cannot read source validity pixels.");
        return pixels;
    }
    void write(const Raster& raster, const QString& path, const void* data, int type, const QImage* image = nullptr)
    {
        auto driver = function<void* (*)(const char*)>("GDALGetDriverByName");
        auto create = function<void* (*)(void*, const char*, int, int, int, int, char**)>("GDALCreate");
        auto close = function<void (*)(void*)>("GDALClose");
        auto band = function<void* (*)(void*, int)>("GDALGetRasterBand");
        auto io = function<int (*)(void*, int, int, int, int, int, void*, int, int, int, int, int)>("GDALRasterIO");
        char* options[] = {const_cast<char*>("COMPRESS=DEFLATE"), const_cast<char*>("TILED=YES"), nullptr};
        const QByteArray filename = path.toUtf8(), projection = raster.projection.toUtf8();
        std::unique_ptr<void, void (*)(void*)> dataset(
            create(driver("GTiff"), filename.constData(), raster.width, raster.height, image ? 3 : 1, type, options),
            close);
        require(bool(dataset), "Cannot create GeoTIFF: " + path);
        auto transform = raster.transform;
        require(function<int (*)(void*, double*)>("GDALSetGeoTransform")(dataset.get(), transform.data()) == 0 &&
                    function<int (*)(void*, const char*)>("GDALSetProjection")(dataset.get(), projection.constData()) ==
                        0,
                "Cannot write raster coordinates.");
        function<int (*)(void*, const char*, const char*, const char*)>("GDALSetMetadataItem")(
            dataset.get(), "GEOKERNEL_TERRAIN_METHOD", Method, nullptr);
        for (int channel = 0; channel < (image ? 3 : 1); ++channel)
        {
            auto* output = band(dataset.get(), channel + 1);
            if (image)
                function<int (*)(void*, int)>("GDALSetRasterColorInterpretation")(output, channel + 3);
            else
            {
                function<int (*)(void*, double)>("GDALSetRasterNoDataValue")(output, type == 1 ? 1. : NoData);
                if (type == 6)
                    function<int (*)(void*, const char*)>("GDALSetRasterUnitType")(output, "metre");
            }
            void* pixels = image ? const_cast<uchar*>(image->constBits()) + channel : const_cast<void*>(data);
            require(io(output, 1, 0, 0, raster.width, raster.height, pixels, raster.width, raster.height, type,
                       image ? 3 : 0, image ? image->bytesPerLine() : 0) == 0,
                    "Cannot write raster pixels.");
        }
    }
    Values triangulate(const Raster& raster, const std::vector<double>& x, const std::vector<double>& y,
                       const std::vector<double>& z, std::atomic_bool& cancelled, const Progress& progress)
    {
        // GDALGridLinearOptions layout, versioned by size since GDAL 3.6.
        struct LinearOptions
        {
            size_t size;
            double radius;
            double nodata;
        };
        const LinearOptions options{sizeof(LinearOptions), 0., NoData};
        auto create = function<void* (*)(int, const void*, unsigned, const double*, const double*, const double*, int)>(
            "GDALGridContextCreate");
        auto free = function<void (*)(void*)>("GDALGridContextFree");
        checkpoint(cancelled);
        progress(62, QString("Triangulating %1 ground supports...").arg(z.size()));
        std::unique_ptr<void, void (*)(void*)> context(
            create(10, &options, unsigned(z.size()), x.data(), y.data(), z.data(), 1), free);
        checkpoint(cancelled);
        require(bool(context),
                "GDAL could not triangulate ground supports; check candidate distribution and SDK Qhull support.");
        struct Callback
        {
            std::atomic_bool& cancelled;
            const Progress& progress;
        } callback{cancelled, progress};
        const auto tick = +[](double fraction, const char*, void* user) -> int
        {
            auto& state = *static_cast<Callback*>(user);
            state.progress(65 + int(fraction * 16), "Interpolating native source pixel centres...");
            return !state.cancelled;
        };
        auto process = function<int (*)(void*, double, double, double, double, unsigned, unsigned, int, void*,
                                        int (*)(double, const char*, void*), void*)>("GDALGridContextProcess");
        Values result(raster.z.size(), NoData);
        // Local x=column metres, y=row metres; output GeoTIFF keeps the real CRS.
        const int status = process(context.get(), 0, raster.width * raster.pixel, 0, raster.height * raster.pixel,
                                   unsigned(raster.width), unsigned(raster.height), 6, result.data(), tick, &callback);
        checkpoint(cancelled);
        require(status == 0, "TIN interpolation failed.");
        return result;
    }

private:
    QLibrary library_;
};

std::shared_ptr<Raster> readRaster(const QString& path, const std::atomic_bool& cancelled)
{
    checkpoint(cancelled);
    RasterRuntime runtime;
    runtime.validateHeight(path);
    GdalElevationProvider provider;
    QString error;
    require(provider.open(path, 1, &error), error);
    const auto m = provider.metadata();
    const auto& t = m.worldTransform;
    const auto crs = CoordinateSystemFactory::fromUserInput(m.projectionWkt);
    require(crs && crs->isProjected() && crs->metersPerUnit() && std::abs(*crs->metersPerUnit() - 1.) < 1e-9,
            "Use a projected CRS with metre XY units.");
    require(m.hasWorldTransform && t.rotationX == 0 && t.rotationY == 0 && t.pixelSizeX > 0 &&
                std::abs(t.pixelSizeX + t.pixelSizeY) < 1e-9,
            "Use a north-up raster with square pixels.");
    require(m.width >= 8 && m.height >= 8 && qint64(m.width) * m.height <= 4000000,
            "This interactive example supports 8 x 8 to 4 million source cells.");
    auto raster = std::make_shared<Raster>();
    raster->path = QFileInfo(path).absoluteFilePath();
    raster->projection = m.projectionWkt;
    raster->width = m.width;
    raster->height = m.height;
    raster->pixel = t.pixelSizeX;
    raster->transform = {t.upperLeftCenterX - t.pixelSizeX * .5,
                         t.pixelSizeX,
                         0,
                         t.upperLeftCenterY - t.pixelSizeY * .5,
                         0,
                         t.pixelSizeY};
    raster->z = provider.readAll(&error);
    require(raster->z.size() == m.width * m.height, "Cannot read complete heights: " + error);
    raster->valid.resize(raster->z.size());
    const auto mask = runtime.readMask(path, m.width, m.height);
    int count = 0;
    for (int i = 0; i < raster->z.size(); ++i)
    {
        raster->valid[i] =
            mask[i] && std::isfinite(raster->z[i]) && (!m.hasNoDataValue || raster->z[i] != m.noDataValue);
        count += raster->valid[i];
    }
    require(count >= 4 && count >= raster->z.size() * .05, "Insufficient valid source coverage.");
    checkpoint(cancelled);
    raster->sha256 = QString::fromLatin1(hashFile(path));
    return raster;
}

void validateReference(const Raster& dsm, const Raster& reference)
{
    const auto a = CoordinateSystemFactory::fromUserInput(dsm.projection);
    const auto b = CoordinateSystemFactory::fromUserInput(reference.projection);
    require(a && b && CoordinateSystemFactory::areEquivalent(*a, *b) && dsm.width == reference.width &&
                dsm.height == reference.height && dsm.transform == reference.transform,
            "Reference must share the DSM's exact dimensions, grid and equivalent CRS; no implicit resampling.");
}

struct Parameters
{
    double working = 2., window = 64., threshold = .4, slope = .12, terrain = .5, retain = .35;
    QJsonObject json() const
    {
        return {{"requested_working_m", working}, {"maximum_window_m", window},      {"base_threshold_m", threshold},
                {"slope_allowance", slope},       {"terrain_slope_factor", terrain}, {"retain_tolerance_m", retain},
                {"roughness_scale_m", .6},        {"maximum_base_threshold_m", 3.}};
    }
};
int reflect(int index, int count)
{
    while (index < 0 || index >= count)
        index = index < 0 ? -index - 1 : 2 * count - index - 1;
    return index;
}
Values extremes(const Values& input, int width, int height, int radius, bool minimum, const std::atomic_bool& cancelled)
{
    Values horizontal(input.size()), output(input.size());
    const auto pass = [&](const Values& source, Values& target, bool vertical)
    {
        const int lines = vertical ? width : height, length = vertical ? height : width;
        for (int line = 0; line < lines; ++line)
        {
            checkpoint(cancelled);
            std::deque<std::pair<int, float>> queue;
            for (int k = -radius; k < length + radius; ++k)
            {
                const int at = reflect(k, length);
                const float value = source[vertical ? at * width + line : line * width + at];
                while (!queue.empty() && (minimum ? queue.back().second >= value : queue.back().second <= value))
                    queue.pop_back();
                queue.emplace_back(k, value);
                while (queue.front().first < k - radius * 2)
                    queue.pop_front();
                const int destination = k - radius;
                if (destination >= 0)
                    target[vertical ? destination * width + line : line * width + destination] = queue.front().second;
            }
        }
    };
    pass(input, horizontal, false);
    pass(horizontal, output, true);
    return output;
}
Values gaussian(const Values& input, int width, int height, double sigma, const std::atomic_bool& cancelled)
{
    const int radius = int(std::ceil(3 * sigma));
    std::vector<double> kernel(radius * 2 + 1);
    double sum = 0;
    for (int k = -radius; k <= radius; ++k)
        sum += kernel[k + radius] = std::exp(-.5 * k * k / (sigma * sigma));
    for (auto& value : kernel)
        value /= sum;
    Values temporary(input.size()), output(input.size());
    for (int y = 0; y < height; ++y)
    {
        checkpoint(cancelled);
        for (int x = 0; x < width; ++x)
        {
            double value = 0;
            for (int k = -radius; k <= radius; ++k)
                value += input[y * width + reflect(x + k, width)] * kernel[k + radius];
            temporary[y * width + x] = float(value);
        }
    }
    for (int y = 0; y < height; ++y)
    {
        checkpoint(cancelled);
        for (int x = 0; x < width; ++x)
        {
            double value = 0;
            for (int k = -radius; k <= radius; ++k)
                value += temporary[reflect(y + k, height) * width + x] * kernel[k + radius];
            output[y * width + x] = float(value);
        }
    }
    return output;
}
Values gradient(const Values& z, int width, int height, double pixel)
{
    Values result(z.size());
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const int l = std::max(0, x - 1), r = std::min(width - 1, x + 1);
            const int t = std::max(0, y - 1), b = std::min(height - 1, y + 1);
            result[y * width + x] = float(std::hypot((z[y * width + r] - z[y * width + l]) / ((r - l) * pixel),
                                                     (z[b * width + x] - z[t * width + x]) / ((b - t) * pixel)));
        }
    return result;
}
// Four-neighbour propagation fills working-grid holes and flagged hull fallbacks.
QVector<int> supportMap(const QVector<uchar>& valid, int width, int height, const std::atomic_bool& cancelled)
{
    QVector<int> nearest(valid.size(), -1);
    std::vector<int> queue;
    queue.reserve(size_t(valid.size()));
    for (int i = 0; i < valid.size(); ++i)
        if (valid[i])
        {
            nearest[i] = i;
            queue.push_back(i);
        }
    require(!queue.empty(), "No support cells.");
    for (size_t head = 0; head < queue.size(); ++head)
    {
        if (!(head % 4096))
            checkpoint(cancelled);
        const int i = queue[head], x = i % width, y = i / width;
        for (int n : {x ? i - 1 : -1, x + 1 < width ? i + 1 : -1, y ? i - width : -1, y + 1 < height ? i + width : -1})
            if (n >= 0 && nearest[n] < 0)
            {
                nearest[n] = nearest[i];
                queue.push_back(n);
            }
    }
    return nearest;
}
struct Terrain
{
    Parameters parameters;
    double working = 0, seconds = 0;
    int supports = 0;
    Values dtm, removed;
    QVector<uchar> quality;
    QJsonObject summary;
};
Terrain estimate(const Raster& dsm, Parameters p, std::atomic_bool& cancelled, const Progress& progress)
{
    QElapsedTimer elapsed;
    elapsed.start();
    Terrain result;
    result.parameters = p;
    const int factor = std::max(1, int(std::lround(p.working / dsm.pixel)));
    const double working = factor * dsm.pixel;
    require(p.window >= 2 * working, "Maximum window must span at least two working cells.");
    result.working = working;
    const int width = (dsm.width + factor - 1) / factor, height = (dsm.height + factor - 1) / factor;
    require(width >= 3 && height >= 3 && qint64(width) * height <= 262144,
            "Working grid must have at least 3 x 3 and at most 262144 cells. Adjust working resolution.");
    const int count = width * height;
    Values low(count, std::numeric_limits<float>::infinity());
    QVector<int> minima(count, -1);
    QVector<uchar> valid(count, 0), candidates(count, 0);
    for (int y = 0; y < dsm.height; ++y)
    {
        checkpoint(cancelled);
        for (int x = 0; x < dsm.width; ++x)
        {
            const int source = y * dsm.width + x, cell = (y / factor) * width + x / factor;
            if (dsm.valid[source] && dsm.z[source] < low[cell])
            {
                low[cell] = dsm.z[source];
                minima[cell] = source;
                valid[cell] = 1;
            }
        }
    }
    const auto fill = supportMap(valid, width, height, cancelled);
    for (int i = 0; i < count; ++i)
        if (!valid[i])
            low[i] = low[fill[i]];
    candidates = valid;
    const auto radiusFor = [&](double window)
    {
        return std::max(1, int(std::lround(window / working)) / 2);
    };
    const auto opening = [&](const Values& z, int radius)
    {
        return extremes(extremes(z, width, height, radius, true, cancelled), width, height, radius, false, cancelled);
    };
    progress(15, "Estimating DSM-derived slope and roughness...");
    const auto envelope = opening(low, radiusFor(p.window));
    const auto terrainSlope = gaussian(gradient(envelope, width, height, working), width, height, 1, cancelled);
    const auto smoothed = gaussian(low, width, height, 1, cancelled);
    const auto localSlope = extremes(gradient(smoothed, width, height, working), width, height, 2, false, cancelled);
    Values highPass(count);
    for (int i = 0; i < count; ++i)
        highPass[i] = (low[i] - smoothed[i]) * (low[i] - smoothed[i]);
    const auto variance = gaussian(highPass, width, height, 2, cancelled);
    std::vector<double> windows;
    for (double window = std::max(working * 2., 4.); window < p.window; window *= 2.)
        windows.push_back(window);
    windows.push_back(p.window);
    for (size_t step = 0; step < windows.size(); ++step)
    {
        const double window = windows[step];
        progress(20 + int(35. * step / windows.size()), QString("Ground candidates: %1 m window").arg(window));
        const auto opened = opening(low, radiusFor(window));
        const auto slope = extremes(terrainSlope, width, height, radiusFor(window), false, cancelled);
        for (int i = 0; i < count; ++i)
        {
            const double weight = 1. / (1. + std::pow(std::sqrt(std::max(0.f, variance[i])) / .6, 4));
            const double threshold = std::min(3., p.threshold + p.slope * window) +
                                     p.terrain * window * std::min(slope[i], localSlope[i]) * weight;
            candidates[i] = candidates[i] && low[i] - opened[i] <= threshold;
        }
    }
    std::vector<double> x, y, z;
    for (int i = 0; i < count; ++i)
        if (candidates[i])
        {
            const int source = minima[i];
            x.push_back((source % dsm.width + .5) * dsm.pixel);
            y.push_back((source / dsm.width + .5) * dsm.pixel);
            z.push_back(dsm.z[source]);
        }
    require(z.size() >= 4, "Not enough ground supports; relax parameters.");
    result.supports = int(z.size());
    const auto nearest = supportMap(candidates, width, height, cancelled);
    RasterRuntime runtime;
    result.dtm = runtime.triangulate(dsm, x, y, z, cancelled, progress);
    result.removed.resize(dsm.z.size());
    result.quality.resize(dsm.z.size());
    qint64 retained = 0, fallback = 0, clamped = 0, borderCount = 0;
    const int border = int(std::ceil(p.window * .5 / dsm.pixel));
    for (int row = 0; row < dsm.height; ++row)
    {
        checkpoint(cancelled);
        for (int col = 0; col < dsm.width; ++col)
        {
            const int i = row * dsm.width + col, cell = (row / factor) * width + col / factor;
            uchar qa = 0;
            if (!dsm.valid[i])
            {
                result.dtm[i] = result.removed[i] = NoData;
                result.quality[i] = 1;
                continue;
            }
            if (result.dtm[i] == NoData || !std::isfinite(result.dtm[i]))
            {
                result.dtm[i] = low[nearest[cell]];
                qa |= 4;
                ++fallback;
            }
            if (candidates[cell] && std::abs(dsm.z[i] - result.dtm[i]) <= p.retain)
            {
                result.dtm[i] = dsm.z[i];
                ++retained;
            }
            else
                qa |= 2;
            if (result.dtm[i] > dsm.z[i])
            {
                result.dtm[i] = dsm.z[i];
                qa |= 16;
                ++clamped;
            }
            if (row < border || col < border || row >= dsm.height - border || col >= dsm.width - border)
            {
                qa |= 8;
                ++borderCount;
            }
            result.removed[i] = std::max(0.f, dsm.z[i] - result.dtm[i]);
            result.quality[i] = qa;
        }
    }
    result.seconds = elapsed.elapsed() / 1000.;
    result.summary = {{"method", Method},
                      {"effective_working_m", working},
                      {"support_points", result.supports},
                      {"retained_source_pixels", double(retained)},
                      {"outside_hull_fallback_pixels", double(fallback)},
                      {"upper_bound_clamped_pixels", double(clamped)},
                      {"edge_sensitive_pixels", double(borderCount)},
                      {"edge_band_m", p.window * .5},
                      {"compute_seconds", result.seconds}};
    progress(84, "Preparing numeric GeoTIFFs and display maps...");
    return result;
}
QImage relief(const Raster& raster, const Values& z, const std::atomic_bool& cancelled)
{
    QImage image(raster.width, raster.height, QImage::Format_RGB888);
    const double az = 315. * 3.141592653589793 / 180., alt = 40. * 3.141592653589793 / 180.;
    const auto at = [&](int x, int y, float fallback)
    {
        const int i = y * raster.width + x;
        return raster.valid[i] ? z[i] : fallback;
    };
    for (int y = 0; y < raster.height; ++y)
    {
        checkpoint(cancelled);
        for (int x = 0; x < raster.width; ++x)
        {
            const int i = y * raster.width + x;
            if (!raster.valid[i])
            {
                image.setPixelColor(x, y, QColor("#75818c"));
                continue;
            }
            const int l = std::max(0, x - 1), r = std::min(raster.width - 1, x + 1), t = std::max(0, y - 1),
                      b = std::min(raster.height - 1, y + 1);
            const double dx = (at(r, y, z[i]) - at(l, y, z[i])) / ((r - l) * raster.pixel),
                         dy = (at(x, t, z[i]) - at(x, b, z[i])) / ((b - t) * raster.pixel);
            const double light =
                (-dx * std::sin(az) * std::cos(alt) - dy * std::cos(az) * std::cos(alt) + std::sin(alt)) /
                std::sqrt(dx * dx + dy * dy + 1.);
            const int gray = std::clamp(int(std::lround(255 * (.25 + .75 * std::max(0., light)))), 0, 255);
            image.setPixelColor(x, y, QColor(gray, gray, gray));
        }
    }
    return image;
}
struct Score
{
    qint64 count = 0;
    double rmse = 0, mae = 0, bias = 0;
    QJsonObject json() const
    {
        return {{"pixels", double(count)},
                {"rmse_m", count ? QJsonValue(rmse) : QJsonValue()},
                {"mae_m", count ? QJsonValue(mae) : QJsonValue()},
                {"bias_m", count ? QJsonValue(bias) : QJsonValue()}};
    }
};
Score score(const Raster& dsm, const Values& result, const Raster* reference, int border = 0)
{
    Score m;
    if (!reference)
        return m;
    for (int y = border; y < dsm.height - border; ++y)
        for (int x = border; x < dsm.width - border; ++x)
        {
            const int i = y * dsm.width + x;
            if (dsm.valid[i] && reference->valid[i])
            {
                const double e = double(result[i]) - reference->z[i];
                ++m.count;
                m.rmse += e * e;
                m.mae += std::abs(e);
                m.bias += e;
            }
        }
    if (m.count)
    {
        m.rmse = std::sqrt(m.rmse / m.count);
        m.mae /= m.count;
        m.bias /= m.count;
    }
    return m;
}
QColor blend(QColor a, QColor b, double t)
{
    t = std::clamp(t, 0., 1.);
    return QColor(int(std::lround(a.red() + (b.red() - a.red()) * t)),
                  int(std::lround(a.green() + (b.green() - a.green()) * t)),
                  int(std::lround(a.blue() + (b.blue() - a.blue()) * t)));
}
QImage diagnostic(const Raster& dsm, const Terrain& result, int mode, const Raster* reference,
                  const std::atomic_bool& cancelled)
{
    QImage image(dsm.width, dsm.height, QImage::Format_RGB888);
    for (int y = 0; y < dsm.height; ++y)
    {
        checkpoint(cancelled);
        for (int x = 0; x < dsm.width; ++x)
        {
            const int i = y * dsm.width + x;
            QColor color("#75818c");
            if (dsm.valid[i])
            {
                if (mode == 0)
                    color = blend(QColor("#fafafa"), QColor("#bc3e31"), result.removed[i] / 20.);
                else if (mode == 1)
                {
                    const auto qa = result.quality[i];
                    color = QColor(qa & 4 ? "#a743a6" : qa & 8 ? "#e8ad49" : qa & 2 ? "#edf3f5" : "#15969b");
                }
                else if (reference && reference->valid[i])
                {
                    const double error = double(result.dtm[i]) - reference->z[i];
                    color = error < 0 ? blend(QColor("#fafafa"), QColor("#2464b5"), -error / 2.)
                                      : blend(QColor("#fafafa"), QColor("#c93645"), error / 2.);
                }
            }
            image.setPixelColor(x, y, color);
        }
    }
    return image;
}
struct Scene
{
    std::shared_ptr<Raster> dsm, reference;
    QString original;
    QJsonObject manifest;
};
struct Run
{
    Terrain terrain;
    QString dtm, removed, quality, display;
    std::array<QString, 3> diagnostics;
    Score all, interior;
    QJsonObject report;
};
struct Outcome
{
    std::shared_ptr<Scene> scene;
    std::shared_ptr<Run> run;
    QString error, message;
};
QJsonObject reportFor(const Scene& scene, const Run& run)
{
    QJsonArray transform;
    for (double v : scene.dsm->transform)
        transform.append(v);
    return {{"schema_version", 1},
            {"method", Method},
            {"parameters", run.terrain.parameters.json()},
            {"summary", run.terrain.summary},
            {"source_dsm", scene.dsm->path},
            {"source_sha256", scene.dsm->sha256},
            {"reference", scene.reference ? QJsonValue(scene.reference->path) : QJsonValue()},
            {"reference_sha256", scene.reference ? QJsonValue(scene.reference->sha256) : QJsonValue()},
            {"reference_used_for_processing", false},
            {"grid", QJsonObject{{"width", scene.dsm->width},
                                 {"height", scene.dsm->height},
                                 {"geotransform", transform},
                                 {"crs_wkt", scene.dsm->projection}}},
            {"source_manifest", scene.manifest},
            {"all_valid_reference_error", run.all.json()},
            {"interior_reference_error", run.interior.json()},
            {"edge_band_definition", "Half the requested maximum window, rounded up to source cells"},
            {"quality_bits", QJsonObject{{"1", "Source NoData"},
                                         {"2", "Interpolated (not explicitly retained source sample)"},
                                         {"4", "Outside TIN hull: grid-support fallback"},
                                         {"8", "Edge-sensitive band"},
                                         {"16", "Prediction clamped to DSM"}}},
            {"height_units", "metre; unspecified source units assumed metres; no vertical datum conversion"},
            {"processing_scope",
             "This input raster only. Supply surrounding DSM context and crop afterward to reduce boundary effects."}};
}
void writeJson(const QString& path, const QJsonObject& object)
{
    QSaveFile file(path);
    const auto bytes = QJsonDocument(object).toJson();
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(),
            "Cannot save JSON: " + path);
}
void atomicCopy(const QString& source, const QString& target, std::atomic_bool& cancelled)
{
    QFile input(source);
    QSaveFile output(target);
    require(input.open(QIODevice::ReadOnly) && output.open(QIODevice::WriteOnly), "Cannot open export: " + target);
    while (!input.atEnd())
    {
        checkpoint(cancelled);
        const auto block = input.read(1024 * 1024);
        require(!block.isEmpty() && output.write(block) == block.size(), "Export write failed.");
    }
    checkpoint(cancelled);
    require(output.commit(), "Cannot commit export: " + target);
}
class ProbeOverlay final : public QWidget
{
public:
    GisViewer* view;
    std::optional<QPointF> point;
    explicit ProbeOverlay(GisViewer* parent) : QWidget(parent), view(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (point)
        {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setBrush(QColor("#d73573"));
            painter.setPen(QPen(Qt::white, 2));
            painter.drawEllipse(view->worldToScreen(GisShapePoint(point->x(), point->y())), 5, 5);
        }
    }
};
class ProcessingWindow final : public QMainWindow
{
public:
    explicit ProcessingWindow(const QCommandLineParser& arguments)
        : arguments_(arguments), assets_(readJson(":/sample-assets.json"))
    {
        setWindowTitle("DSM to DTM | Live classical processing — GeoKernel");
        resize(1720, 960);
        setWindowIcon(QIcon(":/icons/geokernel.ico"));
        setStyleSheet(
            "QMainWindow,QWidget#page{background:#f2f6f8;color:#233e50;} QLabel#title{font-size:22px;font-weight:600;color:#145e72;} QLabel#heading{background:#e4eef2;color:#125a70;padding:9px;font-weight:600;} QPushButton{padding:7px;} QToolBar{background:white;border-bottom:1px solid #d5e2e9;spacing:8px;padding:7px;}");
        auto* page = new QWidget;
        page->setObjectName("page");
        auto* layout = new QHBoxLayout(page);
        auto* sidebar = new QWidget;
        auto* controls = new QVBoxLayout(sidebar);
        controls->setContentsMargins(0, 0, 8, 0);
        controls->setSpacing(9);
        const auto label = [](QString text, const char* name = nullptr)
        {
            auto* item = new QLabel(text);
            item->setWordWrap(true);
            if (name)
                item->setObjectName(name);
            return item;
        };
        controls->addWidget(label("DSM → DTM\nRun the conversion", "title"));
        controls->addWidget(label("Live multiscale filtering + TIN.\nChange parameters, then Run."));
        auto* sample = button("Load Zurich sample", "loadSample", controls,
                              [this]
                              {
                                  loadSample();
                              });
        auto* open = button("Open DSM GeoTIFF...", "openDsm", controls,
                            [this]
                            {
                                openDsm();
                            });
        auto* reference = button("Open reference (optional)...", "openReference", controls,
                                 [this]
                                 {
                                     openReference();
                                 });
        referenceButton_ = reference;
        source_ = label("No source loaded.");
        source_->setObjectName("sourceInfo");
        source_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        controls->addWidget(source_);
        auto* presets = new QComboBox;
        presets->setObjectName("processingPreset");
        presets->addItems({"Balanced", "Remove more objects", "Preserve more relief"});
        controls->addWidget(presets);
        auto* form = new QFormLayout;
        const auto parameter =
            [&](QString title, QString name, double minimum, double maximum, double value, double step, int decimals)
        {
            auto* spin = new QDoubleSpinBox;
            spin->setObjectName(name);
            spin->setRange(minimum, maximum);
            spin->setDecimals(decimals);
            spin->setSingleStep(step);
            spin->setValue(value);
            form->addRow(title, spin);
            parameters_.push_back(spin);
            connect(
                spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this]
                {
                    if (run_)
                        state_->setText(
                            "Controls changed. Run to update; displayed result and scores still use the last completed run.");
                });
            return spin;
        };
        working_ = parameter("Working grid (m)", "workingResolution", .25, 20, 2, .5, 2);
        window_ = parameter("Max. window (m)", "maximumWindow", 4, 200, 64, 4, 1);
        threshold_ = parameter("Base threshold (m)", "baseThreshold", .05, 3, .4, .05, 2);
        slope_ = parameter("Slope allowance", "slopeAllowance", 0, .5, .12, .02, 2);
        terrain_ = parameter("Relief allowance", "terrainAllowance", 0, 2, .5, .1, 2);
        retain_ = parameter("Keep tolerance (m)", "retainTolerance", 0, 1, .35, .05, 2);
        controls->addLayout(form);
        connect(presets, qOverload<int>(&QComboBox::activated), this,
                [this](int index)
                {
                    const std::array<Parameters, 3> p = {Parameters{}, Parameters{1, 80, .25, .08, .25, .2},
                                                         Parameters{2, 48, .6, .16, .8, .4}};
                    const auto v = p[index];
                    working_->setValue(v.working);
                    window_->setValue(v.window);
                    threshold_->setValue(v.threshold);
                    slope_->setValue(v.slope);
                    terrain_->setValue(v.terrain);
                    retain_->setValue(v.retain);
                });
        runButton_ = button("Run DSM → DTM", "runProcessing", controls,
                            [this]
                            {
                                process();
                            });
        cancel_ = button("Cancel", "cancelProcessing", controls,
                         [this]
                         {
                             cancelled_ = true;
                             cancel_->setEnabled(false);
                             setPhase(progressValue_, "Cancelling; completed result will be retained...");
                         });
        state_ = label("Load a DSM to begin.");
        state_->setObjectName("runState");
        controls->addWidget(state_);
        controls->addWidget(label("Right map"));
        mode_ = new QComboBox;
        mode_->setObjectName("diagnosticMode");
        mode_->addItems({"Removed height: DSM − DTM", "Support / edge quality", "Signed reference error"});
        controls->addWidget(mode_);
        connect(mode_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this]
                {
                    showDiagnostic();
                });
        metrics_ = label("Reference errors appear after a completed run.");
        metrics_->setObjectName("metricSummary");
        controls->addWidget(metrics_);
        save_ = button("Save result GeoTIFFs...", "saveResult", controls,
                       [this]
                       {
                           saveResult();
                       });
        probe_ = label("Click any map to inspect source heights.");
        probe_->setObjectName("pixelProbe");
        probe_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        controls->addWidget(probe_);
        controls->addStretch();
        controls->addWidget(label(
            "Sample: Federal Office of Topography swisstopo.\nReference is evaluation-only. Heights are metres; no vertical datum conversion."));
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setFixedWidth(310);
        scroll->setWidget(sidebar);
        layout->addWidget(scroll);
        auto* workspace = new QVBoxLayout;
        auto* maps = new QHBoxLayout;
        maps->setSpacing(8);
        const QStringList titles = {"ORIGINAL DSM · input", "PROCESSED DTM · Run to generate",
                                    "REMOVED HEIGHT · Run to generate"};
        for (int pane = 0; pane < 3; ++pane)
        {
            auto* column = new QVBoxLayout;
            headings_[pane] = label(titles[pane], "heading");
            column->addWidget(headings_[pane]);
            holders_[pane] = new QVBoxLayout;
            column->addLayout(holders_[pane], 1);
            maps->addLayout(column, 1);
        }
        workspace->addLayout(maps, 1);
        legend_ = label("Original DSM remains visible. Both relief maps use identical lighting and contrast.");
        workspace->addWidget(legend_);
        summary_ = label("No processing has run yet.");
        summary_->setObjectName("processingSummary");
        workspace->addWidget(summary_);
        workspace->addWidget(label(
            "Edge-sensitive band = half the maximum filter window. Use a larger DSM and crop afterward for better context. This prototype is independent of ArcGIS Terrain Filter."));
        layout->addLayout(workspace, 1);
        setCentralWidget(page);
        auto* toolbar = addToolBar("Navigation");
        toolbar->setMovable(false);
        toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        const auto action = [&](QString title, QString icon, auto fn)
        {
            auto* a = toolbar->addAction(QIcon(icon), title);
            connect(a, &QAction::triggered, this, fn);
            return a;
        };
        navigation_.push_back(action("Pan", ":/icons/pan.svg",
                                     [this]
                                     {
                                         tool(GisViewerTool::Pan);
                                     }));
        navigation_.push_back(action("Zoom box", ":/icons/zoom-box.svg",
                                     [this]
                                     {
                                         tool(GisViewerTool::ZoomBox);
                                     }));
        navigation_.push_back(action("Zoom in", ":/icons/zoom-in.svg",
                                     [this]
                                     {
                                         if (views_[0])
                                             views_[0]->zoomIn(1.5);
                                     }));
        navigation_.push_back(action("Zoom out", ":/icons/zoom-out.svg",
                                     [this]
                                     {
                                         if (views_[0])
                                             views_[0]->zoomOut(1.5);
                                     }));
        navigation_.push_back(action("Fit scene", ":/icons/full-extent.svg",
                                     [this]
                                     {
                                         fit();
                                     }));
        progress_ = new QProgressBar;
        progress_->setFixedWidth(230);
        statusBar()->addPermanentWidget(progress_);
        controls_ = {sample, open, reference, presets, mode_, save_, runButton_};
        for (auto* spin : parameters_)
            controls_.push_back(spin);
        poll_.setInterval(50);
        connect(&poll_, &QTimer::timeout, this,
                [this]
                {
                    finish();
                });
        setBusy(false);
        QTimer::singleShot(0, this,
                           [this]
                           {
                               loadSample();
                           });
    }
    ~ProcessingWindow() override
    {
        cancelled_ = true;
        poll_.stop();
        if (pending_.valid())
            pending_.wait();
        for (auto* view : views_)
            delete view;
    }

protected:
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (event->type() == QEvent::Resize)
            for (int i = 0; i < 3; ++i)
                if (object == views_[i] && overlays_[i])
                    overlays_[i]->setGeometry(views_[i]->rect());
        return QMainWindow::eventFilter(object, event);
    }
    void closeEvent(QCloseEvent* event) override
    {
        if (busy_)
        {
            closeQueued_ = true;
            cancelled_ = true;
            event->ignore();
            return;
        }
        QMainWindow::closeEvent(event);
    }

private:
    template <class F> QPushButton* button(QString title, QString name, QVBoxLayout* layout, F fn)
    {
        auto* b = new QPushButton(title);
        b->setObjectName(name);
        layout->addWidget(b);
        connect(b, &QPushButton::clicked, this, fn);
        return b;
    }
    void setPhase(int value, const QString& text)
    {
        progressValue_ = value;
        QMutexLocker lock(&phaseMutex_);
        phase_ = text;
    }
    void setBusy(bool busy)
    {
        busy_ = busy;
        for (auto* c : controls_)
            c->setEnabled(!busy);
        runButton_->setEnabled(!busy && bool(scene_));
        referenceButton_->setEnabled(!busy && bool(scene_));
        save_->setEnabled(!busy && bool(run_));
        mode_->setEnabled(!busy && bool(run_));
        cancel_->setEnabled(busy);
        progress_->setVisible(busy);
    }
    void begin(std::function<Outcome()> fn)
    {
        cancelled_ = false;
        progressValue_ = 0;
        setBusy(true);
        pending_ = std::async(std::launch::async,
                              [this, fn = std::move(fn)]
                              {
                                  try
                                  {
                                      return fn();
                                  }
                                  catch (const std::exception& e)
                                  {
                                      Outcome r;
                                      r.error = QString::fromUtf8(e.what());
                                      return r;
                                  }
                              });
        poll_.start();
    }
    QString directory()
    {
        const QString path = QDir(temporary_.path()).filePath(QString::number(++generation_));
        require(QDir().mkpath(path), "Cannot create session directory.");
        return path;
    }
    void loadSample()
    {
        if (busy_)
            return;
        cancelled_ = false;
        setBusy(true);
        try
        {
            const QString root = arguments_.isSet("data-dir")
                                     ? QFileInfo(arguments_.value("data-dir")).absoluteFilePath()
                                     : QDir(QCoreApplication::applicationDirPath()).filePath("data");
            const QUrl url(arguments_.isSet("sample-url") ? arguments_.value("sample-url")
                                                          : assets_.value("url").toString());
            const QString folder = prepareSample(assets_, root, arguments_.value("sample-archive"), url, cancelled_,
                                                 [this](int value, const QString& text)
                                                 {
                                                     progress_->setValue(value);
                                                     statusBar()->showMessage(text);
                                                 });
            checkpoint(cancelled_);
            configureRuntime(folder);
            load(QDir(folder).filePath("dsm.tif"), QDir(folder).filePath("dtm_reference.tif"),
                 readJson(QDir(folder).filePath("source-manifest.json")));
        }
        catch (const std::exception& e)
        {
            failure(QString::fromUtf8(e.what()));
        }
    }
    void openDsm()
    {
        const QString path = QFileDialog::getOpenFileName(this, "Open DSM: metre heights in projected metre CRS",
                                                          QString(), "GeoTIFF (*.tif *.tiff)");
        if (!path.isEmpty())
            load(path, QString(), {});
    }
    void load(const QString& path, const QString& reference, QJsonObject manifest)
    {
        const QString dir = directory();
        begin(
            [this, path, reference, manifest, dir]
            {
                Outcome out;
                out.scene = std::make_shared<Scene>();
                out.scene->manifest = manifest;
                setPhase(5, "Reading source DSM...");
                out.scene->dsm = readRaster(path, cancelled_);
                if (!reference.isEmpty())
                {
                    setPhase(30, "Reading evaluation-only reference...");
                    out.scene->reference = readRaster(reference, cancelled_);
                    validateReference(*out.scene->dsm, *out.scene->reference);
                }
                setPhase(60, "Preparing original DSM relief...");
                RasterRuntime runtime;
                const auto image = relief(*out.scene->dsm, out.scene->dsm->z, cancelled_);
                out.scene->original = QDir(dir).filePath("original.tif");
                runtime.write(*out.scene->dsm, out.scene->original, nullptr, 1, &image);
                checkpoint(cancelled_);
                out.message = "Source ready. Choose parameters and Run.";
                return out;
            });
    }
    void openReference()
    {
        if (!scene_)
            return;
        const QString path = QFileDialog::getOpenFileName(this, "Open aligned reference (evaluation only)", QString(),
                                                          "GeoTIFF (*.tif *.tiff)");
        if (path.isEmpty())
            return;
        const auto scene = scene_;
        const auto run = run_;
        const QString dir = directory();
        begin(
            [this, path, scene, run, dir]
            {
                Outcome out;
                out.scene = std::make_shared<Scene>(*scene);
                setPhase(10, "Reading evaluation-only reference...");
                out.scene->reference = readRaster(path, cancelled_);
                validateReference(*scene->dsm, *out.scene->reference);
                if (run)
                {
                    out.run = std::make_shared<Run>(*run);
                    evaluate(*out.scene, *out.run);
                    RasterRuntime runtime;
                    const auto image = diagnostic(*scene->dsm, run->terrain, 2, out.scene->reference.get(), cancelled_);
                    out.run->diagnostics[2] = QDir(dir).filePath("reference-error.tif");
                    runtime.write(*scene->dsm, out.run->diagnostics[2], nullptr, 1, &image);
                }
                out.message = "Reference attached; processing heights unchanged.";
                return out;
            });
    }
    void evaluate(const Scene& scene, Run& run)
    {
        run.all = score(*scene.dsm, run.terrain.dtm, scene.reference.get());
        run.interior = score(*scene.dsm, run.terrain.dtm, scene.reference.get(),
                             int(std::ceil(run.terrain.parameters.window * .5 / scene.dsm->pixel)));
        run.report = reportFor(scene, run);
    }
    void process()
    {
        if (!scene_ || busy_)
            return;
        const Parameters parameters{working_->value(), window_->value(),  threshold_->value(),
                                    slope_->value(),   terrain_->value(), retain_->value()};
        const auto scene = scene_;
        const QString dir = directory();
        begin(
            [this, scene, parameters, dir]
            {
                Outcome out;
                out.scene = scene;
                out.run = std::make_shared<Run>();
                out.run->terrain = estimate(*scene->dsm, parameters, cancelled_,
                                            [this](int p, const QString& text)
                                            {
                                                setPhase(p, text);
                                            });
                RasterRuntime runtime;
                auto& run = *out.run;
                run.dtm = QDir(dir).filePath("dtm.tif");
                run.removed = QDir(dir).filePath("removed.tif");
                run.quality = QDir(dir).filePath("quality.tif");
                runtime.write(*scene->dsm, run.dtm, run.terrain.dtm.constData(), 6);
                checkpoint(cancelled_);
                runtime.write(*scene->dsm, run.removed, run.terrain.removed.constData(), 6);
                checkpoint(cancelled_);
                runtime.write(*scene->dsm, run.quality, run.terrain.quality.constData(), 1);
                auto image = relief(*scene->dsm, run.terrain.dtm, cancelled_);
                run.display = QDir(dir).filePath("terrain-relief.tif");
                runtime.write(*scene->dsm, run.display, nullptr, 1, &image);
                for (int mode = 0; mode < 3; ++mode)
                {
                    image = diagnostic(*scene->dsm, run.terrain, mode, scene->reference.get(), cancelled_);
                    run.diagnostics[mode] = QDir(dir).filePath(QString("diagnostic-%1.tif").arg(mode));
                    runtime.write(*scene->dsm, run.diagnostics[mode], nullptr, 1, &image);
                }
                setPhase(96, "Scoring reference after processing...");
                evaluate(*scene, run);
                checkpoint(cancelled_);
                out.message = "Processing complete. Change parameters to run again, or save the result.";
                return out;
            });
    }
    void failure(const QString& error)
    {
        setBusy(false);
        statusBar()->showMessage(error);
        if (closeQueued_)
            QTimer::singleShot(0, this, &QWidget::close);
        else if (!cancelled_)
            QMessageBox::warning(this, "Operation failed", error);
    }
    void finish()
    {
        progress_->setValue(progressValue_);
        {
            QMutexLocker lock(&phaseMutex_);
            statusBar()->showMessage(phase_);
        }
        if (!pending_.valid() || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        poll_.stop();
        auto out = pending_.get();
        if (cancelled_ || !out.error.isEmpty())
        {
            failure(!out.error.isEmpty() ? out.error : "Cancelled. Last completed source/result retained.");
            return;
        }
        try
        {
            if (out.scene)
            {
                const bool changed = !scene_ || out.scene->dsm != scene_->dsm;
                scene_ = out.scene;
                run_ = out.run;
                ensureViews();
                if (changed)
                {
                    replace(*views_[0], scene_->original);
                    clear(*views_[1]);
                    clear(*views_[2]);
                    probePoint_.reset();
                    probe_->setText("Click a map to inspect source heights.");
                    for (auto* overlay : overlays_)
                    {
                        overlay->point.reset();
                        overlay->update();
                    }
                    QTimer::singleShot(0, this,
                                       [this]
                                       {
                                           fit();
                                       });
                }
                if (run_)
                {
                    replace(*views_[1], run_->display);
                    showDiagnostic();
                }
                else
                {
                    headings_[1]->setText("PROCESSED DTM · Run to generate");
                    headings_[2]->setText("REMOVED HEIGHT · Run to generate");
                    metrics_->setText("No processing has run yet.");
                    summary_->setText("Source loaded. Run to generate a new terrain estimate.");
                }
                source_->setText(QString("%1\n%2 × %3 · %4 m cells\n%5")
                                     .arg(QFileInfo(scene_->dsm->path).fileName())
                                     .arg(scene_->dsm->width)
                                     .arg(scene_->dsm->height)
                                     .arg(scene_->dsm->pixel)
                                     .arg(scene_->reference ? "Aligned reference attached"
                                                            : "No reference: heights can still be processed"));
                if (run_)
                    updateSummary();
            }
            setBusy(false);
            state_->setText(out.message);
            statusBar()->showMessage(out.message);
            if (closeQueued_)
                QTimer::singleShot(0, this, &QWidget::close);
        }
        catch (const std::exception& e)
        {
            failure(QString::fromUtf8(e.what()));
        }
    }
    void ensureViews()
    {
        const auto crs = CoordinateSystemFactory::fromUserInput(scene_->dsm->projection);
        const QStringList names = {"originalMap", "terrainMap", "diagnosticMap"};
        for (int pane = 0; pane < 3; ++pane)
        {
            if (!views_[pane])
            {
                auto* view = views_[pane] = new GisViewer;
                view->setObjectName(names[pane]);
                view->setMinimumSize(240, 260);
                holders_[pane]->addWidget(view);
                view->setActiveTool(GisViewerTool::Pan);
                auto* overlay = overlays_[pane] = new ProbeOverlay(view);
                overlay->setGeometry(view->rect());
                overlay->show();
                view->installEventFilter(this);
                connect(view, &GisViewer::visibleExtentChanged, this,
                        [this, pane](const GisExtent& extent)
                        {
                            if (!syncing_)
                            {
                                syncing_ = true;
                                const auto centre = extent.center();
                                const double units = std::max(extent.width() / std::max(1, views_[pane]->width()),
                                                              extent.height() / std::max(1, views_[pane]->height()));
                                for (int other = 0; other < 3; ++other)
                                    if (other != pane && views_[other])
                                    {
                                        const double dx = units * views_[other]->width() * .5,
                                                     dy = units * views_[other]->height() * .5;
                                        views_[other]->setViewExtent(GisExtent(centre.x() - dx, centre.y() - dy,
                                                                               centre.x() + dx, centre.y() + dy));
                                    }
                                syncing_ = false;
                            }
                            for (auto* overlay : overlays_)
                                if (overlay)
                                    overlay->update();
                        });
                connect(view, &GisViewer::mapMouseDown, this,
                        [this, pane](GisViewerTool, const QPointF& screen, const GisShapePoint&, Qt::MouseButton b,
                                     Qt::KeyboardModifiers)
                        {
                            if (b == Qt::LeftButton)
                                presses_[pane] = screen;
                        });
                connect(view, &GisViewer::mapMouseUp, this,
                        [this, pane](GisViewerTool tool, const QPointF& screen, const GisShapePoint& world,
                                     Qt::MouseButton b, Qt::KeyboardModifiers)
                        {
                            if (b == Qt::LeftButton)
                            {
                                const auto press = presses_[pane];
                                presses_[pane].reset();
                                if (tool == GisViewerTool::Pan && press && QLineF(*press, screen).length() <= 3)
                                    inspect(QPointF(world.x(), world.y()));
                            }
                        });
            }
            // A completed run or reference attachment must preserve navigation.
            if (!views_[pane]->coordinateSystem() ||
                !CoordinateSystemFactory::areEquivalent(*views_[pane]->coordinateSystem(), *crs))
                views_[pane]->setCoordinateSystem(crs);
        }
    }
    void clear(GisViewer& view)
    {
        while (view.layerCount())
            view.removeLayer(0);
        view.refreshLayers();
    }
    void replace(GisViewer& view, const QString& path)
    {
        const auto extent = view.viewExtent();
        clear(view);
        QString error;
        require(view.addLayerFromPath(path, &error), error);
        if (!extent.isEmpty())
            view.setViewExtent(extent);
        view.refreshLayers();
    }
    void tool(GisViewerTool value)
    {
        for (auto* view : views_)
            if (view)
                view->setActiveTool(value);
    }
    void fit()
    {
        if (scene_ && views_[0])
        {
            const auto e = scene_->dsm->extent();
            views_[0]->setViewExtent(e.inflate(scene_->dsm->pixel * 10, scene_->dsm->pixel * 10));
        }
    }
    void showDiagnostic()
    {
        if (!run_ || !views_[2])
            return;
        if (mode_->currentIndex() == 2 && !scene_->reference)
        {
            const QSignalBlocker blocker(mode_);
            mode_->setCurrentIndex(0);
            statusBar()->showMessage("Attach an aligned reference to show errors.");
        }
        replace(*views_[2], run_->diagnostics[mode_->currentIndex()]);
        headings_[1]->setText("PROCESSED DTM · live native result");
        const QStringList titles = {"REMOVED HEIGHT · DSM − DTM", "PROCESSING QUALITY · supports / edges",
                                    "REFERENCE ERROR · DTM − reference"};
        headings_[2]->setText(titles[mode_->currentIndex()]);
        const QStringList legends = {
            "Removed height: white 0 m → dark red ≥20 m. Display saturation only. NoData gray.",
            "Quality: teal retained source · pale interpolated · orange edge-sensitive · purple hull fallback · gray NoData. Full flag combinations are saved in QA GeoTIFF.",
            "Signed error: blue ≤−2 m · white 0 · red ≥+2 m. Shared fixed scale; numeric scores are not clipped."};
        legend_->setText(legends[mode_->currentIndex()]);
    }
    void updateSummary()
    {
        const auto& p = run_->terrain.parameters;
        summary_->setText(
            QString(
                "Last completed run · %1 supports · %2 s · working %3 m · max window %4 m · base %5 m · slope %6 · relief %7 · keep %8 m\nEdge band %9 m · hull fallback %10 pixels · reference is evaluation-only.")
                .arg(run_->terrain.supports)
                .arg(run_->terrain.seconds, 0, 'f', 2)
                .arg(run_->terrain.working)
                .arg(p.window)
                .arg(p.threshold)
                .arg(p.slope)
                .arg(p.terrain)
                .arg(p.retain)
                .arg(p.window * .5)
                .arg(run_->terrain.summary.value("outside_hull_fallback_pixels").toDouble()));
        const auto line = [](QString name, const Score& score)
        {
            return score.count ? QString("%1 (%2 px)\nRMSE %3 m · MAE %4 m · bias %5 m")
                                     .arg(name)
                                     .arg(score.count)
                                     .arg(score.rmse, 0, 'f', 3)
                                     .arg(score.mae, 0, 'f', 3)
                                     .arg(score.bias, 0, 'f', 3)
                               : name + ": no common valid reference pixels";
        };
        metrics_->setText(
            scene_->reference
                ? line("All valid reference cells", run_->all) + "\n\n" +
                      line("Interior outside edge band", run_->interior)
                : "No reference attached. Scores are unavailable; processing does not require a reference.");
    }
    void inspect(QPointF point)
    {
        if (!scene_ || busy_)
            return;
        const auto& dsm = *scene_->dsm;
        const double cx = (point.x() - dsm.transform[0]) / dsm.pixel, cy = (dsm.transform[3] - point.y()) / dsm.pixel;
        if (!std::isfinite(cx) || !std::isfinite(cy) || cx < 0 || cy < 0 || cx >= dsm.width || cy >= dsm.height)
            return;
        const int x = int(cx), y = int(cy), i = y * dsm.width + x;
        QString text = QString("Source pixel · %1 / %2\n").arg(x).arg(y);
        if (!dsm.valid[i])
            text += "NoData";
        else
        {
            text += QString("DSM %1 m").arg(dsm.z[i], 0, 'f', 2);
            if (run_)
                text += QString("\nDTM %1 m · removed %2 m\nQA bits: %3")
                            .arg(run_->terrain.dtm[i], 0, 'f', 2)
                            .arg(run_->terrain.removed[i], 0, 'f', 2)
                            .arg(run_->terrain.quality[i]);
            if (scene_->reference && scene_->reference->valid[i])
            {
                text += QString("\nReference %1 m").arg(scene_->reference->z[i], 0, 'f', 2);
                if (run_)
                    text +=
                        QString(" · error %1 m").arg(double(run_->terrain.dtm[i]) - scene_->reference->z[i], 0, 'f', 3);
            }
        }
        probe_->setText(text);
        probePoint_ = QPointF(dsm.transform[0] + (x + .5) * dsm.pixel, dsm.transform[3] - (y + .5) * dsm.pixel);
        for (auto* overlay : overlays_)
        {
            overlay->point = probePoint_;
            overlay->update();
        }
    }
    void saveResult()
    {
        if (!run_)
            return;
        QString path = QFileDialog::getSaveFileName(this, "Save DTM, removed heights, QA and JSON",
                                                    QDir::home().filePath("terrain-result.tif"), "GeoTIFF (*.tif)");
        if (path.isEmpty())
            return;
        if (!path.endsWith(".tif", Qt::CaseInsensitive))
            path += ".tif";
        const QFileInfo info(path);
        const QString stem = QDir(info.absolutePath()).filePath(info.completeBaseName());
        const QStringList destinations = {path, stem + ".removed.tif", stem + ".qa.tif", stem + ".json"};
        const auto scene = scene_;
        const auto run = run_;
        try
        {
            for (const auto& target : destinations)
            {
                const auto canonical = [](const QString& p)
                {
                    QFileInfo f(p);
                    return QDir::cleanPath(f.exists() ? f.canonicalFilePath() : f.absoluteFilePath());
                };
                require(canonical(target).compare(canonical(scene->dsm->path), Qt::CaseInsensitive) != 0 &&
                            (!scene->reference ||
                             canonical(target).compare(canonical(scene->reference->path), Qt::CaseInsensitive) != 0),
                        "Choose output names different from the source and reference.");
            }
            bool sidecarExists = false;
            for (int i = 1; i < destinations.size(); ++i)
                sidecarExists |= QFileInfo::exists(destinations[i]);
            if (sidecarExists &&
                QMessageBox::question(
                    this, "Replace existing result companions?",
                    "Removed-height, QA or JSON companion files already exist. Replace the result set?") !=
                    QMessageBox::Yes)
                return;
            begin(
                [this, scene, run, destinations]
                {
                    Outcome out;
                    const QStringList sources = {run->dtm, run->removed, run->quality};
                    QStringList saved;
                    try
                    {
                        for (int i = 0; i < 3; ++i)
                        {
                            setPhase(i * 30, "Saving " + QFileInfo(destinations[i]).fileName());
                            atomicCopy(sources[i], destinations[i], cancelled_);
                            saved << destinations[i];
                        }
                        checkpoint(cancelled_);
                        writeJson(destinations[3], run->report);
                        out.message = "Saved DTM, removed heights, QA flags and provenance JSON.";
                    }
                    catch (const std::exception& e)
                    {
                        out.error = QString::fromUtf8(e.what()) +
                                    (saved.isEmpty() ? QString() : "\nFiles already committed:\n" + saved.join("\n"));
                    }
                    return out;
                });
        }
        catch (const std::exception& e)
        {
            QMessageBox::warning(this, "Cannot export", QString::fromUtf8(e.what()));
        }
    }

    const QCommandLineParser& arguments_;
    QJsonObject assets_;
    std::shared_ptr<Scene> scene_;
    std::shared_ptr<Run> run_;
    std::array<GisViewer*, 3> views_{};
    std::array<QVBoxLayout*, 3> holders_{};
    std::array<QLabel*, 3> headings_{};
    std::array<ProbeOverlay*, 3> overlays_{};
    std::array<std::optional<QPointF>, 3> presses_;
    std::optional<QPointF> probePoint_;
    std::vector<QWidget*> controls_;
    std::vector<QDoubleSpinBox*> parameters_;
    std::vector<QAction*> navigation_;
    QDoubleSpinBox *working_ = nullptr, *window_ = nullptr, *threshold_ = nullptr, *slope_ = nullptr,
                   *terrain_ = nullptr, *retain_ = nullptr;
    QPushButton *runButton_ = nullptr, *cancel_ = nullptr, *save_ = nullptr, *referenceButton_ = nullptr;
    QComboBox* mode_ = nullptr;
    QLabel *source_ = nullptr, *state_ = nullptr, *metrics_ = nullptr, *probe_ = nullptr, *summary_ = nullptr,
           *legend_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QTimer poll_;
    QTemporaryDir temporary_;
    std::future<Outcome> pending_;
    std::atomic_bool cancelled_{false};
    std::atomic_int progressValue_{0};
    QMutex phaseMutex_;
    QString phase_;
    bool busy_ = false, closeQueued_ = false, syncing_ = false;
    int generation_ = 0;
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("DsmToDtmProcessing");
    app.setApplicationVersion("1.0");
    auto palette = app.palette();
    palette.setColor(QPalette::Window, QColor("#f2f6f8"));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::WindowText, QColor("#233e50"));
    palette.setColor(QPalette::Text, QColor("#233e50"));
    app.setPalette(palette);
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Live classical DSM-to-DTM filtering, TIN reconstruction and reference evaluation.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"data-dir", "Sample cache directory.", "directory"});
    parser.addOption({"sample-archive", "Use the pinned local sample ZIP.", "zip"});
    parser.addOption({"sample-url", "HTTPS mirror; pinned hashes still apply.", "url"});
    parser.process(app);
    try
    {
        ProcessingWindow window(parser);
        window.show();
        return app.exec();
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(nullptr, "DsmToDtmProcessing", QString::fromUtf8(e.what()));
        return 1;
    }
}
