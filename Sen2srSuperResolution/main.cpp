#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFormLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <atomic>
#include "DemoAssets.h"
#include "SampleSupport.h"
#include "SrRasterIO.h"
#include "ComparisonView.h"
#include "Raster/Tiff/GisLayerTIFF.h"

using namespace GeoKernel::AI;
using namespace GeoKernel::Viewer;
using namespace GeoKernel::Formats::Raster::Tiff;

struct Assets
{
    QString model, input, rgb;
};

Assets prepareAssets(QWidget *parent = nullptr)
{
    Assets a;
    const QString manifest = ensureSampleFile(QUrl(ModelUrl), ModelArchive, "sen2sr-model",
                                              "geokernel-model.json", parent, ModelSha);
    if (manifest.isEmpty())
        return {};
    a.model = QFileInfo(manifest).absolutePath();
    const QString folder = QDir(sampleDataPath()).filePath("sentinel-sample");
    QDir().mkpath(folder);
    a.input = QDir(folder).filePath("inference-img.tif");
    auto verified = [&]
    {
        QFile f(a.input);
        QCryptographicHash h(QCryptographicHash::Sha256);
        return f.open(QIODevice::ReadOnly) && h.addData(&f) && h.result().toHex() == DatasetSha;
    };

    if (!verified() && (!downloadFile(QUrl(DatasetUrl), a.input, parent) || !verified()))
    {
        sampleError(parent, "Data download", "Source missing or SHA-256 differs.");
        return {};
    }

    a.rgb = QDir(folder).filePath("original-rgb.tif");
    auto sample = readSrSample(a.input);
    writeSrDisplay(sample.raster, sample, a.rgb, false);
    return a;
}

bool ready(const Assets &a)
{
    return QFileInfo::exists(a.input) && QFileInfo::exists(a.rgb) &&
           QFileInfo::exists(QDir(a.model).filePath("geokernel-model.json"));
}

struct Result
{
    QString error, preview;
    AiRasterInferenceResult raster;
    std::shared_ptr<QTemporaryDir> temporary;
};

std::shared_ptr<Result> runSr(const Assets &a, AiExecutionProvider provider,
                              const std::shared_ptr<std::atomic_bool> &cancel,
                              AiRasterInferenceProgressCallback progress = {})
{
    auto result = std::make_shared<Result>();
    try
    {
        if (cancel->load())
        {
            result->raster.cancelled = true;
            return result;
        }
        const auto sample = readSrSample(a.input);
        AiSessionOptions options;
        options.provider = provider;
        options.intraOpThreads = 4;
        options.interOpThreads = 1;
        result->raster = sen2srUpscale(
            a.model, sample.raster, options, [cancel] { return cancel->load(); },
            [&](const AiRasterInferenceProgress &p)
            {
                if (progress)
                {
                    auto v = p;
                    v.percent = v.percent * 90 / 100;
                    progress(v);
                }
            });
        if (result->raster.cancelled || cancel->load())
        {
            result->raster.cancelled = true;
            result->raster.data.clear();
            return result;
        }
        if (progress)
            progress({result->raster.processedTiles, result->raster.processedTiles, 95,
                      "Preparing comparison display..."});
        result->temporary = std::make_shared<QTemporaryDir>();
        if (!result->temporary->isValid())
            throw std::runtime_error("Cannot create temporary display.");
        result->preview = result->temporary->filePath("enhanced-rgb.tif");
        writeSrDisplay(result->raster, sample, result->preview, true);
    }
    catch (const std::exception &e)
    {
        if (cancel->load())
        {
            result->raster.cancelled = true;
            result->raster.data.clear();
        }
        else
            result->error = QString::fromUtf8(e.what());
    }
    return result;
}

void addRaster(GisViewer *viewer, const QString &path, const QString &name)
{
    auto layer = std::make_unique<GisLayerTIFF>(path);
    layer->setName(name);
    layer->open();
    viewer->addLayer(layer);
}

QString diagnostics(const Result &result)
{
    const auto &r = result.raster;
    return QString(
               "SEN2SRLite RGBN x4\n\nProvider: %1\nOriginal:578 x379 /10m\nAI output:%2 x%3 /2.5m "
               "grid\nBands:RGB+NIR\nTiles:%4\nElapsed:%5ms\n\nOriginal Fourier radiometric correction is "
               "included.\n\nDisplay uses the same source-based stretch on both sides. Save GeoTIFF retains "
               "Float32 RGB+NIR, source CRS and AI provenance.\n\nAI output estimates detail;2.5m grid does "
               "not mean2.5m sensor measurements. Results stay in this session until exported.")
        .arg(aiExecutionProviderName(r.provider))
        .arg(r.width)
        .arg(r.height)
        .arg(r.processedTiles)
        .arg(r.elapsedMilliseconds);
}

class DemoWindow : public QMainWindow
{
  public:
    bool busy = false;

