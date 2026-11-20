#include <GeoKernel.Viewer3D/ViewerController.h>
#include <Common/Gdal/GdalElevationProvider.h>
#include <CoordinateSystems/CoordinateSystemFactory.h>
#include <GeoKernelAI.h>
#include <QApplication>
#include <QCloseEvent>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
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
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QVulkanInstance>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <future>
#include <memory>
#include <stdexcept>

using namespace GeoKernel3D;
using namespace GeoKernel::Formats::Common::Gdal;

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
    request.setHeader(QNetworkRequest::UserAgentHeader, "GeoKernel-DsmToDtm/1.0");
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    request.setTransferTimeout(60000);
    
    auto* reply = manager.get(request);
    
    QEventLoop loop;
    bool writeFailed = false;

    QTimer cancelPoll;
    cancelPoll.setInterval(50);
    
    QObject::connect(&cancelPoll, &QTimer::timeout, reply, [&] {
        if (cancelled)
            reply->abort();
    });

    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
        const auto data = reply->readAll();
        if (file.write(data) != data.size()) {
            writeFailed = true;
            reply->abort();
        }
    });

    QObject::connect(reply, &QNetworkReply::downloadProgress, &loop, [&](qint64 done, qint64 total) {
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
        .arg(status).arg(reply->errorString(), url.toString()));

    require(file.commit(), "Could not save the downloaded sample archive.");
}

QString prepareSample(const QJsonObject& assets, const QString& root, const QString& localArchive, const QUrl& url, std::atomic_bool& cancelled, const Progress& progress)
{
    QDir().mkpath(root);
    const QString folder = QDir(root).filePath(assets.value("cache").toString());
    progress(0, "Checking cached sample files...");
    if (validSample(folder, assets))
        return folder;

    const QString archive = localArchive.isEmpty() ? QDir(root).filePath(assets.value("archive").toString()) : localArchive;
    const QByteArray expected = assets.value("sha256").toString().toLatin1();
    
    if (localArchive.isEmpty() && hashFile(archive) != expected)
        download(url, archive, cancelled, progress);
    
    checkpoint(cancelled);
    
    require(hashFile(archive) == expected, "Sample ZIP SHA-256 mismatch. The existing data was retained.");
    
    QTemporaryDir staging(QDir(root).filePath(".extract-XXXXXX"));
    require(staging.isValid(), "Cannot create the sample extraction directory.");

#ifdef Q_OS_WIN
    const QString program = QStandardPaths::findExecutable("tar");
    const QStringList arguments = { "-xf", QDir::toNativeSeparators(QFileInfo(archive).absoluteFilePath()) };
#else
    const QString program = QStandardPaths::findExecutable("unzip");
    const QStringList arguments = { "-o", QFileInfo(archive).absoluteFilePath() };
#endif
    require(!program.isEmpty(), "Archive extractor not found (Windows: tar; Linux: unzip).");
    progress(0, "Extracting verified sample ZIP...");
    QProcess extractor;
    extractor.setWorkingDirectory(staging.path());
    QEventLoop loop;
    QTimer timer, timeout;
    timer.setInterval(50);
    timeout.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] { if (cancelled) extractor.kill(); });
    QObject::connect(&timeout, &QTimer::timeout, &extractor, &QProcess::kill);
    QObject::connect(&extractor, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &loop, &QEventLoop::quit);
    QObject::connect(&extractor, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
    extractor.start(program, arguments);
    timer.start();
    timeout.start(120000);
    
    if (extractor.state() != QProcess::NotRunning)
        loop.exec();
    
    checkpoint(cancelled);
    
    require(extractor.state() == QProcess::NotRunning && extractor.exitStatus() == QProcess::NormalExit && extractor.exitCode() == 0, "Sample ZIP extraction failed: " + QString::fromLocal8Bit(extractor.readAllStandardError()));
    require(validSample(staging.path(), assets), "Extracted sample files failed SHA-256 verification.");
    
    if (QFileInfo::exists(folder))
        require(QDir().rename(folder, folder + ".invalid-" + QString::number(QDateTime::currentMSecsSinceEpoch())),
            "Cannot preserve the old sample cache.");
    require(QDir().rename(staging.path(), folder), "Cannot install the verified sample data.");
    staging.setAutoRemove(false);
    
    return folder;
}

