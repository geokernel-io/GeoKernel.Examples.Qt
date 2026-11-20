#include <Common/Gdal/GdalElevationProvider.h>
#include <CoordinateSystems/CoordinateSystemFactory.h>
#include <GeoKernelAI.h>
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
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineF>
#include <QMainWindow>
#include <QMessageBox>
#include <QMouseEvent>
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
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
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
    request.setHeader(QNetworkRequest::UserAgentHeader, "GeoKernel-DsmToDtmProfile/1.0");
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
    int width = 0, height = 0, profileRow = 0;

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
    grid->profileRow = grid->manifest.value("profile").toObject().value("row").toInt();
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

    require(grid->profileRow >= 0 && grid->profileRow < grid->height, "Invalid sample profile row.");
    return grid;
}

struct ProfileSample
{
    QPointF point;
    double distance = 0;
    std::array<double, 4> height;
};

struct Profile
{
    QPointF start, end;
    double length = 0, spacing = 0;
    std::vector<ProfileSample> samples;
    std::array<double, 2> rmse{}, mae{};
};

std::optional<double> sampleHeight(const Grids& grid, int band, QPointF point)
{
    const double col = (point.x() - grid.transform[0]) / grid.transform[1] - .5;
    const double row = (point.y() - grid.transform[3]) / grid.transform[5] - .5;
    if (!std::isfinite(col) || !std::isfinite(row) || col < -1e-7 || row < -1e-7 || col > grid.width - 1 + 1e-7 ||
        row > grid.height - 1 + 1e-7)
        return {};
    const double x = std::clamp(col, 0., double(grid.width - 1));
    const double y = std::clamp(row, 0., double(grid.height - 1));
    const int ix = std::min(int(x), grid.width - 2), iy = std::min(int(y), grid.height - 2);
    const double u = x - ix, v = y - iy;
    const auto& values = grid.z[band];
    return (1 - v) * ((1 - u) * values[iy * grid.width + ix] + u * values[iy * grid.width + ix + 1]) +
           v * ((1 - u) * values[(iy + 1) * grid.width + ix] + u * values[(iy + 1) * grid.width + ix + 1]);
}

Profile makeProfile(const Grids& grid, QPointF a, QPointF b, double requestedSpacing)
{
    require(sampleHeight(grid, 0, a).has_value() && sampleHeight(grid, 0, b).has_value(),
            "Choose both endpoints inside the raster pixel centres.");
    require(std::isfinite(requestedSpacing) && requestedSpacing >= .5 && requestedSpacing <= 10,
            "Profile spacing must be between 0.5 and 10 metres.");
    Profile result;
    result.start = a;
    result.end = b;
    result.length = std::hypot(b.x() - a.x(), b.y() - a.y());
    require(result.length >= .5, "Choose endpoints at least 0.5 m apart.");
    const int intervals = std::clamp(int(std::ceil(result.length / requestedSpacing)), 1, 4095);
    result.spacing = result.length / intervals;
    result.samples.reserve(intervals + 1);
    for (int i = 0; i <= intervals; ++i)
    {
        const double t = double(i) / intervals;
        ProfileSample sample;
        sample.point = a * (1 - t) + b * t;
        sample.distance = result.length * t;
        for (int band = 0; band < 4; ++band)
        {
            const auto value = sampleHeight(grid, band, sample.point);
            require(value.has_value(), "The transect crosses unavailable raster data.");
            sample.height[band] = *value;
        }
        for (int method = 0; method < 2; ++method)
        {
            const double error = sample.height[method + 1] - sample.height[3];
            result.rmse[method] += error * error;
            result.mae[method] += std::abs(error);
        }
        result.samples.push_back(sample);
    }
    for (int method = 0; method < 2; ++method)
    {
        result.rmse[method] = std::sqrt(result.rmse[method] / result.samples.size());
        result.mae[method] /= result.samples.size();
    }
    return result;
}

