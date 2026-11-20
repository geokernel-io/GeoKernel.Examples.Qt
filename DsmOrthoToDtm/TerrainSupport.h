#pragma once

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
    request.setHeader(QNetworkRequest::UserAgentHeader, "GeoKernel-DsmOrthoToDtm/1.0");
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    request.setTransferTimeout(60000);

    auto* reply = manager.get(request);

    QEventLoop loop;
    bool writeFailed = false;
    bool deadlineExceeded = false;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop,
                     [&]
                     {
                         deadlineExceeded = true;
                         reply->abort();
                         loop.quit();
                     });

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
    deadline.start(600000);
    if (!reply->isFinished())
        loop.exec();
    deadline.stop();
    cancelPoll.stop();

    const auto tail = reply->readAll();
    if (file.write(tail) != tail.size())
        writeFailed = true;

    checkpoint(cancelled);
    require(!deadlineExceeded, "Asset download exceeded ten minutes. Retry the download or use the pinned local ZIP.");
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    require(!writeFailed && reply->error() == QNetworkReply::NoError && status == 200,
            QString("Sample download failed (HTTP %1): %2\n%3\n"
                    "Check that the pinned asset is published at this address, or choose its exact local ZIP.")
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
        if (!raster->valid[i])
            raster->z[i] = NoData;
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
    std::optional<GisExtent> area;
    explicit ProbeOverlay(GisViewer* parent) : QWidget(parent), view(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (area)
        {
            const auto a = view->worldToScreen(GisShapePoint(area->xMin(), area->yMin()));
            const auto b = view->worldToScreen(GisShapePoint(area->xMax(), area->yMax()));
            const QRectF box = QRectF(a, b).normalized();
            QPainterPath outside, inside;
            outside.addRect(rect());
            inside.addRect(box);
            painter.fillPath(outside.subtracted(inside), QColor(255, 255, 255, 155));
            painter.setPen(QPen(QColor("#098b97"), 2, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(box);
        }
        if (point)
        {
            painter.setBrush(QColor("#d73573"));
            painter.setPen(QPen(Qt::white, 2));
            painter.drawEllipse(view->worldToScreen(GisShapePoint(point->x(), point->y())), 5, 5);
        }
    }
};
