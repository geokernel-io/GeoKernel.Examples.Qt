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
    request.setHeader(QNetworkRequest::UserAgentHeader, "GeoKernel-DsmToDtmAccuracy/1.0");
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

struct Grids
{
    QString folder, projection;
    int width = 0, height = 0;

    QVector<double> transform;
    std::array<QVector<float>, 4> z;
    QJsonObject manifest;
};

void configureSampleRuntime(const QString& folder)
{
    const QString sdkData = QDir(QCoreApplication::applicationDirPath()).filePath("proj-data");
    const QString projPath =
        QFileInfo::exists(QDir(sdkData).filePath("proj.db")) ? sdkData : QDir(folder).filePath("proj-data");
    const QByteArray proj = QDir::toNativeSeparators(projPath).toUtf8();
    require(QFileInfo::exists(QDir(projPath).filePath("proj.db")), "Sample projection database is missing.");
    qputenv("PROJ_DATA", proj);
    qputenv("PROJ_LIB", proj);
    qputenv("PROJ_NETWORK", "OFF");
}

std::shared_ptr<Grids> readGrids(const QString& folder, const std::atomic_bool& cancelled)
{
    auto grid = std::make_shared<Grids>();
    grid->folder = folder;
    grid->manifest = readJson(QDir(folder).filePath("source-manifest.json"));
    const QStringList names = {"dsm.tif", "dtm_classical.tif", "dtm_ai.tif", "dtm_reference.tif"};
    for (int i = 0; i < names.size(); ++i)
    {
        checkpoint(cancelled);
        GdalElevationProvider provider;
        QString error;
        require(provider.open(QDir(folder).filePath(names[i]), 1, &error), error);
        const auto m = provider.metadata();
        const auto& t = m.worldTransform;
        QVector<double> transform = {
            t.upperLeftCenterX - (t.pixelSizeX + t.rotationY) * .5, t.pixelSizeX, t.rotationY,
            t.upperLeftCenterY - (t.rotationX + t.pixelSizeY) * .5, t.rotationX,  t.pixelSizeY};

        const auto crs = GeoKernel::Core::CoordinateSystems::CoordinateSystemFactory::fromUserInput(m.projectionWkt);
        const auto expectedCrs = GeoKernel::Core::CoordinateSystems::CoordinateSystemFactory::fromEpsg(2056);
        require(crs && expectedCrs &&
                    GeoKernel::Core::CoordinateSystems::CoordinateSystemFactory::areEquivalent(*crs, *expectedCrs),
                "The sample height CRS is not equivalent to EPSG:2056.");
        require(
            m.hasWorldTransform && m.width == 500 && m.height == 500 && t.pixelSizeX == .5 && t.pixelSizeY == -.5 &&
                t.rotationX == 0 && t.rotationY == 0,
            QString(
                "The sample requires the pinned 500 x 500, 0.5 m grid. Found %1 x %2; transform valid %3; pixel %4, %5; rotation %6, %7.")
                .arg(m.width)
                .arg(m.height)
                .arg(m.hasWorldTransform)
                .arg(t.pixelSizeX)
                .arg(t.pixelSizeY)
                .arg(t.rotationX)
                .arg(t.rotationY));
        if (!i)
        {
            grid->width = m.width;
            grid->height = m.height;
            grid->projection = m.projectionWkt;
            grid->transform = transform;
        }
        else
        {
            require(grid->projection == m.projectionWkt && grid->transform == transform,
                    "Height rasters do not share exactly the same CRS and grid.");
        }
        grid->z[i] = provider.readAll(&error);
        require(grid->z[i].size() == m.width * m.height, "Could not read complete numeric heights: " + error);
        for (float value : grid->z[i])
            require(std::isfinite(value) && (!m.hasNoDataValue || value != m.noDataValue),
                    "This prepared comparison requires valid heights in all four rasters.");
    }

    return grid;
}

constexpr std::array<double, 4> DisplayLimits = {1., 2., 5., 10.};
constexpr std::array<double, 5> ErrorBinEdges = {.25, .5, 1., 2., 5.};

QColor mixColor(QColor a, QColor b, double t)
{
    return QColor(int(std::lround(a.red() + (b.red() - a.red()) * t)),
                  int(std::lround(a.green() + (b.green() - a.green()) * t)),
                  int(std::lround(a.blue() + (b.blue() - a.blue()) * t)));
}

QColor errorColor(double error, bool absolute, double limit)
{
    const double t = std::clamp(std::abs(error) / limit, 0., 1.);
    if (!absolute)
        return mixColor(QColor("#fafafa"), error < 0 ? QColor("#2464b5") : QColor("#c93645"), t);
    return t < .5 ? mixColor(QColor("#fafafa"), QColor("#f4a340"), t * 2)
                  : mixColor(QColor("#f4a340"), QColor("#b22f35"), (t - .5) * 2);
}

// Use the GDAL runtime already shipped with the SDK. Byte RGB bypasses the
// viewer's statistical stretch of non-Byte imagery, preserving the common scale.
class RgbWriter
{
public:
    RgbWriter()
    {
#ifdef Q_OS_WIN
        library_.setFileName(QDir(QCoreApplication::applicationDirPath()).filePath("gdal"));
#else
        library_.setFileName("gdal");
#endif
        library_.setLoadHints(QLibrary::PreventUnloadHint);
        if (!library_.load())
        {
#ifndef Q_OS_WIN
            library_.setFileName("libgdal.so.30");
#endif
            require(library_.load(), "Cannot load the SDK GDAL runtime: " + library_.errorString());
        }
        resolve<void (*)()>("GDALAllRegister")();
    }