QString writeRelief(const Grids& grid, int band, const QString& output, const std::atomic_bool& cancelled)
{
    const int count = grid.width * grid.height;
    QVector<float> pixels(count * 3);
    const double azimuth = 315. * 3.141592653589793 / 180., altitude = 40. * 3.141592653589793 / 180.;
    const double lx = std::sin(azimuth) * std::cos(altitude), ly = std::cos(azimuth) * std::cos(altitude), lz = std::sin(altitude);
    const auto at = [&](int x, int y)
    {
        return double(grid.z[band][y * grid.width + x]);
    };
    for (int y = 0; y < grid.height; ++y)
    {
        checkpoint(cancelled);
        const int top = std::max(0, y - 1), bottom = std::min(grid.height - 1, y + 1);
        for (int x = 0; x < grid.width; ++x)
        {
            const int left = std::max(0, x - 1), right = std::min(grid.width - 1, x + 1);
            const double dx = (at(right, y) - at(left, y)) / ((right - left) * .5);
            const double dy = (at(x, top) - at(x, bottom)) / ((bottom - top) * .5);
            const double light = (-dx * lx - dy * ly + lz) / std::sqrt(dx * dx + dy * dy + 1);
            const float value = float(std::clamp(int(std::lround(255 * (.25 + .75 * std::max(0., light)))), 0, 255));
            const int index = y * grid.width + x;
            for (int channel = 0; channel < 3; ++channel)
                pixels[channel * count + index] = value;
        }
    }
    GeoKernel::AI::AiRasterInferenceResult raster;
    raster.width = grid.width;
    raster.height = grid.height;
    raster.bandCount = 3;
    raster.dataType = GeoKernel::AI::AiTensorDataType::Float32;
    raster.data =
        QByteArray(reinterpret_cast<const char*>(pixels.constData()), pixels.size() * qsizetype(sizeof(float)));
    raster.geoTransform = grid.transform;
    raster.projectionWkt = grid.projection;
    GeoKernel::AI::AiRasterInferenceExecutor::writeGeoTiff(raster, output);
    return output;
}

class TransectOverlay final : public QWidget
{
public:
    GisViewer* viewer;
    std::optional<QPointF> a, b, cursor;
    explicit TransectOverlay(GisViewer* view) : QWidget(view), viewer(view)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (!a)
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto screen = [&](QPointF point)
        {
            return viewer->worldToScreen(GisShapePoint(point.x(), point.y()));
        };
        const auto start = screen(*a);
        if (b)
        {
            p.setPen(QPen(Qt::white, 5));
            p.drawLine(start, screen(*b));
            p.setPen(QPen(QColor("#087e8b"), 2.5));
            p.drawLine(start, screen(*b));
        }
        const auto marker = [&](QPointF point, QColor color, const QString& label)
        {
            p.setPen(QPen(Qt::white, 2));
            p.setBrush(color);
            p.drawEllipse(point, 7, 7);
            p.setPen(QColor("#173c50"));
            p.setBrush(Qt::white);
            const QRectF box(point.x() + 10, point.y() - 24, 22, 21);
            p.drawRoundedRect(box, 3, 3);
            p.drawText(box, Qt::AlignCenter, label);
        };
        marker(start, QColor("#087e8b"), "A");
        if (b)
            marker(screen(*b), QColor("#d98419"), "B");
        if (cursor)
        {
            const auto point = screen(*cursor);
            p.setBrush(QColor("#dd3470"));
            p.setPen(QPen(Qt::white, 2));
            p.drawEllipse(point, 5, 5);
        }
    }
};

class ProfileChart final : public QWidget
{
public:
    Profile profile;
    bool errors = false;
    int hovered = -1;
    std::function<void(int)> hoverChanged;
    explicit ProfileChart(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName("profileChart");
        setMinimumHeight(230);
        setMouseTracking(true);
    }

protected:

    QRectF plotRect() const
    {
        return QRectF(65, 30, width() - 90, height() - 72);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const auto plot = plotRect();
        hovered = profile.samples.empty() || !plot.contains(event->position())
                      ? -1
                      : std::clamp(int(std::lround((event->position().x() - plot.left()) / plot.width() *
                                                   (profile.samples.size() - 1))),
                                   0, int(profile.samples.size() - 1));
        if (hoverChanged)
            hoverChanged(hovered);
        update();
    }