  protected:
    void closeEvent(QCloseEvent *e) override
    {
        if (busy)
        {
            statusBar()->showMessage("Wait for the operation to finish.");
            e->ignore();
        }
        else
            QMainWindow::closeEvent(e);
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    DemoWindow window;
    window.resize(1350, 850);
    window.setWindowTitle("Sen2srSuperResolution");
    window.setWindowIcon(QIcon(":/icons/geokernel.ico"));
    auto *status = window.statusBar();
    auto *comparison = new ComparisonView(&window);
    window.setCentralWidget(comparison);
    auto *viewer = comparison->original;
    viewer->setActiveTool(GisViewerTool::Pan);
    auto *panel = new QWidget(&window);
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel("<b>SEN2SRLite - Sentinel-2 super-resolution</b>", panel));
    auto *label = new QLabel("Compare original10m RGB with an AI-generated2.5m output grid.", panel);
    label->setWordWrap(true);
    layout->addWidget(label);
    auto *form = new QFormLayout;
    auto *provider = new QComboBox(panel);
    provider->addItem("Auto", int(AiExecutionProvider::Auto));
    provider->addItem("CPU", int(AiExecutionProvider::CPU));
    provider->addItem("CUDA", int(AiExecutionProvider::CUDA));
    provider->addItem("DirectML", int(AiExecutionProvider::DirectML));
    form->addRow("Execution provider", provider);
    layout->addLayout(form);
    auto *run = new QPushButton("Run super-resolution", panel);
    auto *save = new QPushButton("Save GeoTIFF", panel);
    auto *cancel = new QPushButton("Cancel inference", panel);
    save->setEnabled(false);
    cancel->setEnabled(false);
    layout->addWidget(run);
    layout->addWidget(save);
    layout->addWidget(cancel);
    auto *slider = new QSlider(Qt::Horizontal, panel);
    slider->setRange(0, 100);
    slider->setValue(50);
    slider->setEnabled(false);
    layout->addWidget(new QLabel("Before / after - slide right to reveal AI output", panel));
    layout->addWidget(slider);
    QObject::connect(slider, &QSlider::valueChanged, comparison, &ComparisonView::setComparison);
    auto *progress = new QProgressBar(panel);
    progress->setRange(0, 100);
    layout->addWidget(progress);
    auto *details = new QTextEdit(panel);
    details->setReadOnly(true);
    layout->addWidget(details, 1);
    auto *credit = new QLabel("Model:SEN2SR / ESAOpenSR contributors.<br>CC0 + inherited SPAN Apache-2.0 "
                              "notices.<br>Imagery:FTW upstream Sentinel-2 example.",
                              panel);
    credit->setWordWrap(true);
    layout->addWidget(credit);
    auto *dock = new QDockWidget("GeoKernel AI", &window);
    dock->setMinimumWidth(370);
    dock->setWidget(panel);
    window.addDockWidget(Qt::RightDockWidgetArea, dock);
    auto *toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);
    QObject::connect(toolbar->addAction(QIcon(":/icons/zoom-in.svg"), "Zoom In"), &QAction::triggered, viewer,
                     &GisViewer::zoomIn);
    QObject::connect(toolbar->addAction(QIcon(":/icons/zoom-out.svg"), "Zoom Out"), &QAction::triggered,
                     viewer, &GisViewer::zoomOut);
    QObject::connect(toolbar->addAction(QIcon(":/icons/full-extent.svg"), "Full Extent"), &QAction::triggered,
                     viewer, &GisViewer::fullExtent);
    QActionGroup tools(&window);
    tools.setExclusive(true);
    auto *zoom = toolbar->addAction(QIcon(":/icons/zoom-box.svg"), "Zoom Rect");
    auto *pan = toolbar->addAction(QIcon(":/icons/pan.svg"), "Pan");
    for (auto *action : {zoom, pan})
    {
        action->setCheckable(true);
        tools.addAction(action);
    }
    pan->setChecked(true);
    QObject::connect(zoom, &QAction::triggered, viewer,
                     [viewer] { viewer->setActiveTool(GisViewerTool::ZoomBox); });
    QObject::connect(pan, &QAction::triggered, viewer,
                     [viewer] { viewer->setActiveTool(GisViewerTool::Pan); });
    Assets assets;
    std::shared_ptr<Result> last;
    auto cancellation = std::make_shared<std::atomic_bool>(false);
    auto prepare = [&]
    {
        if (window.busy)
            return;
        window.busy = true;
        run->setEnabled(false);
        details->setPlainText("Preparing model and Sentinel-2 sample...");
        try
        {
            assets = prepareAssets(&window);
            if (!ready(assets))
                throw std::runtime_error("Preparation failed. Click Run to retry.");
            comparison->setResultReady(false);
            comparison->enhanced->clearLayers();
            viewer->clearLayers();
            last.reset();
            save->setEnabled(false);
            slider->setEnabled(false);
            addRaster(viewer, assets.rgb, "Original10m");
            viewer->fullExtent();
            details->setPlainText("Sentinel-2 L2A RGB+NIR\n578 x379,10m grid.\n\nClick Run to create the4x "
                                  "AI result.\nNo predictions are loaded at startup.");
            progress->setValue(0);
        }
        catch (const std::exception &e)
        {
            details->setPlainText(QString::fromUtf8(e.what()));
        }
        window.busy = false;
        run->setEnabled(true);
    };
    QObject::connect(cancel, &QPushButton::clicked, &window, [cancellation] { cancellation->store(true); });
    QObject::connect(
        run, &QPushButton::clicked, &window,
        [&]
        {
            if (window.busy)
                return;
            if (!ready(assets))
                prepare();
            if (!ready(assets))
                return;
            window.busy = true;
            run->setEnabled(false);
            save->setEnabled(false);
            provider->setEnabled(false);
            cancel->setEnabled(true);
            cancellation->store(false);
            progress->setValue(0);
            progress->setFormat("Starting...");
            auto result = std::make_shared<std::shared_ptr<Result>>();
            auto input = assets;
            auto selected = static_cast<AiExecutionProvider>(provider->currentData().toInt());
            auto *worker = QThread::create(
                [result, input, selected, cancellation, progress]
                {
                    *result = runSr(input, selected, cancellation,
                                    [progress](const AiRasterInferenceProgress &p)
                                    {
                                        QMetaObject::invokeMethod(
                                            progress,
                                            [progress, p]
                                            {
                                                progress->setValue(p.percent);
                                                progress->setFormat(
                                                    QString("%1% - %2").arg(p.percent).arg(p.message));
                                            },
                                            Qt::QueuedConnection);
                                    });
                });
            QObject::connect(worker, &QThread::finished, &window,
                             [&, result, worker]
                             {
                                 worker->deleteLater();
                                 window.busy = false;
                                 run->setEnabled(true);
                                 provider->setEnabled(true);
                                 cancel->setEnabled(false);
                                 if (!*result)
                                     return;
                                 auto completed = *result;
                                 if (completed->raster.cancelled)
                                 {
                                     details->setPlainText("Inference cancelled.");
                                     progress->setValue(0);
                                     save->setEnabled(bool(last));
                                     return;
                                 }
                                 if (!completed->error.isEmpty())
                                 {
                                     details->setPlainText(completed->error);
                                     save->setEnabled(bool(last));
                                     QMessageBox::critical(&window, "Super-resolution", completed->error);
                                     return;
                                 }
                                 try
                                 {
                                     comparison->setResultReady(false);
                                     comparison->enhanced->clearLayers();
                                     last.reset();
                                     addRaster(comparison->enhanced, completed->preview, "AI2.5m grid");
                                     last = completed;
                                     comparison->setResultReady(true);
                                     comparison->setComparison(slider->value());
                                     slider->setEnabled(true);
                                     save->setEnabled(true);
                                     details->setPlainText(diagnostics(*completed));
                                     progress->setValue(100);
                                     progress->setFormat("Complete");
                                 }
                                 catch (const std::exception &e)
                                 {
                                     details->setPlainText(QString::fromUtf8(e.what()));
                                 }
                             });
            worker->start();
        });
    QObject::connect(save, &QPushButton::clicked, &window,
                     [&]
                     {
                         if (window.busy || !last)
                             return;
                         QString path = QFileDialog::getSaveFileName(&window, "Save RGB+NIR super-resolution",
                                                                     "sentinel-super-resolution.tif",
                                                                     "GeoTIFF (*.tif *.tiff)");
                         if (path.isEmpty())
                             return;
                         if (!path.endsWith(".tif", Qt::CaseInsensitive) &&
                             !path.endsWith(".tiff", Qt::CaseInsensitive))
                             path += ".tif";
                         try
                         {
                             sen2srWriteGeoTiff(last->raster, path);
                             window.statusBar()->showMessage("Saved: " + QDir::toNativeSeparators(path));
                         }
                         catch (const std::exception &e)
                         {
                             QMessageBox::critical(&window, "Save GeoTIFF", QString::fromUtf8(e.what()));
                         }
                     });
    QObject::connect(viewer, &GisViewer::drawingProgressChanged, &window,
                     [&](int value, const QString &text)
                     {
                         if (!window.busy)
                         {
                             QString message = text;
                             message.replace("%p", QString::number(std::clamp(value, 0, 100)));
                             status->showMessage(message);
                         }
                     });
    window.show();
    QTimer::singleShot(0, &window, prepare);
    const int exit = app.exec();
    comparison->setResultReady(false);
    comparison->enhanced->clearLayers();
    viewer->clearLayers();
    last.reset();
    return exit;
}
