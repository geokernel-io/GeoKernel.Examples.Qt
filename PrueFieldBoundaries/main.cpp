#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFormLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QSlider>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <atomic>
#include <cmath>
#include <memory>
#include "DemoAssets.h"
#include "LandCoverOutput.h"
#include "FieldRasterIO.h"
#include "SampleSupport.h"
#include "Raster/Tiff/GisLayerTIFF.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::AI;
using namespace GeoKernel::Viewer;
using namespace GeoKernel::Formats::Raster::Tiff;
using namespace GeoKernel::Formats::Common::Gdal;

struct Assets
{
    QString model, input, rgb;
};

Assets prepareAssets(QWidget *parent = nullptr)
{
    Assets a;
    const QString manifest = ensureSampleFile(QUrl(ModelUrl), ModelArchive, "prue-model-v2",
                                              "geokernel-model.json", parent, ModelSha);
    if (manifest.isEmpty())
        return {};
    a.model = QFileInfo(manifest).absolutePath();
    const QString folder = QDir(sampleDataPath()).filePath("prue-fields");
    QDir().mkpath(folder);
    a.input = QDir(folder).filePath("inference-img.tif");
    auto validFile = [&]
    {
        QFile file(a.input);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        return file.open(QIODevice::ReadOnly) && hash.addData(&file) && hash.result().toHex() == DatasetSha;
    };
    if (!validFile())
    {
        if (!downloadFile(QUrl(DatasetUrl), a.input, parent) || !validFile())
        {
            sampleError(parent, "Data download",
                        "The sample could not be downloaded or its SHA-256 differs.");
            return {};
        }
    }
    a.rgb = QDir(folder).filePath("fields-rgb.tif");
    FieldRasterIO io;
    io.rgb(io.read(a.input), a.rgb);
    return a;
}

bool ready(const Assets &a)
{
    return QFileInfo::exists(QDir(a.model).filePath("geokernel-model.json")) && QFileInfo::exists(a.input) &&
           QFileInfo::exists(a.rgb);
}

struct Result
{
    QString error, preview;
    QByteArray geoJson;
    AiRasterInferenceResult classes;
    QVector<qint64> histogram = QVector<qint64>(3, 0);
    qint64 polygons = 0;
    std::shared_ptr<QTemporaryDir> workspace;
};