    void write(const Grids& grid, const QImage& image, const QString& path)
    {
        require(image.format() == QImage::Format_RGB888 && image.width() == grid.width && image.height() == grid.height,
                "Expected grid-aligned Byte RGB image.");
        auto driver = resolve<void* (*)(const char*)>("GDALGetDriverByName");
        auto create = resolve<void* (*)(void*, const char*, int, int, int, int, char**)>("GDALCreate");
        auto close = resolve<void (*)(void*)>("GDALClose");
        auto transform = resolve<int (*)(void*, double*)>("GDALSetGeoTransform");
        auto projection = resolve<int (*)(void*, const char*)>("GDALSetProjection");
        auto band = resolve<void* (*)(void*, int)>("GDALGetRasterBand");
        auto interpretation = resolve<int (*)(void*, int)>("GDALSetRasterColorInterpretation");
        auto rasterIO =
            resolve<int (*)(void*, int, int, int, int, int, void*, int, int, int, int, int)>("GDALRasterIO");
        char* options[] = {const_cast<char*>("COMPRESS=DEFLATE"), const_cast<char*>("TILED=YES"),
                           const_cast<char*>("PHOTOMETRIC=RGB"), nullptr};
        const QByteArray filename = path.toUtf8(), wkt = grid.projection.toUtf8();
        std::unique_ptr<void, void (*)(void*)> dataset(
            create(driver("GTiff"), filename.constData(), grid.width, grid.height, 3, 1, options), close);
        require(bool(dataset), "Cannot create display GeoTIFF: " + path);
        auto gt = grid.transform;
        require(transform(dataset.get(), gt.data()) == 0 && projection(dataset.get(), wkt.constData()) == 0,
                "Cannot set display GeoTIFF coordinates.");
        for (int channel = 0; channel < 3; ++channel)
        {
            auto* output = band(dataset.get(), channel + 1);
            require(output && interpretation(output, channel + 3) == 0, "Cannot mark RGB display bands.");
            require(rasterIO(output, 1, 0, 0, grid.width, grid.height, const_cast<uchar*>(image.constBits()) + channel,
                             grid.width, grid.height, 1, 3, image.bytesPerLine()) == 0,
                    "Cannot write display RGB pixels.");
        }
    }

private:
    template <class T> T resolve(const char* name)
    {
        const auto function = library_.resolve(name);
        require(function, QString("GDAL runtime entry point missing: %1").arg(name));
        return reinterpret_cast<T>(function);
    }
    QLibrary library_;
};

QImage reliefImage(const Grids& grid, int band, const std::atomic_bool& cancelled)
{
    QImage image(grid.width, grid.height, QImage::Format_RGB888);
    const double az = 315. * 3.141592653589793 / 180., alt = 40. * 3.141592653589793 / 180.;
    const double lx = std::sin(az) * std::cos(alt), ly = std::cos(az) * std::cos(alt), lz = std::sin(alt);
    const auto at = [&](int x, int y)
    {
        return double(grid.z[band][y * grid.width + x]);
    };
    for (int y = 0; y < grid.height; ++y)
    {
        checkpoint(cancelled);
        auto* row = image.scanLine(y);
        const int top = std::max(0, y - 1), bottom = std::min(grid.height - 1, y + 1);
        for (int x = 0; x < grid.width; ++x)
        {
            const int left = std::max(0, x - 1), right = std::min(grid.width - 1, x + 1);
            const double dx = (at(right, y) - at(left, y)) / ((right - left) * .5),
                         dy = (at(x, top) - at(x, bottom)) / ((bottom - top) * .5);
            const double light = (-dx * lx - dy * ly + lz) / std::sqrt(dx * dx + dy * dy + 1);
            const uchar gray = uchar(std::clamp(int(std::lround(255 * (.25 + .75 * std::max(0., light)))), 0, 255));
            row[x * 3] = row[x * 3 + 1] = row[x * 3 + 2] = gray;
        }
    }
    return image;
}

QImage errorImage(const Grids& grid, int method, bool absolute, double limit, const std::atomic_bool& cancelled)
{
    QImage image(grid.width, grid.height, QImage::Format_RGB888);
    for (int y = 0; y < grid.height; ++y)
    {
        checkpoint(cancelled);
        auto* row = image.scanLine(y);
        for (int x = 0; x < grid.width; ++x)
        {
            const int index = y * grid.width + x;
            const QColor color = errorColor(double(grid.z[method][index]) - grid.z[3][index], absolute, limit);
            row[x * 3] = uchar(color.red());
            row[x * 3 + 1] = uchar(color.green());
            row[x * 3 + 2] = uchar(color.blue());
        }
    }
    return image;
}

struct Metric
{
    qint64 count = 0, above = 0;
    double rmse = 0, mae = 0, bias = 0, p95 = 0;
    std::array<qint64, 6> bins{};
    QJsonObject json() const
    {
        QJsonArray histogram;
        for (qint64 bin : bins)
            histogram.append(double(bin));
        return {{"pixels", double(count)},
                {"rmse_m", rmse},
                {"mae_m", mae},
                {"bias_m", bias},
                {"p95_absolute_m", p95},
                {"above_threshold_pixels", double(above)},
                {"above_threshold_percent", count ? 100. * above / count : 0.},
                {"absolute_error_histogram", histogram}};
    }
};

QRect pixelWindow(const Grids& grid, const GisExtent& area)
{
    require(!area.isEmpty() && std::isfinite(area.xMin()) && std::isfinite(area.yMin()) && std::isfinite(area.xMax()) &&
                std::isfinite(area.yMax()),
            "Choose a nonempty finite study area.");
    const auto index = [](double value, int maximum)
    {
        return int(std::clamp(value, 0., double(maximum)));
    };
    const double dx = grid.transform[1], dy = -grid.transform[5], x0 = grid.transform[0], y0 = grid.transform[3];
    const int left = index(std::ceil((area.xMin() - x0) / dx - .5), grid.width);
    const int right = index(std::floor((area.xMax() - x0) / dx - .5) + 1, grid.width);
    const int top = index(std::ceil((y0 - area.yMax()) / dy - .5), grid.height);
    const int bottom = index(std::floor((y0 - area.yMin()) / dy - .5) + 1, grid.height);
    require(right > left && bottom > top, "The selected area contains no source pixel centres.");
    return QRect(left, top, right - left, bottom - top);
}

GisExtent windowExtent(const Grids& grid, const QRect& window)
{
    const auto& t = grid.transform;
    return GisExtent(t[0] + window.left() * t[1], t[3] + (window.top() + window.height()) * t[5],
                     t[0] + (window.left() + window.width()) * t[1], t[3] + window.top() * t[5]);
}