struct Grids {
    QString folder, projection;
    int width = 0, height = 0, profileRow = 0;
    
    QVector<double> transform;
    std::array<QVector<float>, 4> z;
    QJsonObject manifest;
};

void configureSampleRuntime(const QString& folder)
{
    const QString sdkData = QDir(QCoreApplication::applicationDirPath()).filePath("proj-data");
    const QString projPath = QFileInfo::exists(QDir(sdkData).filePath("proj.db")) ? sdkData : QDir(folder).filePath("proj-data");
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
    const QStringList names = { "dsm.tif", "dtm_classical.tif", "dtm_ai.tif", "dtm_reference.tif" };
    for (int i = 0; i < names.size(); ++i) {
        checkpoint(cancelled);
        GdalElevationProvider provider;
        QString error;
        require(provider.open(QDir(folder).filePath(names[i]), 1, &error), error);
        const auto m = provider.metadata();
        const auto& t = m.worldTransform;
        QVector<double> transform = {
            t.upperLeftCenterX - (t.pixelSizeX + t.rotationY) * .5,
            t.pixelSizeX,
            t.rotationY,
            t.upperLeftCenterY - (t.rotationX + t.pixelSizeY) * .5, t.rotationX, t.pixelSizeY
        };

        const auto crs = GeoKernel::Core::CoordinateSystems::CoordinateSystemFactory::fromUserInput(m.projectionWkt);
        const auto expectedCrs = GeoKernel::Core::CoordinateSystems::CoordinateSystemFactory::fromEpsg(2056);
        require(crs && expectedCrs && GeoKernel::Core::CoordinateSystems::CoordinateSystemFactory::areEquivalent(*crs, *expectedCrs), "The sample height CRS is not equivalent to EPSG:2056.");
        require(m.hasWorldTransform && m.width == 500 && m.height == 500 &&
            t.pixelSizeX == .5 && t.pixelSizeY == -.5 && t.rotationX == 0 && t.rotationY == 0,
            QString("The sample requires the pinned 500 x 500, 0.5 m grid. Found %1 x %2; transform valid %3; pixel %4, %5; rotation %6, %7.")
            .arg(m.width).arg(m.height).arg(m.hasWorldTransform).arg(t.pixelSizeX).arg(t.pixelSizeY).arg(t.rotationX).arg(t.rotationY));
        if (!i) {
            grid->width = m.width; grid->height = m.height;
            grid->projection = m.projectionWkt; grid->transform = transform;
        }
        else {
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

QVector<float> blended(const Grids& grid, int method, int percent)
{
    require((method == 1 || method == 2) && percent >= 0 && percent <= 100, "Invalid surface selection.");
    if (!percent) return grid.z[0];
    if (percent == 100) return grid.z[method];
    QVector<float> result(grid.z[0].size());
    const double t = percent / 100.;
    for (qsizetype i = 0; i < result.size(); ++i)
        result[i] = float((1. - t) * grid.z[0][i] + t * grid.z[method][i]);
    return result;
}

QString surfaceFile(const Grids& grid, int method, int percent, const QString& temporary)
{
    if (!percent) return QDir(grid.folder).filePath("dsm.tif");
    if (percent == 100)
        return QDir(grid.folder).filePath(method == 1 ? "dtm_classical.tif" : "dtm_ai.tif");

    const auto heights = blended(grid, method, percent);
    GeoKernel::AI::AiRasterInferenceResult raster;
    raster.width = grid.width; raster.height = grid.height; raster.bandCount = 1;
    raster.dataType = GeoKernel::AI::AiTensorDataType::Float32;
    raster.data = QByteArray(reinterpret_cast<const char*>(heights.constData()), heights.size() * qsizetype(sizeof(float)));
    raster.geoTransform = grid.transform; raster.projectionWkt = grid.projection; raster.noDataValue = -9999.;
    GeoKernel::AI::AiRasterInferenceExecutor::writeGeoTiff(raster, temporary);
    return temporary;
}

std::unique_ptr<LoadedScene> prepareScene(const Grids& grid, const QString& path, std::atomic_bool& cancelled, std::atomic_int& progress)
{
    SceneOptions options;
    options.demFile = path;
    options.imageFile = QDir(grid.folder).filePath("neutral_surface.tif");
    options.roadFile.clear(); options.buildingFile.clear();
    options.terrainResolution = 500;
    options.overrideHeightReference = true;
    options.height.datum = HeightDatum::Unknown;
    options.height.unit = "m"; options.height.unitAssumed = false;
    
    LoadControl control;
    control.cancellationFlag = &cancelled;
    control.cancelled = [&] { return cancelled.load(); };
    control.progress = [&](int value, const QString&) { progress = value; };
    QString error;
    auto result = loadPreparedScene(grid.folder, options, error, control);
    
    require(bool(result), error);
    return result;
}

CameraState mappedCamera(const ViewerController& source, const ViewerController& target)
{
    auto camera = source.camera();
    const auto a = source.reference(), b = target.reference();
    if (!a || !b) return camera;
    const auto enu = a->fromRender({ camera.x, camera.y, camera.z }, source.heightScale());
    const auto point = b->toRender(b->ecefToEnu(a->enuToEcef(enu)), target.heightScale());
    camera.x = float(point.x); camera.y = float(point.y); camera.z = float(point.z);
    camera.distance = float(camera.distance * b->gpuScale / a->gpuScale);
    return camera;
}

bool sameCamera(const CameraState& a, const CameraState& b)
{
    return std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z) + std::abs(a.yaw - b.yaw) + std::abs(a.pitch - b.pitch) + std::abs(a.distance - b.distance) < .00001f;
}

class ProfileChart final : public QWidget {
public:
    
    std::shared_ptr<const Grids> grid;
    
    int selected = 1;
    
    explicit ProfileChart(QWidget* parent = nullptr) : QWidget(parent) { 
        setMinimumHeight(190); 
    }
    
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.fillRect(rect(), Qt::white);
        if (!grid) return;
        const QRectF plot(64, 30, width() - 88, height() - 64);
        const std::array<QColor, 4> colors = { QColor("#66768a"), QColor("#087e8b"), QColor("#8857bd"), QColor("#d98419") };
        double low = 1e9, high = -1e9;
        for (const auto& values : grid->z)
            for (int x = 0; x < grid->width; ++x) {
                const double v = values[grid->profileRow * grid->width + x];
                low = std::min(low, v); high = std::max(high, v);
            }
        low -= 2; high += 2;
        p.setPen(QColor("#476174"));
        p.drawText(QRectF(8, 3, width() - 16, 22), Qt::AlignLeft, "Fixed building transect · original pixel heights (LN02 metres)");
        for (int i = 0; i <= 4; ++i) {
            const double t = i / 4.; const double y = plot.bottom() - t * plot.height();
            p.setPen(QColor("#e2e9ee")); p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            p.setPen(QColor("#476174"));
            p.drawText(QRectF(0, y - 9, 56, 18), Qt::AlignRight, QString::number(low + t * (high - low), 'f', 0));
            p.drawText(QRectF(plot.left() + t * plot.width() - 25, plot.bottom() + 5, 50, 18), Qt::AlignCenter, QString::number(t * 249.5, 'f', 0) + " m");
        }
        for (int index : {0, 3, 1, 2}) {
            QPainterPath path;
            for (int x = 0; x < grid->width; ++x) {
                const double value = grid->z[index][grid->profileRow * grid->width + x];
                const QPointF point(plot.left() + double(x) / (grid->width - 1) * plot.width(), plot.bottom() - (value - low) / (high - low) * plot.height());
                if (!x) path.moveTo(point); else path.lineTo(point);
            }
            QPen pen(colors[index], index == selected ? 2.5 : 1.4);
            if (index == 3) pen.setStyle(Qt::DashLine);
            p.setPen(pen); p.drawPath(path);
        }
    }
};

struct Work {
    std::shared_ptr<Grids> grid;
    std::unique_ptr<LoadedScene> original, result;
    int method = 1, percent = 100;
    QString error;
};

class DemoWindow final : public QMainWindow {
public:
    DemoWindow(QVulkanInstance& instance, const QCommandLineParser& arguments)
        : assets_(readJson(":/sample-assets.json")), arguments_(arguments)
    {
        setWindowTitle("DSM to DTM | Ground beneath buildings — GeoKernel");
        resize(1520, 960);
        setWindowIcon(QIcon(":/icons/geokernel.ico"));
        setStyleSheet("QMainWindow,QWidget#page{background:#f2f6f8;color:#233e50;}"
            "QLabel#title{font-size:22px;font-weight:600;color:#145e72;}"
            "QLabel#paneTitle{background:#e4eef2;color:#125a70;padding:10px;font-weight:600;}"
            "QPushButton{padding:7px 10px;} QGroupBox{border:1px solid #d5e2e9;}"
            "QToolBar{background:white;border-bottom:1px solid #d5e2e9;spacing:8px;padding:7px;}"
            "QLabel#notice{background:#fff5db;color:#705528;padding:9px;}"
            "QLabel#metrics{background:white;border:1px solid #d5e2e9;padding:12px;}"
            "QProgressBar{max-height:16px;}");
        const auto makeLabel = [](const QString& text, const char* name = nullptr) {
            auto* label = new QLabel(text); label->setWordWrap(true);
            if (name) label->setObjectName(name);
            return label;
        };
        auto* page = new QWidget; page->setObjectName("page");
        auto* outer = new QHBoxLayout(page); outer->setContentsMargins(12, 12, 12, 12); outer->setSpacing(12);
        auto* sidebar = new QWidget; sidebar->setFixedWidth(285);
        auto* controls = new QVBoxLayout(sidebar); controls->setContentsMargins(0, 0, 0, 0); controls->setSpacing(12);
        controls->addWidget(makeLabel("Ground beneath\nbuildings", "title"));
        controls->addWidget(makeLabel("Zürich · 250 × 250 m\n0.5 m height pixels · EPSG:2056"));
        controls->addWidget(makeLabel("1  Inspect the original roofs and trees.\n2  Choose a prepared terrain estimate.\n3  Move the slider to reveal the ground."));
        method_ = new QComboBox; method_->addItems({ "Classical · morphological / TIN", "AI · DSM-only pix_grad" });
        controls->addWidget(makeLabel("Terrain estimate")); controls->addWidget(method_);
        percentLabel_ = makeLabel("100% · prepared DTM endpoint"); controls->addWidget(percentLabel_);
        slider_ = new QSlider(Qt::Horizontal); slider_->setObjectName("surfaceSlider"); slider_->setRange(0, 100); slider_->setValue(100);
        slider_->setToolTip("0% = original DSM; 100% = prepared DTM. Release to rebuild the surface.");
        controls->addWidget(slider_);
        auto* endpoints = new QHBoxLayout;
        auto* original = new QPushButton("Original · 0%"); auto* terrain = new QPushButton("Terrain · 100%");
        endpoints->addWidget(original); endpoints->addWidget(terrain); controls->addLayout(endpoints);
        endpointButtons_ = { original,terrain };
        controls->addWidget(makeLabel("Intermediate positions interpolate heights for display. They are not new terrain estimates."));
        auto* form = new QFormLayout;
        exaggeration_ = new QDoubleSpinBox; exaggeration_->setRange(.25, 4); exaggeration_->setSingleStep(.25); exaggeration_->setValue(1); exaggeration_->setSuffix(" ×");
        form->addRow("Vertical exaggeration", exaggeration_); controls->addLayout(form);
        metrics_ = makeLabel("Preparing the sample...", "metrics"); metrics_->setTextInteractionFlags(Qt::TextSelectableByMouse); controls->addWidget(metrics_);
        controls->addWidget(makeLabel("Prepared results · no inference runs here. Both methods start from the original DSM independently.", "notice"));
        controls->addStretch();
        retry_ = new QPushButton("Retry / reload sample"); cancelButton_ = new QPushButton("Cancel loading");
        controls->addWidget(retry_); controls->addWidget(cancelButton_);
        controls->addWidget(makeLabel("Source: Federal Office of Topography swisstopo\nAI: David Perez · experimental pix_grad"));
        auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        scroll->setFixedWidth(300); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setWidget(sidebar);
        outer->addWidget(scroll);
        auto* workspace = new QVBoxLayout; workspace->setSpacing(8);
        workspace->addWidget(makeLabel("One observed surface. Two independent ways to estimate terrain."));
        auto* views = new QHBoxLayout; views->setSpacing(8);
        for (int i = 0; i < 2; ++i) {
            auto* column = new QVBoxLayout; column->setSpacing(0);
            headings_[i] = makeLabel(i == 0 ? "ORIGINAL DSM · roofs + trees + ground" : "CLASSICAL DTM · prepared terrain estimate", "paneTitle");
            column->addWidget(headings_[i]);
            windows_[i] = createViewerWindow(&instance);
            auto* container = QWidget::createWindowContainer(windows_[i]);
            container->setMinimumSize(300, 320); container->setFocusPolicy(Qt::StrongFocus);
            column->addWidget(container, 1); views->addLayout(column, 1);
            viewers_[i] = std::make_unique<ViewerController>(windows_[i]);
        }
        workspace->addLayout(views, 1);
        chart_ = new ProfileChart; workspace->addWidget(chart_);
        workspace->addWidget(makeLabel("Profile: gray DSM · teal classical · purple AI · dashed orange reference. The line is fixed across the same buildings in both views."));
        workspace->addWidget(makeLabel("Local 3D preview of source LN02 heights; no geoid conversion. DSM/reference source years: 2018/2019. Fixed development crop; scores are not a held-out accuracy benchmark."));
        outer->addLayout(workspace, 1); setCentralWidget(page);
        auto* toolbar = addToolBar("Navigation"); toolbar->setMovable(false);
        auto* reset = toolbar->addAction(QIcon(":/icons/full-extent.svg"), "Reset view");
        connect(reset, &QAction::triggered, this, [&] {resetView(); });
        connect(toolbar->addAction("Top view"), &QAction::triggered, this, [&] {if (ready_) { auto c = viewers_[0]->camera(); c.pitch = 85; c.yaw = 0; viewers_[0]->setCamera(c); syncFrom(0); }});
        connect(toolbar->addAction(QIcon(":/icons/zoom-in.svg"), "Zoom in"), &QAction::triggered, this, [&] {zoom(.8f); });
        connect(toolbar->addAction(QIcon(":/icons/zoom-out.svg"), "Zoom out"), &QAction::triggered, this, [&] {zoom(1.25f); });
        auto* profileAction = toolbar->addAction("Show profile"); profileAction->setCheckable(true); profileAction->setChecked(true);
        connect(profileAction, &QAction::toggled, this, [&](bool visible) {
            chart_->setVisible(visible);
            if (!ready_)return;
            if (visible)drawProfileLines(); else for (auto& viewer : viewers_)viewer->clearAnalysisLines();
        });
        connect(toolbar->addAction("Data folder"), &QAction::triggered, this, [&] {QDesktopServices::openUrl(QUrl::fromLocalFile(dataRoot())); });
        connect(toolbar->addAction("Controls"), &QAction::triggered, this, [&] {QMessageBox::information(this, "Camera controls", "Left drag: pan\nWheel or right drag: zoom\nMiddle drag or Ctrl + left drag: orbit\nBoth cameras stay linked.\n\nMove the surface slider and release to update geometry. The original view stays unchanged."); });
        progressBar_ = new QProgressBar; progressBar_->setFixedWidth(200); progressBar_->setRange(0, 100); statusBar()->addPermanentWidget(progressBar_); progressBar_->hide();
        connect(retry_, &QPushButton::clicked, this, [&] {start(); });
        connect(cancelButton_, &QPushButton::clicked, this, [&] {cancelled_ = true; cancelButton_->setEnabled(false); statusBar()->showMessage("Cancelling... Previous complete views are retained."); });
        debounce_.setSingleShot(true); debounce_.setInterval(200);
        connect(&debounce_, &QTimer::timeout, this, [&] {if (!slider_->isSliderDown())updateSurface(); });
        connect(slider_, &QSlider::valueChanged, this, [&] {updatePercentLabel(); debounce_.start(); });
        connect(slider_, &QSlider::sliderReleased, this, [&] {debounce_.stop(); updateSurface(); });
        connect(original, &QPushButton::clicked, this, [&] {slider_->setValue(0); });
        connect(terrain, &QPushButton::clicked, this, [&] {slider_->setValue(100); });
        connect(method_, qOverload<int>(&QComboBox::currentIndexChanged), this, [&] {debounce_.stop(); updateSurface(); });
        connect(exaggeration_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [&](double value) {
            for (auto& viewer : viewers_) {
                auto camera = viewer->camera();
                const auto ref = viewer->reference();
                if (ready_ && ref) {
                    const auto enu = ref->fromRender({ camera.x,camera.y,camera.z }, viewer->heightScale());
                    const auto target = ref->toRender(enu, value);
                    camera.x = float(target.x); camera.y = float(target.y); camera.z = float(target.z);
                }
                viewer->setHeightScale(float(value));
                if (ready_)viewer->setCamera(camera);
            }
            if (ready_)syncFrom(0);
        });
        poll_.setInterval(50); connect(&poll_, &QTimer::timeout, this, [&] {pollWork(); });
        cameraPoll_.setInterval(30); connect(&cameraPoll_, &QTimer::timeout, this, [&] {
            if (!ready_)return;
            for (int i = 0; i < 2; ++i) if (!sameCamera(lastCamera_[i], viewers_[i]->camera())) { syncFrom(i); break; }
        }); cameraPoll_.start();
        setBusy(false);
        QTimer::singleShot(0, this, [&] {start(); });
    }

    ~DemoWindow() override
    {
        cancelled_ = true;
        poll_.stop();
        cameraPoll_.stop();
        debounce_.stop();

        if (pending_.valid()) pending_.wait();

        for (auto& viewer : viewers_)
            viewer.reset();
    }

protected:
    void closeEvent(QCloseEvent* event) override
    {
        if (busy_) { closeQueued_ = true; cancelled_ = true; event->ignore(); return; }
        QMainWindow::closeEvent(event);
    }

private:
    QString dataRoot() const
    {
        return arguments_.isSet("data-dir") ? QFileInfo(arguments_.value("data-dir")).absoluteFilePath()
            : QDir(QCoreApplication::applicationDirPath()).filePath("data");
    }

    void setBusy(bool busy)
    {
        busy_ = busy; retry_->setEnabled(!busy); cancelButton_->setEnabled(busy);
        method_->setEnabled(!busy && ready_); slider_->setEnabled(!busy && ready_);
        for (auto* button : endpointButtons_)button->setEnabled(!busy && ready_);
        progressBar_->setVisible(busy);
    }

    void failure(const QString& error)
    {
        setBusy(false); statusBar()->showMessage(error);
        if (closeQueued_) { QTimer::singleShot(0, this, &QWidget::close); return; }
        if (!cancelled_)QMessageBox::warning(this, "Sample unavailable", error);
    }

    void start()
    {
        if (busy_)
            return;

        cancelled_ = false;
        setBusy(true);
        progress_ = 0;

        try {
            const QUrl url(arguments_.isSet("sample-url") ? arguments_.value("sample-url") : assets_.value("url").toString());
            const QString folder = prepareSample(assets_, dataRoot(), arguments_.value("sample-archive"), url, cancelled_, [&](int percent, const QString& message) {progressBar_->setValue(percent); statusBar()->showMessage(message); });
            checkpoint(cancelled_);
            configureSampleRuntime(folder);
            const int method = method_->currentIndex() + 1, percent = slider_->value();
            const QString temporary = temporary_.filePath(QString("surface-%1.tif").arg(++serial_));
            statusBar()->showMessage("Reading numeric heights and preparing both native 3D scenes...");
            pending_ = std::async(std::launch::async, [this, folder, method, percent, temporary] {
                Work result; result.method = method; result.percent = percent;
                try {
                    result.grid = readGrids(folder, cancelled_);
                    result.original = prepareScene(*result.grid, QDir(folder).filePath("dsm.tif"), cancelled_, progress_);
                    checkpoint(cancelled_);
                    const auto path = surfaceFile(*result.grid, method, percent, temporary);
                    result.result = prepareScene(*result.grid, path, cancelled_, progress_);
                }
                catch (const std::exception& e) { result.error = QString::fromUtf8(e.what()); }
                return result;
            });
            poll_.start();
        }
        catch (const std::exception& e) { 
            failure(QString::fromUtf8(e.what()));
        }
    }

    void updateSurface()
    {
        if (!ready_ || busy_)return;
        const int method = method_->currentIndex() + 1, percent = slider_->value();
        if (method == committedMethod_ && percent == committedPercent_)return;
        cancelled_ = false; progress_ = 0; setBusy(true);
        const auto grid = grid_;
        const QString temporary = temporary_.filePath(QString("surface-%1.tif").arg(++serial_));
        statusBar()->showMessage("Preparing the interpolated display surface...");
        pending_ = std::async(std::launch::async, [this, grid, method, percent, temporary] {
            Work result; result.grid = grid; result.method = method; result.percent = percent;
            try {
                checkpoint(cancelled_);
                const auto path = surfaceFile(*grid, method, percent, temporary);
                checkpoint(cancelled_);
                result.result = prepareScene(*grid, path, cancelled_, progress_);
            }
            catch (const std::exception& e) { result.error = QString::fromUtf8(e.what()); }
            return result;
        }); poll_.start();
    }

    void restoreSelection()
    {
        const QSignalBlocker a(method_), b(slider_);
        method_->setCurrentIndex(committedMethod_ - 1); slider_->setValue(committedPercent_); updatePercentLabel();
    }

    void pollWork()
    {
        progressBar_->setValue(progress_.load());
        if (!pending_.valid() || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)return;
        poll_.stop();
        auto work = pending_.get();

        if (cancelled_ || !work.error.isEmpty()) {
            restoreSelection();
            failure(cancelled_ ? "Loading cancelled. Previous complete views retained." : work.error);
            return;
        }
        QString error;
        try {
            if (work.original)
                require(viewers_[0]->setScene(std::move(work.original), error), error);
            require(viewers_[1]->setScene(std::move(work.result), error), error);
            const bool first = !ready_; grid_ = work.grid; ready_ = true;
            committedMethod_ = work.method; committedPercent_ = work.percent;
            for (auto& viewer : viewers_)viewer->setHeightScale(float(exaggeration_->value()));
            chart_->grid = grid_; chart_->selected = committedMethod_; chart_->update();
            if (first)resetView(); else syncFrom(0);
            updatePresentation(); setBusy(false);
            statusBar()->showMessage("Ready · Original DSM stays visible | Drag: pan · Wheel: zoom · Middle/Ctrl+drag: orbit");
            if (closeQueued_) {
                QTimer::singleShot(0, this, &QWidget::close);
                return; 
            }
        }
        catch (const std::exception& e) { failure(QString::fromUtf8(e.what())); }
    }

    void resetView()
    {
        if (!ready_)return;
        viewers_[0]->resetCamera();
        auto camera = viewers_[0]->camera();
        camera.yaw = 20;
        camera.pitch = 48;
        camera.distance = 3.25f;

        double mean = 0; for (float value : grid_->z[0])mean += value; mean /= grid_->z[0].size();
        HorizontalTransform horizontal(grid_->projection);
        const auto geo = horizontal.toWgs84(grid_->transform[0] + grid_->width * .25, grid_->transform[3] - grid_->height * .25, mean);
        const auto ref = viewers_[0]->reference();
        const auto target = ref->toRender(ref->toEnu(geo), viewers_[0]->heightScale());
        camera.x = float(target.x);
        camera.y = float(target.y);
        camera.z = float(target.z);
        viewers_[0]->setCamera(camera);
        syncFrom(0);
    }

    void zoom(float factor)
    {
        if (!ready_)return;
        auto c = viewers_[0]->camera(); c.distance = std::clamp(c.distance * factor, .05f, 20.f); viewers_[0]->setCamera(c); syncFrom(0);
    }

    void syncFrom(int source)
    {
        const int target = 1 - source;
        viewers_[target]->setCamera(mappedCamera(*viewers_[source], *viewers_[target]));
        for (int i = 0; i < 2; ++i)lastCamera_[i] = viewers_[i]->camera();
    }

    void updatePercentLabel()
    {
        percentLabel_->setText(slider_->value() == 100 ? "100% · prepared DTM endpoint" :
            slider_->value() == 0 ? "0% · original DSM endpoint" : QString("%1% · display interpolation").arg(slider_->value()));
    }

    void updatePresentation()
    {
        const QString method = committedMethod_ == 1 ? "CLASSICAL" : "AI · pix_grad";
        headings_[1]->setText(committedPercent_ == 100 ? method + " DTM · prepared terrain estimate" :
            committedPercent_ == 0 ? "ORIGINAL DSM · slider at 0%" : method + QString(" · %1% removal transition (display only)").arg(committedPercent_));
        const auto metrics = grid_->manifest.value("metrics").toObject();
        const auto classical = metrics.value("classical").toObject(), ai = metrics.value("ai").toObject();
        metrics_->setText(QString("REFERENCE CHECK · this crop only\n\nClassical: RMSE %1 m · MAE %2 m\nAI: RMSE %3 m · MAE %4 m\n\n250,000 shared reference pixels\nLower error is better.\n\nScores describe the final DTM endpoints, not the transition slider.")
            .arg(classical.value("rmse_m").toDouble(), 0, 'f', 2).arg(classical.value("mae_m").toDouble(), 0, 'f', 2)
            .arg(ai.value("rmse_m").toDouble(), 0, 'f', 2).arg(ai.value("mae_m").toDouble(), 0, 'f', 2));
        updatePercentLabel();
        drawProfileLines();
    }

    void drawProfileLines()
    {
        if (chart_->isHidden())
            return;

        HorizontalTransform horizontal(grid_->projection);

        for (int pane = 0; pane < 2; ++pane) {
            const auto ref = viewers_[pane]->reference();
            const auto values = pane == 0 ? grid_->z[0] : blended(*grid_, committedMethod_, committedPercent_);
            AnalysisLine line;
            for (int x : {0, grid_->width - 1}) {
                const double east = grid_->transform[0] + (x + .5) * .5;
                const double north = grid_->transform[3] - (grid_->profileRow + .5) * .5;
                const auto geo = horizontal.toWgs84(east, north, values[grid_->profileRow * grid_->width + x] + .3);
                line.points.push_back(ref->toEnu(geo));
            }
            require(viewers_[pane]->setAnalysisLines(ref, { line }), "Could not draw the shared profile line.");
        }
    }

    QJsonObject assets_;
    const QCommandLineParser& arguments_;
    std::array<QWindow*, 2> windows_{};
    std::array<std::unique_ptr<ViewerController>, 2> viewers_;
    std::array<QLabel*, 2> headings_{};
    std::array<QPushButton*, 2> endpointButtons_{};
    std::array<CameraState, 2> lastCamera_;
    std::shared_ptr<Grids> grid_;
    ProfileChart* chart_ = nullptr;
    QComboBox* method_ = nullptr;
    QSlider* slider_ = nullptr;
    QDoubleSpinBox* exaggeration_ = nullptr;
    QLabel* metrics_ = nullptr, * percentLabel_ = nullptr;
    QPushButton* retry_ = nullptr, * cancelButton_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QTimer poll_, cameraPoll_, debounce_;
    QTemporaryDir temporary_;
    std::future<Work> pending_;
    std::atomic_bool cancelled_{ false };
    std::atomic_int progress_{ 0 };
    bool ready_ = false, busy_ = false, closeQueued_ = false;
    int committedMethod_ = 1, committedPercent_ = 100, serial_ = 0;
};

int main(int argc,char** argv)
{
    QApplication app(argc,argv);
    app.setApplicationName("DsmToDtm");app.setApplicationVersion("1.0");
    auto palette=app.palette();
    palette.setColor(QPalette::Window,QColor("#f2f6f8"));palette.setColor(QPalette::Base,Qt::white);
    palette.setColor(QPalette::WindowText,QColor("#233e50"));palette.setColor(QPalette::Text,QColor("#233e50"));
    palette.setColor(QPalette::Button,Qt::white);palette.setColor(QPalette::ButtonText,QColor("#233e50"));
    palette.setColor(QPalette::Highlight,QColor("#16859b"));palette.setColor(QPalette::HighlightedText,Qt::white);
    app.setPalette(palette);
    const auto gdalData=QDir(QCoreApplication::applicationDirPath()).filePath("gdal-data");
    if(QDir(gdalData).exists())qputenv("GDAL_DATA",QDir::toNativeSeparators(gdalData).toUtf8());
    QCommandLineParser parser;parser.setApplicationDescription("Ground beneath buildings — native GeoKernel 3D prepared DSM/DTM comparison.");
    parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"sample-archive","Use the exact pinned local sample ZIP before GitHub upload.","zip"});
    parser.addOption({"sample-url","Override the download mirror; the pinned ZIP SHA-256 still applies.","url"});
    parser.addOption({"data-dir","Override the sample cache directory.","directory"});
    parser.process(app);

    QVulkanInstance instance;instance.setApiVersion(QVersionNumber(1,1,0));
    if(!instance.create()) {
        QMessageBox::critical(nullptr,"DsmToDtm","Vulkan could not be initialized.");
        return 1;
    }
    try {
        int result;
        {
            DemoWindow window(instance,parser);
            window.show();
            result=app.exec();
        }
        return result;
    }catch(const std::exception& e) {
        QMessageBox::critical(nullptr,"DsmToDtm",QString::fromUtf8(e.what()));
        return 1;
    }
}