std::shared_ptr<Result> classify(const Assets &assets, AiExecutionProvider provider,
                                 const std::shared_ptr<std::atomic_bool> &cancellation,
                                 AiRasterInferenceProgressCallback progress = {})
{

    auto result = std::make_shared<Result>();

    auto stop = [&]
    {
        if (cancellation->load())
        {
            result->classes.cancelled = true;
            return true;
        }

        return false;
    };

    try
    {
        QElapsedTimer timer;
        timer.start();
        const auto package = AiModelPackage::open(assets.model);
        const auto errors = package.validationErrors();

        if (!errors.isEmpty())
            throw std::runtime_error(errors.join("\n").toStdString());

        const auto &m = package.manifest();

        if (m.id != "fieldsoftheworld-prue-efficientnet-b5-ccby" || m.inputAdapters.isEmpty() ||
            m.outputDecoders.isEmpty())
            throw std::runtime_error("Invalid PRUE CC-BY package.");

        FieldRasterIO io;
        const auto raster = io.read(assets.input);

        if (stop())
            return result;

        AiRasterInferenceRequest request;
        request.modelPath = package.modelPath();
        request.modelId = m.id;
        request.modelVersion = m.version;
        request.modelRevision = m.provenance.revision;
        request.modelSha256 = m.modelSha256;
        request.rasterPath = assets.input;
        request.inputName = m.inputs[0].name;
        request.outputName = m.outputs[0].name;
        request.bands = m.inputAdapters[0].bands;
        request.tiling = m.tiling;
        request.normalization = m.normalization;
        request.outputDecoder = m.outputDecoders[0];
        request.outputMode = AiRasterOutputMode::ClassMask;
        request.sessionOptions.provider = provider;
        request.sessionOptions.intraOpThreads = 4;
        request.sessionOptions.interOpThreads = 1;
        request.isCancellationRequested = [cancellation] { return cancellation->load(); };
        result->classes = AiRasterInferenceExecutor::run(request,
                                                         [&](const AiRasterInferenceProgress &state)
                                                         {
                                                             if (progress)
                                                             {
                                                                 auto p = state;
                                                                 p.percent = state.percent * 85 / 100;
                                                                 progress(p);
                                                             }
                                                         });
        if (result->classes.cancelled || stop())
            return result;

        auto &output = result->classes;

        if (output.width != raster.width || output.height != raster.height ||
            output.dataType != AiTensorDataType::Int32 ||
            output.data.size() != raster.width * raster.height * int(sizeof(qint32)))
            throw std::runtime_error("Invalid PRUE output.");

        auto *ids = reinterpret_cast<qint32 *>(output.data.data());

        for (int i = 0; i < raster.width * raster.height; ++i)
        {
            if (!raster.valid[i])
                ids[i] = -1;
            else if (ids[i] < 0 || ids[i] > 2)
                throw std::runtime_error("Invalid PRUE class.");
            if (ids[i] >= 0)
                ++result->histogram[ids[i]];
        }

        result->workspace = std::make_shared<QTemporaryDir>();

        if (!result->workspace->isValid())
            throw std::runtime_error("Cannot create temporary result workspace.");

        LandCoverOutput writer;
        result->preview = result->workspace->filePath("field-rgba.tif");
        writer.writePreview(output, result->preview, ClassColors);

        if (progress)
            progress({output.processedTiles, output.processedTiles, 90, "Creating field polygons..."});

        const QString path = result->workspace->filePath("fields.geojson");
        result->polygons = writer.write(output, path, [cancellation] { 
            return cancellation->load(); 
        });

        if (stop())
            return result;

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            throw std::runtime_error("Cannot read field polygons.");
        auto document = QJsonDocument::fromJson(file.readAll()).object();
        auto features = document["features"].toArray();

        for (int i = 0; i < features.size(); ++i)
        {
            auto f = features[i].toObject();
            auto props = f["properties"].toObject();

            if (props["class_id"].toInt(-1) != 1)
                throw std::runtime_error("Invalid field polygon.");

            props["class_name"] = ClassNames[1];
            f["properties"] = props;
            features[i] = f;
        }
        document["features"] = features;
        result->geoJson = QJsonDocument(document).toJson(QJsonDocument::Compact);
        output.elapsedMilliseconds = timer.elapsed();
    }
    catch (const std::exception &e)
    {
        if (!stop())
            result->error = QString::fromUtf8(e.what());
    }
    return result;
}

QString diagnostics(const Result &result)
{
    const auto &r = result.classes;
    QString text =
        QString("PRUE EfficientNet-B5 CCBY field segmentation\n\nProvider: %1\nRaster: %2 x %3, 8 "
                "bi-temporal Sentinel-2 bands\nTiles: %4\nInference: %5 ms\nField polygons: %6\n\n")
            .arg(aiExecutionProviderName(r.provider))
            .arg(r.width)
            .arg(r.height)
            .arg(r.processedTiles)
            .arg(r.elapsedMilliseconds)
            .arg(result.polygons);
    for (int i = 0; i < 3; ++i)
        text += QString("%1: %2 pixels\n").arg(ClassNames[i]).arg(result.histogram[i]);
    return text + "\nResults are session-only. Use Save Shapefile to export. Polygons are agricultural field "
                  "units, not legal/cadastral parcels. Cloud/shadow detection is not included.";
}