    void leaveEvent(QEvent*) override
    {
        hovered = -1;
        if (hoverChanged)
            hoverChanged(-1);
        update();
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), Qt::white);
        if (profile.samples.empty())
        {
            p.setPen(QColor("#476174"));
            p.drawText(rect(), Qt::AlignCenter, "Draw a transect: choose A, then B on either map.");
            return;
        }
        const auto plot = plotRect();
        const auto value = [&](const ProfileSample& sample, int band)
        {
            return sample.height[band] - (errors ? sample.height[3] : 0.);
        };
        double low = errors ? 0 : 1e10, high = errors ? 0 : -1e10;
        for (const auto& sample : profile.samples)
            for (int band = errors ? 1 : 0; band < (errors ? 3 : 4); ++band)
            {
                low = std::min(low, value(sample, band));
                high = std::max(high, value(sample, band));
            }
        const double pad = std::max(.25, (high - low) * .12);
        low -= pad;
        high += pad;
        p.setPen(QColor("#476174"));
        p.drawText(QRectF(8, 2, width() - 16, 24), Qt::AlignLeft,
                   errors ? "Estimate minus reference (m) · zero is closest"
                          : "Source elevation (LN02 metres) · all four rasters on one scale");

        for (int i = 0; i <= 4; ++i)
        {
            const double t = i / 4., y = plot.bottom() - t * plot.height();
            p.setPen(QColor("#e2e9ee"));
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            p.setPen(QColor("#476174"));
            p.drawText(QRectF(0, y - 9, 58, 18), Qt::AlignRight, QString::number(low + t * (high - low), 'f', 1));
            p.drawText(QRectF(plot.left() + t * plot.width() - 28, plot.bottom() + 5, 56, 18), Qt::AlignCenter,
                       QString::number(t * profile.length, 'f', 1));
        }

        p.drawText(QRectF(0, height() - 22, width(), 20), Qt::AlignCenter, "Distance along A → B (projected metres)");
        
        if (errors)
        {
            const double y = plot.bottom() - (0 - low) / (high - low) * plot.height();
            p.setPen(QPen(QColor("#b4bfca"), 1, Qt::DashLine));
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        
        const std::array<QColor, 4> colors = {
            QColor("#66768a"), 
            QColor("#087e8b"), 
            QColor("#8857bd"),
            QColor("#d98419")
        };
        
        for (int band : {0, 3, 1, 2})
        {
            if (errors && (band == 0 || band == 3))
                continue;
            
            QPainterPath path;
            for (size_t i = 0; i < profile.samples.size(); ++i)
            {
                const auto& sample = profile.samples[i];
                const QPointF point(plot.left() + sample.distance / profile.length * plot.width(),
                                    plot.bottom() - (value(sample, band) - low) / (high - low) * plot.height());
                if (!i)
                    path.moveTo(point);
                else
                    path.lineTo(point);
            }
            QPen pen(colors[band], band == 0 ? 1.5 : 2);
            if (band == 3)
                pen.setStyle(Qt::DashLine);
            p.setPen(pen);
            p.drawPath(path);
        }

        if (hovered >= 0 && hovered < int(profile.samples.size()))
        {
            const double x = plot.left() + profile.samples[hovered].distance / profile.length * plot.width();
            p.setPen(QPen(QColor("#dd3470"), 1, Qt::DashLine));
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }
    }
};

struct Prepared
{
    std::shared_ptr<Grids> grid;
    std::array<QString, 4> relief;
    QString error;
};