std::array<Metric, 2> measure(const Grids& grid, const QRect& window, double threshold)
{
    require(threshold > 0 && std::isfinite(threshold), "Error threshold must be positive.");
    require(!window.isEmpty() && QRect(0, 0, grid.width, grid.height).contains(window), "Invalid source pixel window.");
    std::array<Metric, 2> result;
    for (int method = 0; method < 2; ++method)
    {
        auto& metric = result[method];
        std::vector<double> errors;
        errors.reserve(size_t(window.width()) * window.height());
        for (int y = window.top(); y < window.top() + window.height(); ++y)
            for (int x = window.left(); x < window.left() + window.width(); ++x)
            {
                const int pixel = y * grid.width + x;
                const double error = double(grid.z[method + 1][pixel]) - grid.z[3][pixel], absolute = std::abs(error);
                ++metric.count;
                metric.rmse += error * error;
                metric.mae += absolute;
                metric.bias += error;
                metric.above += absolute > threshold;
                ++metric.bins[size_t(std::lower_bound(ErrorBinEdges.begin(), ErrorBinEdges.end(), absolute) -
                                     ErrorBinEdges.begin())];
                errors.push_back(absolute);
            }
        metric.rmse = std::sqrt(metric.rmse / metric.count);
        metric.mae /= metric.count;
        metric.bias /= metric.count;
        std::sort(errors.begin(), errors.end());
        metric.p95 = errors[size_t(std::ceil(.95 * errors.size())) - 1];
    }
    return result;
}

class AreaOverlay final : public QWidget
{
public:
    GisViewer* viewer;
    std::optional<GisExtent> area;
    std::optional<QPointF> probe;
    explicit AreaOverlay(GisViewer* view) : QWidget(view), viewer(view)
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
            const auto a = viewer->worldToScreen(GisShapePoint(area->xMin(), area->yMin())),
                       b = viewer->worldToScreen(GisShapePoint(area->xMax(), area->yMax()));
            const QRectF box = QRectF(a, b).normalized();
            QPainterPath outside;
            outside.addRect(rect());
            QPainterPath inside;
            inside.addRect(box);
            painter.fillPath(outside.subtracted(inside), QColor(255, 255, 255, 100));
            painter.setPen(QPen(QColor("#167d8d"), 2, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(box);
        }
        if (probe)
        {
            const auto point = viewer->worldToScreen(GisShapePoint(probe->x(), probe->y()));
            painter.setBrush(QColor("#dc3672"));
            painter.setPen(QPen(Qt::white, 2));
            painter.drawEllipse(point, 5, 5);
        }
    }
};

class ColorLegend final : public QWidget
{
public:
    int mode = 0;
    double limit = 2;
    explicit ColorLegend(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(62);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        p.setPen(QColor("#476174"));
        if (mode == 2)
        {
            p.drawText(
                rect(), Qt::AlignCenter,
                "Neutral relief: same light and contrast for both terrain estimates. Switch to an error view to compare reference differences.");
            return;
        }
        p.drawText(QRectF(8, 1, width() - 16, 20), Qt::AlignCenter,
                   mode == 0 ? "Estimated DTM − reference: blue below · white zero · red above (metres)"
                             : "Absolute reference error: white zero · orange/red larger errors (metres)");
        QLinearGradient gradient(70, 0, width() - 70, 0);
        for (int i = 0; i <= 20; ++i)
        {
            const double t = i / 20.;
            gradient.setColorAt(t, errorColor(mode == 0 ? (t * 2 - 1) * limit : t * limit, mode == 1, limit));
        }
        p.fillRect(QRectF(70, 25, width() - 140, 13), gradient);
        p.drawText(QRectF(8, 42, 180, 18), Qt::AlignLeft, mode == 0 ? QString("≤ −%1 m").arg(limit) : "0 m");
        p.drawText(QRectF(width() - 188, 42, 180, 18), Qt::AlignRight, QString("≥ %1 m").arg(limit));
        p.drawText(QRectF(200, 42, width() - 400, 18), Qt::AlignCenter,
                   "One shared scale · display saturation only; numeric errors are unchanged");
    }
};

class ErrorHistogram final : public QWidget
{
public:
    std::array<Metric, 2> metrics;
    explicit ErrorHistogram(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(150);
        setObjectName("errorHistogram");
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), Qt::white);
        if (!metrics[0].count)
            return;
        p.setPen(QColor("#476174"));
        p.drawText(QRectF(8, 1, width() - 16, 22), Qt::AlignCenter,
                   "Absolute error distribution · evaluated area · teal classical / purple AI");
        const QRectF plot(50, 30, width() - 70, height() - 60);
        const double group = plot.width() / 6;
        double maximum = 10;
        for (const auto& metric : metrics)
            for (qint64 count : metric.bins)
                maximum = std::max(maximum, 100. * count / metric.count);
        maximum = std::ceil(maximum / 10) * 10;
        for (int tick = 0; tick <= 2; ++tick)
        {
            const double y = plot.bottom() - tick * .5 * plot.height();
            p.setPen(QColor("#e2e9ee"));
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            p.setPen(QColor("#476174"));
            p.drawText(QRectF(0, y - 9, 43, 18), Qt::AlignRight, QString::number(tick * .5 * maximum, 'f', 0) + "%");
        }
        const QStringList labels = {"≤0.25", "0.25–0.5", "0.5–1", "1–2", "2–5", ">5 m"};
        for (int bin = 0; bin < 6; ++bin)
        {
            for (int method = 0; method < 2; ++method)
            {
                const double height =
                    100. * metrics[method].bins[bin] / metrics[method].count / maximum * plot.height();
                p.fillRect(QRectF(plot.left() + bin * group + group * (.18 + method * .32), plot.bottom() - height,
                                  group * .28, height),
                           QColor(method ? "#8857bd" : "#087e8b"));
            }
            p.setPen(QColor("#476174"));
            p.drawText(QRectF(plot.left() + bin * group, plot.bottom() + 3, group, 20), Qt::AlignCenter, labels[bin]);
        }
    }
};