void addRaster(GisViewer *viewer, const QString &path, const QString &name)
{
    auto layer = std::make_unique<GisLayerTIFF>(path);
    layer->setName(name);
    layer->open();
    viewer->addLayer(layer);
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
            statusBar()->showMessage("Wait for the current operation to finish.");
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
    window.resize(1280, 820);
    window.setWindowTitle("PrueFieldBoundaries");
    window.setWindowIcon(QIcon(":/icons/geokernel.ico"));
    auto *status = window.statusBar();
    auto *viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);
    auto *panel = new QWidget(&window);
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel("<b>FTW / PRUE — field boundaries</b>", panel));
    auto *description =
        new QLabel("Extract agricultural field boundaries from a two-period Sentinel-2 sample.", panel);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto *form = new QFormLayout;
    auto *provider = new QComboBox(panel);
    provider->addItem("Auto", int(AiExecutionProvider::Auto));
    provider->addItem("CPU", int(AiExecutionProvider::CPU));
    provider->addItem("CUDA", int(AiExecutionProvider::CUDA));
    provider->addItem("DirectML", int(AiExecutionProvider::DirectML));
    form->addRow("Execution provider", provider);
    layout->addLayout(form);
    auto *transparency = new QSlider(Qt::Horizontal, panel);
    transparency->setRange(0, 100);
    transparency->setValue(59);
    transparency->setEnabled(false);
    auto *transparencyLabel = new QLabel("Output transparency: 41%", panel);
    layout->addWidget(transparencyLabel);
    layout->addWidget(transparency);
    auto applyTransparency = [=]
    {
        transparencyLabel->setText(QString("Output transparency: %1%").arg(100 - transparency->value()));
        if (auto *layer = viewer->mapLayerByName("PRUE field interiors"))
        {
            layer->setOpacity(transparency->value() / 100.0);
            viewer->invalidateRenderCache(true, true);
            viewer->update();
        }
    };
    QObject::connect(transparency, &QSlider::valueChanged, &window, [=](int) { applyTransparency(); });
    auto *run = new QPushButton("Run field boundary analysis", panel);
    auto *save = new QPushButton("Save Shapefile", panel);
    save->setEnabled(false);
    auto *cancel = new QPushButton("Cancel inference", panel);
    cancel->setEnabled(false);
    layout->addWidget(run);
    layout->addWidget(save);
    layout->addWidget(cancel);
    auto *progress = new QProgressBar(panel);
    progress->setRange(0, 100);
    layout->addWidget(progress);
    auto *details = new QTextEdit(panel);
    details->setReadOnly(true);
    layout->addWidget(new QLabel("Inference diagnostics", panel));
    layout->addWidget(details, 1);
    QString legend = "<b>Field classes</b><br>";
    for (int i = 1; i < 3; ++i)
        legend += QString("<span style='color:%1'>●</span> %2<br>").arg(ClassColors[i], ClassNames[i]);
    auto *labels = new QLabel(legend, panel);
    layout->addWidget(labels);
    auto *credits = new QLabel(
        "Model: FTW / PRUE contributors; CC-BY-4.0.<br>Imagery: FTW upstream Sentinel-2 example.", panel);
    credits->setWordWrap(true);
    layout->addWidget(credits);
    auto *dock = new QDockWidget("GeoKernel AI", &window);
    dock->setMinimumWidth(390);
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
    toolbar->addSeparator();
    QActionGroup tools(&window);
    tools.setExclusive(true);
    auto *zoom = toolbar->addAction(QIcon(":/icons/zoom-box.svg"), "Zoom Rect");
    auto *pan = toolbar->addAction(QIcon(":/icons/pan.svg"), "Pan");
    for (auto *a : {zoom, pan})
    {
        a->setCheckable(true);
        tools.addAction(a);
    }
    pan->setChecked(true);
    QObject::connect(zoom, &QAction::triggered, viewer,
                     [viewer] { viewer->setActiveTool(GisViewerTool::ZoomBox); });
    QObject::connect(pan, &QAction::triggered, viewer,
                     [viewer] { viewer->setActiveTool(GisViewerTool::Pan); });
    Assets assets;
    std::shared_ptr<Result> lastResult;
    auto cancellation = std::make_shared<std::atomic_bool>(false);
    QObject::connect(cancel, &QPushButton::clicked, &window, [cancellation] { cancellation->store(true); });
    auto prepare = [&]
    {
        if (window.busy)
            return;
        window.busy = true;
        run->setEnabled(false);
        details->setPlainText("Preparing the fixed model and Sentinel-2 sample...");
        try
        {
            assets = prepareAssets(&window);
        }
        catch (const std::exception &e)
        {
            window.busy = false;
            run->setEnabled(true);
            details->setPlainText(QString::fromUtf8(e.what()));
            return;
        }
        window.busy = false;
        run->setEnabled(true);
        if (!ready(assets))
        {
            details->setPlainText("Preparation failed. Check your connection and click Run to retry.");
            return;
        }
        try
        {
            viewer->clearLayers();
            lastResult.reset();
            transparency->setEnabled(false);
            save->setEnabled(false);
            addRaster(viewer, assets.rgb, "Fields - RGB preview");
            viewer->fullExtent();
            progress->setValue(0);
            progress->setFormat("Ready");
            details->setPlainText("FTW field sample\n578 x 379 pixels, two RGB+NIR periods.\n\nClick Run to "
                                  "classify the image. No predictions are loaded at startup.");
            status->showMessage("Sample ready.");
        }
        catch (const std::exception &e)
        {
            details->setPlainText(QString::fromUtf8(e.what()));
        }
    };
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
            progress->setFormat("Starting inference...");
            const Assets input = assets;
            const auto selected = static_cast<AiExecutionProvider>(provider->currentData().toInt());
            auto result = std::make_shared<std::shared_ptr<Result>>();
            auto *worker = QThread::create(
                [result, input, selected, cancellation, progress]
                {
                    *result =
                        classify(input, selected, cancellation,
                                 [progress](const AiRasterInferenceProgress &state)
                                 {
                                     QMetaObject::invokeMethod(
                                         progress,
                                         [progress, state]
                                         {
                                             progress->setValue(state.percent);
                                             progress->setFormat(
                                                 QString("%1% — %2").arg(state.percent).arg(state.message));
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
                                 {
                                     details->setPlainText("Inference returned no result.");
                                     return;
                                 }
                                 auto completed = *result;
                                 if (completed->classes.cancelled)
                                 {
                                     details->setPlainText("Inference cancelled.");
                                     progress->setValue(0);
                                     save->setEnabled(lastResult && !lastResult->geoJson.isEmpty());
                                     return;
                                 }
                                 if (!completed->error.isEmpty())
                                 {
                                     details->setPlainText(completed->error);
                                     save->setEnabled(lastResult && !lastResult->geoJson.isEmpty());
                                     QMessageBox::critical(&window, "PrueFieldBoundaries", completed->error);
                                     return;
                                 }
                                 try
                                 {
                                     viewer->clearLayers();
                                     lastResult.reset();
                                     addRaster(viewer, assets.rgb, "Fields - RGB preview");
                                     addRaster(viewer, completed->preview, "PRUE field interiors");
                                     lastResult = completed;
                                     transparency->setEnabled(true);
                                     applyTransparency();
                                     viewer->fullExtent();
                                     details->setPlainText(diagnostics(*completed));
                                     progress->setValue(100);
                                     progress->setFormat("Field analysis complete");
                                     save->setEnabled(completed->polygons > 0);
                                     status->showMessage(QString("Field boundaries generated in %1 ms.")
                                                             .arg(completed->classes.elapsedMilliseconds));
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
                         if (window.busy || !lastResult)
                             return;
                         QString path = QFileDialog::getSaveFileName(&window, "Save field polygons",
                                                                     "fields.shp", "ESRI Shapefile (*.shp)");
                         if (path.isEmpty())
                             return;
                         if (!path.endsWith(".shp", Qt::CaseInsensitive))
                             path += ".shp";
                         try
                         {
                             LandCoverOutput output;
                             output.saveShapefile(lastResult->geoJson, path);
                             status->showMessage("Shapefile saved: " + QDir::toNativeSeparators(path));
                         }
                         catch (const std::exception &e)
                         {
                             QMessageBox::critical(&window, "Save Shapefile", QString::fromUtf8(e.what()));
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
    viewer->clearLayers();
    lastResult.reset();
    return exit;
}