class ProfileWindow final : public QMainWindow
{
public:
    explicit ProfileWindow(const QCommandLineParser& arguments)
        : arguments_(arguments), assets_(readJson(":/sample-assets.json"))
    {
        setWindowTitle("DSM to DTM | Interactive elevation profile — GeoKernel");
        resize(1460, 940);
        setWindowIcon(QIcon(":/icons/geokernel.ico"));
        setStyleSheet(
            "QMainWindow,QWidget#page{background:#f2f6f8;color:#233e50;} QLabel#title{font-size:21px;font-weight:600;color:#145e72;}"
            "QLabel#heading{background:#e4eef2;color:#125a70;padding:9px;font-weight:600;}"
            "QLabel#card{background:white;border:1px solid #d5e2e9;padding:10px;} QPushButton{padding:7px;}"
            "QToolBar{background:white;border-bottom:1px solid #d5e2e9;spacing:8px;padding:7px;}");
        const auto label = [](const QString& text, const char* name = nullptr)
        {
            auto* l = new QLabel(text);
            l->setWordWrap(true);
            if (name)
                l->setObjectName(name);
            return l;
        };
        auto* page = new QWidget;
        page->setObjectName("page");
        auto* layout = new QHBoxLayout(page);
        layout->setSpacing(12);
        auto* sidebar = new QWidget;
        auto* controls = new QVBoxLayout(sidebar);
        controls->setContentsMargins(0, 0, 10, 0);
        controls->setSpacing(12);
        controls->addWidget(label("Compare along\nyour own transect", "title"));
        controls->addWidget(label("Zürich · 250 × 250 m\n0.5 m height pixels · EPSG:2056"));
        instructions_ = label("Preparing sample data...", "card");
        controls->addWidget(instructions_);
        controls->addWidget(label("Right-hand map"));
        surface_ = new QComboBox;
        surface_->setObjectName("surfaceChoice");
        surface_->addItems({"Classical DTM", "AI DTM · pix_grad", "Reference DTM · evaluation only"});
        controls->addWidget(surface_);
        spacing_ = new QDoubleSpinBox;
        spacing_->setObjectName("profileSpacing");
        spacing_->setRange(.5, 10);
        spacing_->setValue(.5);
        spacing_->setSingleStep(.5);
        spacing_->setSuffix(" m");
        auto* form = new QFormLayout;
        form->addRow("Sample spacing", spacing_);
        controls->addLayout(form);
        stats_ = label("Select a transect to compare errors.", "card");
        stats_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        controls->addWidget(stats_);
        cursor_ = label("Move over the chart to locate a sample on both maps.", "card");
        cursor_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        controls->addWidget(cursor_);
        controls->addWidget(label(
            "Profiles sample numeric Float32 heights with bilinear interpolation. Relief images are for display only. Finer sampling cannot add source detail."));
        controls->addStretch();
        export_ = new QPushButton("Export profile CSV");
        export_->setObjectName("exportCsv");
        controls->addWidget(export_);
        reload_ = new QPushButton("Retry / reload sample");
        cancel_ = new QPushButton("Cancel loading");
        controls->addWidget(reload_);
        controls->addWidget(cancel_);
        controls->addWidget(
            label("Source: Federal Office of Topography swisstopo\nAI: David Perez · experimental pix_grad"));
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setFixedWidth(285);
        scroll->setWidget(sidebar);
        layout->addWidget(scroll);
        auto* workspace = new QVBoxLayout;
        auto* split = new QSplitter(Qt::Vertical);
        auto* mapRow = new QWidget;
        auto* maps = new QHBoxLayout(mapRow);
        maps->setContentsMargins(0, 0, 0, 0);
        maps->setSpacing(8);

        for (int i = 0; i < 2; ++i)
        {
            auto* pane = new QWidget;
            auto* column = new QVBoxLayout(pane);
            column->setContentsMargins(0, 0, 0, 0);
            column->setSpacing(0);
            headings_[i] = label(i == 0 ? "ORIGINAL DSM · roofs + trees + ground" : "CLASSICAL DTM · prepared estimate",
                                 "heading");
            column->addWidget(headings_[i]);
            holders_[i] = new QVBoxLayout;
            column->addLayout(holders_[i], 1);
            maps->addWidget(pane, 1);
        }

        split->addWidget(mapRow);
        auto* profilePane = new QWidget;
        auto* chartLayout = new QVBoxLayout(profilePane);
        chartLayout->setContentsMargins(0, 0, 0, 0);
        mode_ = new QComboBox;
        mode_->setObjectName("chartMode");
        mode_->addItems({"Compare elevations", "Compare errors against reference"});
        chartLayout->addWidget(mode_);
        chart_ = new ProfileChart;
        chartLayout->addWidget(chart_, 1);
        legend_ = label("Gray DSM · teal classical · purple AI · dashed orange reference");
        chartLayout->addWidget(legend_);
        split->addWidget(profilePane);
        split->setSizes({540, 270});
        workspace->addWidget(split, 1);
        workspace->addWidget(label(
            "Prepared independent results; no new inference. Reference is evaluation-only. Source LN02 heights, metres; no vertical datum conversion. DSM/reference source years: 2018/2019. This is a fixed development scene."));
        layout->addLayout(workspace, 1);
        setCentralWidget(page);
        auto* toolbar = addToolBar("Profile and navigation");
        toolbar->setMovable(false);
        toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        draw_ = toolbar->addAction("Draw transect · A → B");
        draw_->setObjectName("drawTransect");
        draw_->setCheckable(true);
        auto* example = toolbar->addAction("Example transect");
        example->setObjectName("exampleTransect");
        auto* clear = toolbar->addAction("Clear transect");
        clear->setObjectName("clearTransect");
        toolbar->addSeparator();
        auto* pan = toolbar->addAction(QIcon(":/icons/pan.svg"), "Pan");
        auto* box = toolbar->addAction(QIcon(":/icons/zoom-box.svg"), "Zoom box");
        auto* in = toolbar->addAction(QIcon(":/icons/zoom-in.svg"), "Zoom in");
        auto* out = toolbar->addAction(QIcon(":/icons/zoom-out.svg"), "Zoom out");
        auto* fit = toolbar->addAction(QIcon(":/icons/full-extent.svg"), "Full extent");
        navigation_ = {example, clear, pan, box, in, out, fit};
        connect(pan, &QAction::triggered, this,
                [this]
                {
                    draw_->setChecked(false);
                    setTool(GisViewerTool::Pan);
                });
        connect(box, &QAction::triggered, this,
                [this]
                {
                    draw_->setChecked(false);
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
        connect(draw_, &QAction::toggled, this,
                [this](bool on)
                {
                    draft_.reset();
                    if (on)
                    {
                        setTool(GisViewerTool::Pan);
                        instructions_->setText("Click A on either map. Dragging still pans; wheel zooms.");
                    }
                    else
                        instructions_->setText("Draw a new A → B transect, or inspect the current profile.");
                    updateOverlays();
                });
        connect(example, &QAction::triggered, this,
                [this]
                {
                    exampleProfile();
                });
        connect(clear, &QAction::triggered, this,
                [this]
                {
                    draw_->setChecked(false);
                    draft_.reset();
                    chart_->profile = {};
                    chart_->hovered = -1;
                    chart_->update();
                    refreshStats();
                    hover(-1);
                    updateOverlays();
                });
        connect(surface_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this]
                {
                    selectSurface();
                });
        connect(spacing_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this]
                {
                    if (!chart_->profile.samples.empty())
                        commitProfile(chart_->profile.start, chart_->profile.end);
                });
        connect(mode_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this](int index)
                {
                    chart_->errors = index == 1;
                    legend_->setText(index ? "Teal classical − reference · purple AI − reference · zero is closest"
                                           : "Gray DSM · teal classical · purple AI · dashed orange reference");
                    chart_->update();
                });
        chart_->hoverChanged = [this](int index)
        {
            hover(index);
        };
        connect(export_, &QPushButton::clicked, this,
                [this]
                {
                    exportCsv();
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
                    statusBar()->showMessage("Cancelling loading...");
                });
        progress_ = new QProgressBar;
        progress_->setFixedWidth(200);
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