struct Prepared
{
    std::shared_ptr<Grids> grid;
    QString original;
    std::array<std::array<QString, 9>, 2> files;
    QString error;
};

Prepared prepareDisplays(std::shared_ptr<Grids> grid, const QString& directory, std::atomic_bool& cancelled,
                         std::atomic_int& progress)
{
    Prepared result;
    result.grid = std::move(grid);
    RgbWriter writer;
    int done = 0;
    const auto save = [&](QImage image, const QString& name)
    {
        checkpoint(cancelled);
        const QString path = QDir(directory).filePath(name);
        writer.write(*result.grid, image, path);
        progress = ++done * 100 / 19;
        return path;
    };
    result.original = save(reliefImage(*result.grid, 0, cancelled), "original.tif");
    for (int method = 0; method < 2; ++method)
    {
        for (int range = 0; range < 4; ++range)
        {
            result.files[method][range] =
                save(errorImage(*result.grid, method + 1, false, DisplayLimits[range], cancelled),
                     QString("signed-%1-%2.tif").arg(method).arg(range));
            result.files[method][4 + range] =
                save(errorImage(*result.grid, method + 1, true, DisplayLimits[range], cancelled),
                     QString("absolute-%1-%2.tif").arg(method).arg(range));
        }
        result.files[method][8] =
            save(reliefImage(*result.grid, method + 1, cancelled), QString("terrain-%1.tif").arg(method));
    }
    return result;
}

class AccuracyWindow final : public QMainWindow
{
public:
    explicit AccuracyWindow(const QCommandLineParser& arguments)
        : arguments_(arguments), assets_(readJson(":/sample-assets.json"))
    {
        setWindowTitle("DSM to DTM | Where is each method more accurate? — GeoKernel");
        resize(1720, 970);
        setWindowIcon(QIcon(":/icons/geokernel.ico"));
        setStyleSheet(
            "QMainWindow,QWidget#page{background:#f2f6f8;color:#233e50;} QLabel#title{font-size:21px;font-weight:600;color:#145e72;} QLabel#heading{background:#e4eef2;color:#125a70;padding:9px;font-weight:600;} QLabel#card{background:white;border:1px solid #d5e2e9;padding:10px;} QPushButton{padding:7px;} QToolBar{background:white;border-bottom:1px solid #d5e2e9;spacing:8px;padding:7px;}");
        const auto label = [](const QString& text, const char* name = nullptr)
        {
            auto* item = new QLabel(text);
            item->setWordWrap(true);
            if (name)
                item->setObjectName(name);
            return item;
        };
        auto* page = new QWidget;
        page->setObjectName("page");
        auto* layout = new QHBoxLayout(page);
        layout->setSpacing(12);
        auto* sidebar = new QWidget;
        auto* controls = new QVBoxLayout(sidebar);
        controls->setContentsMargins(0, 0, 10, 0);
        controls->setSpacing(11);
        controls->addWidget(label("Where does each\nmethod work better?", "title"));
        controls->addWidget(label("Zürich · same DSM and reference\n250 × 250 m · 0.5 m source cells"));
        instructions_ = label("Preparing sample data...", "card");
        controls->addWidget(instructions_);
        mode_ = new QComboBox;
        mode_->setObjectName("displayMode");
        mode_->addItems({"Signed reference errors", "Absolute reference errors", "Terrain estimates"});
        controls->addWidget(label("Map comparison"));
        controls->addWidget(mode_);
        range_ = new QComboBox;
        range_->setObjectName("displayRange");
        range_->addItems({"1 m", "2 m", "5 m", "10 m"});
        range_->setCurrentIndex(1);
        threshold_ = new QDoubleSpinBox;
        threshold_->setObjectName("errorThreshold");
        threshold_->setRange(.1, 20);
        threshold_->setDecimals(1);
        threshold_->setSingleStep(.5);
        threshold_->setValue(2);
        threshold_->setSuffix(" m");
        auto* form = new QFormLayout;
        form->addRow("Display limit", range_);
        form->addRow("Large error >", threshold_);
        controls->addLayout(form);
        presets_ = new QComboBox;
        presets_->setObjectName("studyPreset");
        presets_->addItems(
            {"Full sample", "Central urban block", "Northern block", "Southern block", "Custom selection"});
        controls->addWidget(label("Study-area preset"));
        controls->addWidget(presets_);
        winners_ = label("Compare lower errors for each metric.", "card");
        winners_->setObjectName("metricVerdict");
        controls->addWidget(winners_);
        probe_ = label("Click a map pixel to inspect its heights and signed errors.", "card");
        probe_->setObjectName("pixelProbe");
        probe_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        controls->addWidget(probe_);
        controls->addWidget(label(
            "One shared reference and source grid. Geographic presets are not land-cover classes. Dimmed map pixels are outside the evaluated area."));
        controls->addStretch();
        export_ = new QPushButton("Export area report");
        export_->setObjectName("exportReport");
        reload_ = new QPushButton("Retry / reload sample");
        cancel_ = new QPushButton("Cancel loading");
        controls->addWidget(export_);
        controls->addWidget(reload_);
        controls->addWidget(cancel_);
        controls->addWidget(
            label("Source: Federal Office of Topography swisstopo\nAI: David Perez · experimental pix_grad"));
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setFixedWidth(290);
        scroll->setWidget(sidebar);
        layout->addWidget(scroll);
        auto* workspace = new QVBoxLayout;
        auto* maps = new QHBoxLayout;
        maps->setSpacing(8);
        const QStringList titles = {"ORIGINAL DSM · context", "CLASSICAL DTM · reference error",
                                    "AI DTM · reference error"};
        for (int pane = 0; pane < 3; ++pane)
        {
            auto* column = new QVBoxLayout;
            column->setSpacing(0);
            headings_[pane] = label(titles[pane], "heading");
            column->addWidget(headings_[pane]);
            holders_[pane] = new QVBoxLayout;
            column->addLayout(holders_[pane], 1);
            maps->addLayout(column, 1);
        }
        workspace->addLayout(maps, 1);
        legend_ = new ColorLegend;
        workspace->addWidget(legend_);
        scope_ = label("No evaluation area loaded.");
        scope_->setObjectName("evaluationScope");
        workspace->addWidget(scope_);
        table_ = new QTableWidget(2, 6);
        table_->setObjectName("metricTable");
        table_->setHorizontalHeaderLabels(
            {"Method", "RMSE (m)", "MAE (m)", "Bias (m)", "P95 |error| (m)", "|error| > 2 m (%)"});
        table_->verticalHeader()->hide();
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setSelectionMode(QAbstractItemView::NoSelection);
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table_->setFixedHeight(101);
        workspace->addWidget(table_);
        histogram_ = new ErrorHistogram;
        workspace->addWidget(histogram_);
        workspace->addWidget(label(
            "Prepared independent results; reference is evaluation-only. Source heights: LN02 metres, no vertical datum conversion. Source filename years: DSM 2018 / reference 2019. Fixed development crop; these are not held-out benchmark results."));
        layout->addLayout(workspace, 1);
        setCentralWidget(page);
        auto* toolbar = addToolBar("Area and navigation");
        toolbar->setMovable(false);
        toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        areaAction_ = toolbar->addAction("Select evaluation area");
        areaAction_->setObjectName("selectArea");
        areaAction_->setCheckable(true);
        auto* current = toolbar->addAction("Evaluate current view");
        current->setObjectName("currentView");
        auto* whole = toolbar->addAction("Full sample");
        whole->setObjectName("fullSample");
        toolbar->addSeparator();
        auto* pan = toolbar->addAction(QIcon(":/icons/pan.svg"), "Pan");
        auto* box = toolbar->addAction(QIcon(":/icons/zoom-box.svg"), "Zoom box");
        auto* in = toolbar->addAction(QIcon(":/icons/zoom-in.svg"), "Zoom in");
        auto* out = toolbar->addAction(QIcon(":/icons/zoom-out.svg"), "Zoom out");
        auto* fit = toolbar->addAction(QIcon(":/icons/full-extent.svg"), "Full extent");
        navigation_ = {current, whole, pan, box, in, out, fit};
        connect(areaAction_, &QAction::toggled, this,
                [this](bool active)
                {
                    setTool(active ? GisViewerTool::Select : GisViewerTool::Pan);
                    instructions_->setText(
                        active ? "Drag a box on any map. The same source pixels will be evaluated for both methods."
                               : "Select an area, choose a preset, or click a pixel to inspect errors.");
                });
        connect(current, &QAction::triggered, this,
                [this]
                {
                    if (views_[0])
                        chooseArea(views_[0]->viewExtent(), "Current map view");
                });
        connect(whole, &QAction::triggered, this,
                [this]
                {
                    applyPreset(0);
                });
        connect(pan, &QAction::triggered, this,
                [this]
                {
                    areaAction_->setChecked(false);
                    setTool(GisViewerTool::Pan);
                });
        connect(box, &QAction::triggered, this,
                [this]
                {
                    areaAction_->setChecked(false);
                    setTool(GisViewerTool::ZoomBox);
                });
        connect(in, &QAction::triggered, this,
                [this]
                {
                    if (views_[0])
                        views_[0]->zoomIn(1.5);
                });
        connect(out, &QAction::triggered, this,
                [this]
                {
                    if (views_[0])
                        views_[0]->zoomOut(1.5);
                });
        connect(fit, &QAction::triggered, this,
                [this]
                {
                    fullExtent();
                });
        connect(mode_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this]
                {
                    selectDisplay();
                });
        connect(range_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this]
                {
                    selectDisplay();
                });
        connect(threshold_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this]
                {
                    if (grid_)
                        updateMetrics();
                });
        connect(presets_, qOverload<int>(&QComboBox::activated), this,
                [this](int index)
                {
                    applyPreset(index);
                });
        connect(export_, &QPushButton::clicked, this,
                [this]
                {
                    exportReport();
                });
        connect(reload_, &QPushButton::clicked, this,
                [this]
                {
                    start();
                });
        connect(cancel_, &QPushButton::clicked, this,
                [this]
                {
                    cancelled_ = true;
                    cancel_->setEnabled(false);
                    statusBar()->showMessage("Cancelling preparation...");
                });
        progress_ = new QProgressBar;
        progress_->setFixedWidth(210);
        statusBar()->addPermanentWidget(progress_);
        poll_.setInterval(50);
        connect(&poll_, &QTimer::timeout, this,
                [this]
                {
                    finishLoading();
                });
        setBusy(false);
        QTimer::singleShot(0, this,
                           [this]
                           {
                               start();
                           });
    }

    ~AccuracyWindow() override
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
            for (int pane = 0; pane < 3; ++pane)
                if (object == views_[pane] && overlays_[pane])
                    overlays_[pane]->setGeometry(views_[pane]->rect());
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
    void setBusy(bool busy)
    {
        busy_ = busy;
        reload_->setEnabled(!busy);
        cancel_->setEnabled(busy);
        progress_->setVisible(busy);
        const std::array<QWidget*, 5> widgets = {mode_, range_, threshold_, presets_, export_};
        for (auto* widget : widgets)
            widget->setEnabled(!busy && bool(grid_));
        range_->setEnabled(!busy && bool(grid_) && mode_->currentIndex() != 2);
        areaAction_->setEnabled(!busy && bool(grid_));
        for (auto* action : navigation_)
            action->setEnabled(!busy && bool(grid_));
    }
    void start()
    {
        if (busy_)
            return;
        cancelled_ = false;
        areaAction_->setChecked(false);
        progressValue_ = 0;
        setBusy(true);
        try
        {
            const QString cache = arguments_.isSet("data-dir")
                                      ? QFileInfo(arguments_.value("data-dir")).absoluteFilePath()
                                      : QDir(QCoreApplication::applicationDirPath()).filePath("data");
            const QUrl url(arguments_.isSet("sample-url") ? arguments_.value("sample-url")
                                                          : assets_.value("url").toString());
            const QString folder = prepareSample(assets_, cache, arguments_.value("sample-archive"), url, cancelled_,
                                                 [this](int percent, const QString& text)
                                                 {
                                                     progress_->setValue(percent);
                                                     statusBar()->showMessage(text);
                                                 });
            checkpoint(cancelled_);
            configureSampleRuntime(folder);
            const QString temporary = QDir(temporary_.path()).filePath(QString::number(++generation_));
            require(QDir().mkpath(temporary), "Cannot create temporary display directory.");
            statusBar()->showMessage("Reading heights and preparing shared-scale Byte RGB maps...");
            pending_ = std::async(std::launch::async,
                                  [this, folder, temporary]
                                  {
                                      Prepared result;
                                      try
                                      {
                                          result = prepareDisplays(readGrids(folder, cancelled_), temporary, cancelled_,
                                                                   progressValue_);
                                      }
                                      catch (const std::exception& e)
                                      {
                                          result.error = QString::fromUtf8(e.what());
                                      }
                                      return result;
                                  });
            poll_.start();
        }
        catch (const std::exception& e)
        {
            failure(QString::fromUtf8(e.what()));
        }
    }
    void failure(const QString& error)
    {
        setBusy(false);
        statusBar()->showMessage(error);
        if (closeQueued_)
            QTimer::singleShot(0, this, &QWidget::close);
        else if (!cancelled_)
            QMessageBox::warning(this, "Unable to load comparison", error);
    }
    GisLayer* addRaster(GisViewer& view, const QString& path, const QString& name)
    {
        std::vector<GisLayer*> before;
        for (int i = 0; i < view.layerCount(); ++i)
            before.push_back(view.mapLayerAt(i));
        QString error;
        require(view.addLayerFromPath(path, &error), error);
        GisLayer* result = nullptr;
        for (int i = 0; i < view.layerCount(); ++i)
        {
            auto* candidate = view.mapLayerAt(i);
            if (std::find(before.begin(), before.end(), candidate) == before.end())
            {
                require(!result, "Unexpected extra raster layer.");
                result = candidate;
            }
        }
        require(result, "Raster layer was not added.");
        result->setName(name);
        return result;
    }
    void finishLoading()
    {
        progress_->setValue(progressValue_);
        if (!pending_.valid() || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        poll_.stop();
        auto result = pending_.get();
        if (cancelled_ || !result.error.isEmpty())
        {
            failure(cancelled_ ? "Preparation cancelled; completed comparison retained." : result.error);
            return;
        }
        try
        {
            grid_ = result.grid;
            const auto crs = CoordinateSystemFactory::fromEpsg(2056);
            require(bool(crs), "EPSG:2056 is unavailable.");
            const QStringList names = {"originalMap", "classicalMap", "aiMap"};
            for (int pane = 0; pane < 3; ++pane)
            {
                if (!views_[pane])
                {
                    views_[pane] = new GisViewer;
                    views_[pane]->setObjectName(names[pane]);
                    views_[pane]->setMinimumSize(270, 290);
                    holders_[pane]->addWidget(views_[pane]);
                    views_[pane]->setCoordinateSystem(crs);
                    views_[pane]->setActiveTool(GisViewerTool::Pan);
                    overlays_[pane] = new AreaOverlay(views_[pane]);
                    overlays_[pane]->setGeometry(views_[pane]->rect());
                    overlays_[pane]->show();
                    views_[pane]->installEventFilter(this);
                    connect(views_[pane], &GisViewer::visibleExtentChanged, this,
                            [this, pane](const GisExtent& extent)
                            {
                                if (!syncing_)
                                    syncViews(pane, extent);
                                updateOverlays();
                            });
                    connect(views_[pane], &GisViewer::mapSelectionBoxFinished, this,
                            [this](const QRect&, const GisExtent& extent, Qt::KeyboardModifiers)
                            {
                                if (areaAction_->isChecked())
                                {
                                    chooseArea(extent, "User-selected area");
                                    areaAction_->setChecked(false);
                                }
                            });
                    connect(views_[pane], &GisViewer::mapMouseDown, this,
                            [this, pane](GisViewerTool, const QPointF& screen, const GisShapePoint&,
                                         Qt::MouseButton button, Qt::KeyboardModifiers)
                            {
                                if (button == Qt::LeftButton)
                                    presses_[pane] = screen;
                            });
                    connect(views_[pane], &GisViewer::mapMouseUp, this,
                            [this, pane](GisViewerTool, const QPointF& screen, const GisShapePoint& point,
                                         Qt::MouseButton button, Qt::KeyboardModifiers)
                            {
                                if (button != Qt::LeftButton)
                                    return;
                                const auto press = presses_[pane];
                                presses_[pane].reset();
                                if (press && !areaAction_->isChecked() && QLineF(*press, screen).length() <= 3.)
                                    inspect(QPointF(point.x(), point.y()));
                            });
                }
                while (views_[pane]->layerCount())
                    views_[pane]->removeLayer(0);
                if (!pane)
                    addRaster(*views_[pane], result.original, "Original DSM");
                else
                    for (int file = 0; file < 9; ++file)
                    {
                        layers_[pane - 1][file] =
                            addRaster(*views_[pane], result.files[pane - 1][file],
                                      QString("%1 display %2").arg(pane == 1 ? "Classical" : "AI").arg(file));
                        layers_[pane - 1][file]->setVisible(false);
                    }
                views_[pane]->refreshLayers();
            }
            QTimer::singleShot(0, this,
                               [this]
                               {
                                   fullExtent();
                               });
            selectDisplay();
            if (area_.isEmpty())
                applyPreset(0);
            else
            {
                updateMetrics();
                updateOverlays();
            }
            setBusy(false);
            instructions_->setText("Select an area, choose a preset, or click a pixel to inspect errors.");
            statusBar()->showMessage("Ready · select an evaluation area or click any source pixel.");
            if (closeQueued_)
                QTimer::singleShot(0, this, &QWidget::close);
        }
        catch (const std::exception& e)
        {
            failure(QString::fromUtf8(e.what()));
        }
    }
    void setTool(GisViewerTool tool)
    {
        for (auto* view : views_)
            if (view)
                view->setActiveTool(tool);
    }
    void syncViews(int source, const GisExtent& extent)
    {
        if (extent.isEmpty() || views_[source]->width() <= 0 || views_[source]->height() <= 0)
            return;
        const double units =
            std::max(extent.width() / views_[source]->width(), extent.height() / views_[source]->height());
        const auto centre = extent.center();
        syncing_ = true;
        for (int pane = 0; pane < 3; ++pane)
            if (pane != source && views_[pane])
            {
                const double dx = units * views_[pane]->width() * .5;
                const double dy = units * views_[pane]->height() * .5;
                views_[pane]->setViewExtent(
                    GisExtent(centre.x() - dx, centre.y() - dy, centre.x() + dx, centre.y() + dy));
            }
        syncing_ = false;
    }
    void fullExtent()
    {
        if (views_[0])
            views_[0]->setViewExtent(GisExtent(2677090, 1251340, 2677360, 1251610));
    }
    void selectDisplay()
    {
        legend_->mode = mode_->currentIndex();
        legend_->limit = DisplayLimits[range_->currentIndex()];
        legend_->update();
        range_->setEnabled(!busy_ && bool(grid_) && mode_->currentIndex() != 2);
        if (!views_[1])
            return;
        const int selected =
            mode_->currentIndex() == 2 ? 8 : (mode_->currentIndex() == 1 ? 4 : 0) + range_->currentIndex();
        for (int method = 0; method < 2; ++method)
        {
            for (int file = 0; file < 9; ++file)
                layers_[method][file]->setVisible(file == selected);
            views_[method + 1]->refreshLayers();
        }
        headings_[1]->setText(mode_->currentIndex() == 2   ? "CLASSICAL DTM · terrain estimate"
                              : mode_->currentIndex() == 1 ? "CLASSICAL DTM · absolute error"
                                                           : "CLASSICAL DTM · signed error");
        headings_[2]->setText(mode_->currentIndex() == 2   ? "AI DTM · experimental pix_grad"
                              : mode_->currentIndex() == 1 ? "AI DTM · absolute error"
                                                           : "AI DTM · signed error");
    }
    void chooseArea(const GisExtent& extent, const QString& name, int preset = -1)
    {
        if (!grid_)
            return;
        try
        {
            const QRect selected = pixelWindow(*grid_, extent);
            area_ = selected;
            areaName_ = name;
            const QSignalBlocker blocker(presets_);
            presets_->setCurrentIndex(preset < 0 ? 4 : preset);
            updateMetrics();
            updateOverlays();
            statusBar()->showMessage("Updated both methods on exactly the same source cells.");
        }
        catch (const std::exception& e)
        {
            statusBar()->showMessage(QString::fromUtf8(e.what()));
        }
    }
    void applyPreset(int index)
    {
        if (!grid_)
            return;
        if (index == 4)
        {
            areaAction_->setChecked(true);
            return;
        }
        areaAction_->setChecked(false);
        const QSignalBlocker blocker(presets_);
        presets_->setCurrentIndex(index);
        const double x = grid_->transform[0], y = grid_->transform[3], size = grid_->width * .5;
        if (index == 0)
            chooseArea(GisExtent(x, y - size, x + size, y), "Full sample", index);
        else if (index == 1)
            chooseArea(GisExtent(x + size * .2, y - size * .8, x + size * .8, y - size * .2), "Central urban block",
                       index);
        else if (index == 2)
            chooseArea(GisExtent(x, y - size * .35, x + size, y), "Northern block", index);
        else
            chooseArea(GisExtent(x, y - size, x + size, y - size * .65), "Southern block", index);
    }
    void updateMetrics()
    {
        if (!grid_ || area_.isEmpty())
            return;
        metrics_ = measure(*grid_, area_, threshold_->value());
        histogram_->metrics = metrics_;
        histogram_->update();
        table_->setHorizontalHeaderItem(5,
                                        new QTableWidgetItem(QString("|error| > %1 m (%)").arg(threshold_->value())));
        for (int method = 0; method < 2; ++method)
        {
            const auto& metric = metrics_[method];
            const QStringList values = {
                method ? "AI · pix_grad" : "Classical", QString::number(metric.rmse, 'f', 3),
                QString::number(metric.mae, 'f', 3),    QString::number(metric.bias, 'f', 3),
                QString::number(metric.p95, 'f', 3),    QString::number(100. * metric.above / metric.count, 'f', 2)};
            for (int column = 0; column < 6; ++column)
            {
                auto* item = new QTableWidgetItem(values[column]);
                item->setForeground(QColor(method ? "#8857bd" : "#087e8b"));
                table_->setItem(method, column, item);
            }
        }
        const auto rank = [](double classical, double ai)
        {
            if (std::abs(classical - ai) <= 1e-9)
                return QString("Tie");
            return QString(classical < ai ? "Classical" : "AI · pix_grad");
        };
        winners_->setText(
            QString(
                "THIS AREA ONLY\n\nLower RMSE: %1\nLower MAE: %2\n\nDifferent metrics can rank methods differently. No overall winner is implied.")
                .arg(rank(metrics_[0].rmse, metrics_[1].rmse), rank(metrics_[0].mae, metrics_[1].mae)));
        scope_->setText(
            QString(
                "%1 · %2 common valid reference pixels · evaluated cell footprint %3 m² · reference coverage 100% · bias is estimate − reference")
                .arg(areaName_)
                .arg(metrics_[0].count)
                .arg(metrics_[0].count * .25, 0, 'f', 2));
    }
    void inspect(QPointF point)
    {
        if (!grid_ || busy_)
            return;
        const double col = (point.x() - grid_->transform[0]) / .5, row = (grid_->transform[3] - point.y()) / .5;
        if (!std::isfinite(col) || !std::isfinite(row) || col < 0 || row < 0 || col >= grid_->width ||
            row >= grid_->height)
        {
            statusBar()->showMessage("Click inside the source raster.");
            return;
        }
        const int x = int(col), y = int(row), pixel = y * grid_->width + x;
        probePoint_ = QPointF(grid_->transform[0] + (x + .5) * .5, grid_->transform[3] - (y + .5) * .5);
        probe_->setText(
            QString(
                "SOURCE PIXEL · column %1 / row %2%3\n\nDSM: %4 m\nReference: %5 m\n\nClassical: %6 m · error %7 m\nAI: %8 m · error %9 m")
                .arg(x)
                .arg(y)
                .arg(area_.contains(x, y) ? QString() : QString("\nOutside evaluation area"))
                .arg(grid_->z[0][pixel], 0, 'f', 2)
                .arg(grid_->z[3][pixel], 0, 'f', 2)
                .arg(grid_->z[1][pixel], 0, 'f', 2)
                .arg(double(grid_->z[1][pixel]) - grid_->z[3][pixel], 0, 'f', 3)
                .arg(grid_->z[2][pixel], 0, 'f', 2)
                .arg(double(grid_->z[2][pixel]) - grid_->z[3][pixel], 0, 'f', 3));
        updateOverlays();
    }
    void updateOverlays()
    {
        for (auto* overlay : overlays_)
            if (overlay)
            {
                if (grid_ && !area_.isEmpty())
                    overlay->area = windowExtent(*grid_, area_);
                overlay->probe = probePoint_;
                overlay->update();
            }
    }
    void exportReport()
    {
        if (!grid_ || area_.isEmpty())
            return;
        const QString path =
            QFileDialog::getSaveFileName(this, "Export evaluation report", "accuracy-report.json", "JSON (*.json)");
        if (path.isEmpty())
            return;
        try
        {
            const auto extent = windowExtent(*grid_, area_);
            QJsonArray bounds;
            for (double value : {extent.xMin(), extent.yMin(), extent.xMax(), extent.yMax()})
                bounds.append(value);
            const QJsonObject report{
                {"schema_version", 1},
                {"sample_archive_sha256", assets_.value("sha256")},
                {"source_manifest", grid_->manifest},
                {"area_name", areaName_},
                {"source_pixel_window", QJsonObject{{"column", area_.left()},
                                                    {"row", area_.top()},
                                                    {"width", area_.width()},
                                                    {"height", area_.height()}}},
                {"evaluated_cell_bounds_epsg2056", bounds},
                {"pixel_footprint_m2", metrics_[0].count * .25},
                {"large_error_threshold_m", threshold_->value()},
                {"p95_definition", "Nearest-rank empirical 95th percentile of absolute reference errors"},
                {"histogram_bin_edges_m", QJsonArray{.25, .5, 1., 2., 5.}},
                {"classical", metrics_[0].json()},
                {"ai_pix_grad", metrics_[1].json()},
                {"reference_used_for_inference", false},
                {"metric_scope",
                 "Shared native pixel centres in this selected area; fixed development scene; not held-out generalization accuracy"},
                {"attribution", "Federal Office of Topography swisstopo; AI: David Perez, pix_grad adaptation"}};
            const QByteArray bytes = QJsonDocument(report).toJson();
            QSaveFile file(path);
            require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(),
                    "Cannot save evaluation report.");
            statusBar()->showMessage("Saved selected-area metrics and source provenance.");
        }
        catch (const std::exception& e)
        {
            QMessageBox::warning(this, "Export report", QString::fromUtf8(e.what()));
        }
    }

    const QCommandLineParser& arguments_;
    QJsonObject assets_;
    std::shared_ptr<Grids> grid_;
    std::array<GisViewer*, 3> views_{};
    std::array<AreaOverlay*, 3> overlays_{};
    std::array<QVBoxLayout*, 3> holders_{};
    std::array<QLabel*, 3> headings_{};
    std::array<std::optional<QPointF>, 3> presses_;
    std::array<std::array<GisLayer*, 9>, 2> layers_{};
    std::vector<QAction*> navigation_;
    QAction* areaAction_ = nullptr;
    QComboBox *mode_ = nullptr, *range_ = nullptr, *presets_ = nullptr;
    QDoubleSpinBox* threshold_ = nullptr;
    QLabel *instructions_ = nullptr, *winners_ = nullptr, *probe_ = nullptr, *scope_ = nullptr;
    QTableWidget* table_ = nullptr;
    ColorLegend* legend_ = nullptr;
    ErrorHistogram* histogram_ = nullptr;
    QPushButton *export_ = nullptr, *reload_ = nullptr, *cancel_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QRect area_;
    QString areaName_;
    std::optional<QPointF> probePoint_;
    std::array<Metric, 2> metrics_;
    QTimer poll_;
    QTemporaryDir temporary_;
    std::future<Prepared> pending_;
    std::atomic_bool cancelled_{false};
    std::atomic_int progressValue_{0};
    bool busy_ = false, syncing_ = false, closeQueued_ = false;
    int generation_ = 0;
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("DsmToDtmAccuracy");
    app.setApplicationVersion("1.0");
    auto palette = app.palette();
    palette.setColor(QPalette::Window, QColor("#f2f6f8"));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::WindowText, QColor("#233e50"));
    palette.setColor(QPalette::Text, QColor("#233e50"));
    app.setPalette(palette);
    const QString gdal = QDir(QCoreApplication::applicationDirPath()).filePath("gdal-data");
    if (QDir(gdal).exists())
        qputenv("GDAL_DATA", QDir::toNativeSeparators(gdal).toUtf8());
    QCommandLineParser parser;
    parser.setApplicationDescription("Shared-scale DSM-to-DTM reference error comparison and selected-area metrics.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"data-dir", "Sample cache directory.", "directory"});
    parser.addOption({"sample-archive", "Use the exact pinned local sample ZIP.", "zip"});
    parser.addOption({"sample-url", "HTTPS sample mirror; pinned hashes still apply.", "url"});
    parser.process(app);
    try
    {
        AccuracyWindow window(parser);
        window.show();
        return app.exec();
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(nullptr, "DsmToDtmAccuracy", QString::fromUtf8(e.what()));
        return 1;
    }
}