    ~ProfileWindow() override
    {
        cancelled_ = true;
        poll_.stop();
        if (pending_.valid())
            pending_.wait();
        for (auto* viewer : views_)
            delete viewer;
    }

protected:

    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (event->type() == QEvent::Resize)
            for (int i = 0; i < 2; ++i)
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

    void setBusy(bool busy)
    {
        busy_ = busy;
        reload_->setEnabled(!busy);
        cancel_->setEnabled(busy);
        progress_->setVisible(busy);
        draw_->setEnabled(!busy && bool(grid_));
        surface_->setEnabled(!busy && bool(grid_));
        spacing_->setEnabled(!busy && bool(grid_));
        export_->setEnabled(!busy && !chart_->profile.samples.empty());
        for (auto* action : navigation_)
            action->setEnabled(!busy && bool(grid_));
    }
    void start()
    {
        if (busy_)
            return;
        setBusy(true);
        cancelled_ = false;
        progressValue_ = 0;
        try
        {
            const QString cache = arguments_.isSet("data-dir")
                                      ? QFileInfo(arguments_.value("data-dir")).absoluteFilePath()
                                      : QDir(QCoreApplication::applicationDirPath()).filePath("data");
            const QUrl url(arguments_.isSet("sample-url") ? arguments_.value("sample-url")
                                                          : assets_.value("url").toString());
            const QString folder = prepareSample(assets_, cache, arguments_.value("sample-archive"), url, cancelled_,
                                                 [this](int percent, const QString& message)
                                                 {
                                                     progress_->setValue(percent);
                                                     statusBar()->showMessage(message);
                                                 });
            checkpoint(cancelled_);
            configureSampleRuntime(folder);
            const QString temporary = QDir(temporary_.path()).filePath(QString::number(++generation_));
            require(QDir().mkpath(temporary), "Cannot create a temporary relief directory.");
            statusBar()->showMessage("Reading numeric heights and preparing matching relief maps...");
            pending_ =
                std::async(std::launch::async,
                           [this, folder, temporary]
                           {
                               Prepared result;
                               try
                               {
                                   result.grid = readGrids(folder, cancelled_);
                                   for (int band = 0; band < 4; ++band)
                                   {
                                       result.relief[band] = writeRelief(
                                           *result.grid, band,
                                           QDir(temporary).filePath(QString("relief-%1.tif").arg(band)), cancelled_);
                                       progressValue_ = (band + 1) * 25;
                                   }
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
            QMessageBox::warning(this, "Unable to load sample", error);
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
            failure(cancelled_ ? "Loading cancelled." : result.error);
            return;
        }
        try
        {
            grid_ = result.grid;
            const auto crs = CoordinateSystemFactory::fromEpsg(2056);
            require(bool(crs), "EPSG:2056 is unavailable.");
            for (int pane = 0; pane < 2; ++pane)
            {
                if (!views_[pane])
                {
                    views_[pane] = new GisViewer;
                    views_[pane]->setObjectName(pane ? "resultMap" : "originalMap");
                    views_[pane]->setMinimumSize(300, 280);
                    holders_[pane]->addWidget(views_[pane]);
                    views_[pane]->setCoordinateSystem(crs);
                    views_[pane]->setActiveTool(GisViewerTool::Pan);
                    overlays_[pane] = new TransectOverlay(views_[pane]);
                    overlays_[pane]->setGeometry(views_[pane]->rect());
                    overlays_[pane]->show();
                    views_[pane]->installEventFilter(this);
                    connect(views_[pane], &GisViewer::visibleExtentChanged, this,
                            [this, pane](const GisExtent& extent)
                            {
                                if (!syncing_ && views_[1 - pane])
                                {
                                    syncing_ = true;
                                    views_[1 - pane]->setViewExtent(extent);
                                    syncing_ = false;
                                }
                                updateOverlays();
                            });
                    connect(views_[pane], &GisViewer::mapMouseDown, this,
                            [this, pane](GisViewerTool, const QPointF& screen, const GisShapePoint&,
                                         Qt::MouseButton button, Qt::KeyboardModifiers)
                            {
                                if (button == Qt::LeftButton)
                                    presses_[pane] = screen;
                            });
                    connect(views_[pane], &GisViewer::mapMouseUp, this,
                            [this, pane](GisViewerTool, const QPointF& screen, const GisShapePoint& world,
                                         Qt::MouseButton button, Qt::KeyboardModifiers)
                            {
                                if (button != Qt::LeftButton)
                                    return;
                                const auto press = presses_[pane];
                                presses_[pane].reset();
                                if (press && QLineF(*press, screen).length() <= 3.)
                                    pick(QPointF(world.x(), world.y()));
                            });
                }
                while (views_[pane]->layerCount())
                    views_[pane]->removeLayer(0);
                for (int band = 0; band < (pane == 0 ? 1 : 4); ++band)
                {
                    std::vector<GeoKernel::Core::Layers::GisLayer*> previous;
                    for (int index = 0; index < views_[pane]->layerCount(); ++index)
                        previous.push_back(views_[pane]->mapLayerAt(index));
                    QString error;
                    require(views_[pane]->addLayerFromPath(result.relief[band], &error), error);
                    GeoKernel::Core::Layers::GisLayer* added = nullptr;
                    for (int index = 0; index < views_[pane]->layerCount(); ++index)
                    {
                        auto* candidate = views_[pane]->mapLayerAt(index);
                        if (std::find(previous.begin(), previous.end(), candidate) == previous.end())
                        {
                            require(!added, "Unexpected extra raster layer.");
                            added = candidate;
                        }
                    }
                    require(added, "The raster layer was not added.");
                    layers_[pane][band] = added;
                    const QStringList names = {"Original DSM", "Classical DTM", "AI DTM", "Reference DTM"};
                    added->setName(names[band]);
                    added->setVisible(pane == 0 ? band == 0 : band == surface_->currentIndex() + 1);
                }
                views_[pane]->refreshLayers();
            }
            fullExtent();
            selectSurface();
            setBusy(false);
            exampleProfile();
            statusBar()->showMessage("Ready · draw A → B on either map; move over the chart to inspect heights.");
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

    void fullExtent()
    {
        if (views_[0])
            views_[0]->setViewExtent(GisExtent(2677090, 1251340, 2677360, 1251610));
    }

    void selectSurface()
    {
        if (!views_[1])
            return;
        for (int band = 0; band < 4; ++band)
            layers_[1][band]->setVisible(band == surface_->currentIndex() + 1);
        headings_[1]->setText(surface_->currentIndex() == 0   ? "CLASSICAL DTM · prepared estimate"
                              : surface_->currentIndex() == 1 ? "AI DTM · experimental pix_grad"
                                                              : "REFERENCE DTM · evaluation only");
        views_[1]->refreshLayers();
        updateOverlays();
    }

    void pick(QPointF point)
    {
        if (!grid_ || busy_ || !draw_->isChecked())
            return;
        if (!sampleHeight(*grid_, 0, point))
        {
            statusBar()->showMessage("Click inside the valid raster pixel centres.");
            return;
        }
        if (!draft_)
        {
            draft_ = point;
            instructions_->setText("A selected. Click B on either map. Toggle Draw off to cancel.");
            updateOverlays();
            return;
        }
        try
        {
            const auto a = *draft_;
            commitProfile(a, point);
            draft_.reset();
            draw_->setChecked(false);
            instructions_->setText(
                "Your transect is ready. Hover over the chart to locate matching heights on both maps.");
        }
        catch (const std::exception& e)
        {
            statusBar()->showMessage(QString::fromUtf8(e.what()));
        }
    }

    void exampleProfile()
    {
        if (!grid_)
            return;
        draw_->setChecked(false);
        draft_.reset();
        const double y = grid_->transform[3] + (grid_->profileRow + .5) * grid_->transform[5];
        commitProfile(QPointF(grid_->transform[0] + .25, y),
                      QPointF(grid_->transform[0] + (grid_->width - .5) * .5, y));
        instructions_->setText(
            "Example transect loaded. Select Draw transect, then click A and B to compare your own line.");
    }

    void commitProfile(QPointF a, QPointF b)
    {
        auto result = makeProfile(*grid_, a, b, spacing_->value());
        chart_->profile = std::move(result);
        chart_->hovered = -1;
        chart_->update();
        refreshStats();
        hover(-1);
        updateOverlays();
    }

    void refreshStats()
    {
        const auto& profile = chart_->profile;
        export_->setEnabled(!busy_ && !profile.samples.empty());
        if (profile.samples.empty())
        {
            stats_->setText("No transect selected.");
            return;
        }
        stats_->setText(
            QString(
                "THIS TRANSECT ONLY\n\nLength: %1 m · %2 samples\nActual spacing: %3 m\n\nClassical RMSE: %4 m\nClassical MAE: %5 m\n\nAI RMSE: %6 m\nAI MAE: %7 m\n\nLower error is better. These are line-sample errors, not full-area accuracy.")
                .arg(profile.length, 0, 'f', 1)
                .arg(profile.samples.size())
                .arg(profile.spacing, 0, 'f', 3)
                .arg(profile.rmse[0], 0, 'f', 2)
                .arg(profile.mae[0], 0, 'f', 2)
                .arg(profile.rmse[1], 0, 'f', 2)
                .arg(profile.mae[1], 0, 'f', 2));
    }

    void hover(int index)
    {
        if (index < 0 || index >= int(chart_->profile.samples.size()))
            cursor_->setText("Move over the chart to locate a sample on both maps.");
        else
        {
            const auto& sample = chart_->profile.samples[index];
            cursor_->setText(
                QString("Distance: %1 m\nE %2 · N %3\n\nDSM: %4 m\nClassical: %5 m\nAI: %6 m\nReference: %7 m")
                    .arg(sample.distance, 0, 'f', 1)
                    .arg(sample.point.x(), 0, 'f', 2)
                    .arg(sample.point.y(), 0, 'f', 2)
                    .arg(sample.height[0], 0, 'f', 2)
                    .arg(sample.height[1], 0, 'f', 2)
                    .arg(sample.height[2], 0, 'f', 2)
                    .arg(sample.height[3], 0, 'f', 2));
        }
        updateOverlays();
    }

    void updateOverlays()
    {
        for (auto* overlay : overlays_)
            if (overlay)
            {
                overlay->a = draft_;
                overlay->b.reset();
                overlay->cursor.reset();
                if (!draft_ && !chart_->profile.samples.empty())
                {
                    overlay->a = chart_->profile.start;
                    overlay->b = chart_->profile.end;
                    if (chart_->hovered >= 0 && chart_->hovered < int(chart_->profile.samples.size()))
                        overlay->cursor = chart_->profile.samples[chart_->hovered].point;
                }
                overlay->update();
            }
    }

    void exportCsv()
    {
        if (chart_->profile.samples.empty())
            return;
        const QString path = QFileDialog::getSaveFileName(this, "Export numeric profile", "profile.csv", "CSV (*.csv)");
        if (path.isEmpty())
            return;
        try
        {
            QByteArray csv =
                "distance_m,easting_epsg2056_m,northing_epsg2056_m,dsm_ln02_m,classical_ln02_m,ai_ln02_m,reference_ln02_m,classical_error_m,ai_error_m\n";
            for (const auto& sample : chart_->profile.samples)
            {
                const std::array<double, 9> values = {sample.distance,
                                                      sample.point.x(),
                                                      sample.point.y(),
                                                      sample.height[0],
                                                      sample.height[1],
                                                      sample.height[2],
                                                      sample.height[3],
                                                      sample.height[1] - sample.height[3],
                                                      sample.height[2] - sample.height[3]};
                for (size_t i = 0; i < values.size(); ++i)
                {
                    if (i)
                        csv += ',';
                    csv += QByteArray::number(values[i], 'f', 6);
                }
                csv += '\n';
            }
            QSaveFile file(path);
            require(file.open(QIODevice::WriteOnly), "Cannot open CSV output.");
            require(file.write(csv) == csv.size() && file.commit(), "Cannot save CSV output.");
            const QJsonObject metadata{
                {"sample_archive_sha256", assets_.value("sha256")},
                {"source", grid_->manifest},
                {"sampling", "Bilinear numeric heights, projected metric distances"},
                {"line_length_m", chart_->profile.length},
                {"sample_spacing_m", chart_->profile.spacing},
                {"sample_count", int(chart_->profile.samples.size())},
                {"source_attribution", "Federal Office of Topography swisstopo; AI: David Perez, pix_grad adaptation"}};
            QSaveFile sidecar(path + ".metadata.json");
            require(sidecar.open(QIODevice::WriteOnly), "CSV saved; cannot open metadata sidecar.");
            const auto bytes = QJsonDocument(metadata).toJson();
            require(sidecar.write(bytes) == bytes.size() && sidecar.commit(),
                    "CSV saved; cannot save metadata sidecar.");
            statusBar()->showMessage("Saved numeric profile CSV and metadata sidecar.");
        }
        catch (const std::exception& e)
        {
            QMessageBox::warning(this, "Export profile", QString::fromUtf8(e.what()));
        }
    }

    const QCommandLineParser& arguments_;
    QJsonObject assets_;
    std::shared_ptr<Grids> grid_;
    std::array<GisViewer*, 2> views_{};
    std::array<std::array<GeoKernel::Core::Layers::GisLayer*, 4>, 2> layers_{};
    std::array<TransectOverlay*, 2> overlays_{};
    std::array<QVBoxLayout*, 2> holders_{};
    std::array<QLabel*, 2> headings_{};
    std::array<std::optional<QPointF>, 2> presses_;
    std::vector<QAction*> navigation_;
    ProfileChart* chart_ = nullptr;
    QAction* draw_ = nullptr;
    QComboBox *surface_ = nullptr, *mode_ = nullptr;
    QDoubleSpinBox* spacing_ = nullptr;
    QLabel *instructions_ = nullptr, *stats_ = nullptr, *cursor_ = nullptr, *legend_ = nullptr;
    QPushButton *export_ = nullptr, *reload_ = nullptr, *cancel_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QTimer poll_;
    QTemporaryDir temporary_;
    std::future<Prepared> pending_;
    std::atomic_bool cancelled_{false};
    std::atomic_int progressValue_{0};
    std::optional<QPointF> draft_;
    bool busy_ = false, syncing_ = false, closeQueued_ = false;
    int generation_ = 0;
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("DsmToDtmProfile");
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
    parser.setApplicationDescription("Interactive DSM / classical / AI / reference elevation profiles.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"data-dir", "Sample cache location.", "directory"});
    parser.addOption({"sample-archive", "Use the exact pinned local sample ZIP.", "zip"});
    parser.addOption({"sample-url", "HTTPS sample mirror; all pinned hashes still apply.", "url"});
    parser.process(app);
    try
    {
        ProfileWindow window(parser);
        window.show();
        return app.exec();
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(nullptr, "DsmToDtmProfile", QString::fromUtf8(e.what()));
        return 1;
    }
}
