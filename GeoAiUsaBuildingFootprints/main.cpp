#include <QAction>
#include <QFileDialog>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QCloseEvent>
#include <QTimer>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTextStream>
#include <atomic>
#include "DemoAssets.h"
#include "FootprintOutput.h"
#include "Vector/GeoJson/GisLayerGeoJSON.h"
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTextEdit>
#include <QThread>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>
#include "GeoKernelAI.h"
#include "GeoKernelAnalysis.h"
#include "Raster/Tiff/GisLayerTIFF.h"
#include "SampleSupport.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::AI;
using namespace GeoKernel::Analysis;
using namespace GeoKernel::Formats::Raster::Tiff;
using namespace GeoKernel::Viewer;

struct JobResult
{
    QString error;
    QString sourcePath;
    QString inferencePath;
    QString vectorPath;
    QByteArray footprintGeoJson;
    bool cancelled = false;
    AiModelManifest manifest;
    AiRasterInferenceResult inference;
    qint64 polygonCount = 0;
    std::unique_ptr<GisLayerTIFF> sourceLayer;
    std::unique_ptr<GeoKernel::Core::Layers::GisLayerVector> predictionLayer;
};

GisLayerStyle footprintStyle()
{
    GisLayerStyle style;
    style.setFillColor("#FF4020");
    style.setFillOpacity(160);
    style.setLineColor("#FFE040");
    style.setLineWidth(1.25F);
    return style;
}

QString existingPath(const QString &candidate)
{
    return QFileInfo::exists(candidate) ? QDir::toNativeSeparators(candidate) : QString{};
}

QString prepareDataset(const DemoDataset &item, QWidget *parent = nullptr)
{
    if (!item.localSource)
        return ensureSampleFile(QUrl(QString(DatasetRelease) + item.archive), item.archive,
                                QString("datasets/%1").arg(item.id), item.raster, parent, item.sha256);
    const QString destination =
        QDir(sampleDataPath()).filePath(QString("datasets/%1/%2").arg(item.id, item.raster));
    if (QFileInfo::exists(destination))
        return destination;
    const QString source = qEnvironmentVariable(item.localEnvironment, item.localSource);
    if (!QFileInfo::exists(source))
        return {};
    QDir().mkpath(QFileInfo(destination).absolutePath());
    if (!QFile::copy(source, destination))
    {
        sampleError(parent, "Local sample", "Could not cache the local raster.");
        return {};
    }
    return destination;
}

QString diagnostics(const JobResult &result)
{
    return QStringLiteral("GeoAI USA Mask R-CNN building footprint inference\n\n"
                          "Model: %1 %2\nProvider: %3\nRaster: %4 x %5\nTiles: %6\nElapsed: %7 ms\n\n"
                          "Building polygons: %8\nOutput: session-only building overlay")
        .arg(result.manifest.id, result.manifest.version, aiExecutionProviderName(result.inference.provider))
        .arg(result.inference.width)
        .arg(result.inference.height)
        .arg(result.inference.processedTiles)
        .arg(result.inference.elapsedMilliseconds)
        .arg(result.polygonCount);
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
            statusBar()->showMessage("Wait for inference to finish before closing.");
            e->ignore();
        }
        else
            QMainWindow::closeEvent(e);
    }
};
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    DemoWindow window;
    window.resize(1280, 820);
    window.setWindowTitle("GeoAiUsaBuildingFootprints");
    window.setWindowIcon(QIcon(":/icons/geokernel.ico"));

    auto *viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto *status = window.statusBar();
    status->showMessage(QStringLiteral("Map ready."));

    auto *panel = new QWidget(&window);
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->addWidget(new QLabel("<b>GeoAI USA Mask R-CNN building footprints</b>", panel));
    panelLayout->addWidget(new QLabel("Run a GeoKernel model package on a georeferenced RGB raster.", panel));

    auto *form = new QFormLayout;
    QString modelPath = QDir(sampleDataPath()).filePath("geoai-usa-model");
    QString rasterPath;
    const QString modelUrl = "https://pub-00d214f25f754884b22349c19ac37bbf.r2.dev/geokernel-models/"
                             "giswqs-geoai-usa-maskrcnn-resnet50-fpn-geokernel-onnx-v1.zip";
    auto *provider = new QComboBox(panel);
    provider->addItem("Auto", static_cast<int>(AiExecutionProvider::Auto));
    provider->addItem("CPU", static_cast<int>(AiExecutionProvider::CPU));
    provider->addItem("CUDA", static_cast<int>(AiExecutionProvider::CUDA));
    provider->addItem("DirectML", static_cast<int>(AiExecutionProvider::DirectML));
    form->addRow("Execution provider", provider);
    auto *threshold = new QDoubleSpinBox(panel);
    threshold->setRange(.05, .95);
    threshold->setDecimals(4);
    threshold->setSingleStep(.05);
    threshold->setValue(.50);
    threshold->setToolTip("Confidence threshold for each building detection. Mask probability threshold is fixed at 0.50.");
    form->addRow("Building confidence", threshold);
    panelLayout->addLayout(form);

    auto *run = new QPushButton("Run building segmentation inference", panel);
    panelLayout->addWidget(run);
    auto *save = new QPushButton("Save Shapefile", panel);
    save->setEnabled(false);
    panelLayout->addWidget(save);
    auto savedFootprints = std::make_shared<QByteArray>();
    QObject::connect(save, &QPushButton::clicked, &window,
                     [&window, status, savedFootprints]
                     {
                         QString path = QFileDialog::getSaveFileName(
                             &window, "Save building footprints", "buildings.shp", "ESRI Shapefile (*.shp)");
                         if (path.isEmpty())
                             return;
                         if (!path.endsWith(".shp", Qt::CaseInsensitive))
                             path += ".shp";
                         try
                         {
                             FootprintOutput output;
                             output.saveShapefile(*savedFootprints, path);
                             status->showMessage("Shapefile saved: " + QDir::toNativeSeparators(path));
                         }
                         catch (const std::exception &e)
                         {
                             QMessageBox::critical(&window, "Save Shapefile", QString::fromUtf8(e.what()));
                         }
                     });

    auto cancellation = std::make_shared<std::atomic_bool>(false);
    auto *cancel = new QPushButton("Cancel inference", panel);
    cancel->setEnabled(false);
    panelLayout->addWidget(cancel);

    QObject::connect(cancel, &QPushButton::clicked, &window, [cancellation] { cancellation->store(true); });
    auto *progress = new QProgressBar(panel);
    progress->setRange(0, 100);
    progress->setValue(0);
    panelLayout->addWidget(progress);
    auto *details = new QTextEdit(panel);
    details->setReadOnly(true);
    details->setPlainText("Preparing the building model and sample raster.");
    panelLayout->addWidget(new QLabel("Inference diagnostics", panel));
    panelLayout->addWidget(details, 1);

    auto *legend = new QLabel(panel);
    legend->setTextFormat(Qt::RichText);
    legend->setText("<b>Building segmentation classes</b><br>"
                    "<font color='#ebebeb'>●</font> Background &nbsp; "
                    "<font color='#ff4020'>●</font> Building");
    legend->setWordWrap(true);
    panelLayout->addWidget(legend);
    auto *credits = new QLabel(panel);
    credits->setWordWrap(true);
    credits->setOpenExternalLinks(true);
    panelLayout->addWidget(credits);

    auto *dock = new QDockWidget("GeoKernel AI", &window);
    dock->setMinimumWidth(420);
    dock->setWidget(panel);
    window.addDockWidget(Qt::RightDockWidgetArea, dock);

    auto *toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);
    auto *zoomIn = toolbar->addAction(QIcon(":/icons/zoom-in.svg"), "Zoom In");
    auto *zoomOut = toolbar->addAction(QIcon(":/icons/zoom-out.svg"), "Zoom Out");
    auto *fullExtent = toolbar->addAction(QIcon(":/icons/full-extent.svg"), "Full Extent");
    toolbar->addSeparator();
    QActionGroup tools(&window);
    tools.setExclusive(true);
    auto *zoomBox = toolbar->addAction(QIcon(":/icons/zoom-box.svg"), "Zoom Rect");
    zoomBox->setCheckable(true);
    tools.addAction(zoomBox);
    auto *pan = toolbar->addAction(QIcon(":/icons/pan.svg"), "Pan");
    pan->setCheckable(true);
    pan->setChecked(true);
    tools.addAction(pan);
    QObject::connect(zoomIn, &QAction::triggered, viewer, &GisViewer::zoomIn);
    QObject::connect(zoomOut, &QAction::triggered, viewer, &GisViewer::zoomOut);
    QObject::connect(fullExtent, &QAction::triggered, viewer, &GisViewer::fullExtent);
    QObject::connect(zoomBox, &QAction::triggered, viewer,
                     [viewer] { viewer->setActiveTool(GisViewerTool::ZoomBox); });
    QObject::connect(pan, &QAction::triggered, viewer,
                     [viewer] { viewer->setActiveTool(GisViewerTool::Pan); });
    QObject::connect(viewer, &GisViewer::drawingProgressChanged, &window,
                     [progress](int value, const QString &text)
                     {
                         progress->setValue(std::clamp(value, 0, 100));
                         progress->setFormat(
                             QStringLiteral("%1% — %2").arg(std::clamp(value, 0, 100)).arg(text));
                     });

    auto openBaseRaster = [viewer](const QString &path)
    {
        if (path.trimmed().isEmpty())
            return;
        viewer->clearLayers();
        auto layer = std::make_unique<GisLayerTIFF>(path);
        layer->setName("Buildings RGB source");
        layer->open();
        viewer->addLayer(layer);
        viewer->fullExtent();
    };

    std::function<void()> prepareAll;
    QObject::connect(
        run, &QPushButton::clicked, &window,
        [&window, viewer, status, &modelPath, &rasterPath, provider, threshold, run, save, savedFootprints,
         &prepareAll, progress, details, cancel, cancellation, &modelUrl]
        {
            if (window.busy)
                return;
            if (!QFileInfo::exists(QDir(modelPath).filePath("geokernel-model.json")) ||
                !QFileInfo::exists(rasterPath))
                prepareAll();
            if (!QFileInfo::exists(QDir(modelPath).filePath("geokernel-model.json")) ||
                !QFileInfo::exists(rasterPath))
            {
                QMessageBox::warning(
                    &window, "GeoAiUsaBuildingFootprints",
                    "Model or sample preparation failed. Check your connection and click Run to retry.");
                return;
            }
            save->setEnabled(false);
            savedFootprints->clear();
            cancellation->store(false);
            window.busy = true;
            provider->setEnabled(false);
            threshold->setEnabled(false);
            cancel->setEnabled(true);
            run->setEnabled(false);
            progress->setValue(0);
            progress->setFormat("0% — Opening model package...");
            details->setPlainText("Validating the model package and preparing tiled inference...");
            auto result = std::make_shared<JobResult>();
            result->sourcePath = rasterPath;
            const QString packageDirectory = modelPath;
            const QString inputRaster = rasterPath;
            const double selectedThreshold = threshold->value();
            const auto selectedProvider = static_cast<AiExecutionProvider>(provider->currentData().toInt());

            auto *worker = QThread::create(
                [result, packageDirectory, inputRaster, selectedProvider, selectedThreshold, progress,
                 cancellation]
                {
                    try
                    {
                        const AiModelPackage package = AiModelPackage::open(packageDirectory);
                        const QStringList errors = package.validationErrors();
                        if (!errors.isEmpty())
                            throw std::runtime_error(errors.join("\n").toStdString());
                        result->manifest = package.manifest();
                        if (result->manifest.id != QStringLiteral("giswqs-geoai-usa-maskrcnn-resnet50-fpn"))
                            throw std::runtime_error(
                                "Select the GeoAI USA Mask R-CNN Buildings SDK model package.");
                        if (result->manifest.inputs.isEmpty() || result->manifest.outputs.isEmpty())
                            throw std::runtime_error(
                                "The model manifest does not define input and output tensors.");

                        QTemporaryDir temporaryOutput;
                        if (!temporaryOutput.isValid())
                            throw std::runtime_error("Could not create temporary inference workspace.");
                        result->vectorPath = temporaryOutput.filePath("buildings.geojson");
                        result->inferencePath = inputRaster;
                        if (cancellation->load())
                        {
                            result->cancelled = true;
                            return;
                        }
                        AiInstanceVectorizationRequest request;
                        request.inference.modelPackagePath = packageDirectory;
                        request.inference.rasterPath = result->inferencePath;
                        request.inference.sessionOptions.provider = selectedProvider;
                        request.inference.sessionOptions.intraOpThreads = 2;
                        request.inference.sessionOptions.interOpThreads = 1;
                        request.inference.isCancellationRequested = [cancellation]
                        { return cancellation->load(); };
                        request.outputPath = result->vectorPath;
                        request.connectivity = 4;
                        request.layerName = QStringLiteral("building_predictions");
                        request.instanceIdField = QStringLiteral("instance_id");
                        auto vectorized = runDemoInference(
                            request,
                            [progress](const AnalysisProgress &state)
                            {
                                QMetaObject::invokeMethod(
                                    progress,
                                    [progress, state]
                                    {
                                        progress->setValue(state.percent);
                                        progress->setFormat(
                                            QStringLiteral("%1% — %2").arg(state.percent).arg(state.message));
                                    },
                                    Qt::QueuedConnection);
                            },
                            selectedThreshold);
                        result->inference = vectorized.inference.labels;
                        result->cancelled = result->inference.cancelled || cancellation->load();
                        result->polygonCount = vectorized.polygonCount;
                        if (!result->cancelled)
                        {
                            QFile vectorFile(result->vectorPath);
                            if (!vectorFile.open(QIODevice::ReadOnly))
                                throw std::runtime_error("Could not retain building footprints for export.");
                            result->footprintGeoJson = vectorFile.readAll();
                            FootprintOutput output;
                            result->predictionLayer =
                                output.loadOverlay(result->vectorPath, result->inference.projectionWkt);
                            if (result->polygonCount > 0 && result->predictionLayer->count() == 0)
                                throw std::runtime_error(
                                    "Footprint output contains polygons but the display layer is empty.");
                        }
                    }
                    catch (const std::exception &exception)
                    {
                        if (cancellation->load())
                            result->cancelled = true;
                        else
                            result->error = QString::fromUtf8(exception.what());
                    }
                });
            QObject::connect(
                worker, &QThread::finished, &window,
                [&window, viewer, status, run, save, savedFootprints, progress, details, result, worker,
                 cancel, &modelPath, &rasterPath, modelUrl, provider, threshold]
                {
                    worker->deleteLater();
                    window.busy = false;
                    provider->setEnabled(true);
                    threshold->setEnabled(true);
                    cancel->setEnabled(false);
                    run->setEnabled(true);
                    if (result->cancelled)
                    {
                        progress->setValue(0);
                        progress->setFormat("Cancelled");
                        details->setPlainText("Inference cancelled. Run again when ready.");
                        return;
                    }
                    if (!result->error.isEmpty())
                    {
                        progress->setValue(0);
                        progress->setFormat("Inference failed");
                        details->setPlainText("Inference failed:\n" + result->error);
                        QMessageBox::critical(&window, "GeoAiUsaBuildingFootprints", result->error);
                        return;
                    }
                    try
                    {
                        progress->setValue(95);
                        progress->setFormat("95% — Opening source and prediction overlay...");
                        viewer->clearLayers();
                        result->sourceLayer = std::make_unique<GisLayerTIFF>(result->sourcePath);
                        result->sourceLayer->setName("RGB source - original resolution");
                        result->sourceLayer->open();
                        viewer->addLayer(result->sourceLayer);
                        const auto predictionStyle = footprintStyle();
                        if (!result->predictionLayer)
                            throw std::runtime_error("The building polygon layer was not created.");
                        result->predictionLayer->setName("Building prediction polygons");
                        result->predictionLayer->style() = predictionStyle;
                        result->predictionLayer->setVisible(true);
                        viewer->addLayer(result->predictionLayer);
                        viewer->fullExtent();
                        *savedFootprints = result->footprintGeoJson;
                        save->setEnabled(result->polygonCount > 0 && !savedFootprints->isEmpty());
                        details->setPlainText(
                            diagnostics(*result) +
                            "\n\nInference processes the entire raster at native resolution; the source "
                            "raster is preserved.\nPer-building masks are filtered at 0.50; overlapping "
                            "tiles use cross-tile box NMS. Model errors can remain.");
                        progress->setValue(100);
                        progress->setFormat("100% — Inference complete");
                        status->showMessage(QStringLiteral("%1 building polygons created in %2 ms.")
                                                .arg(result->polygonCount)
                                                .arg(result->inference.elapsedMilliseconds));
                    }
                    catch (const std::exception &exception)
                    {
                        result->error = QString::fromUtf8(exception.what());
                        details->setPlainText(
                            "Inference completed, but the raster/vector preview could not be opened:\n" +
                            result->error);
                        QMessageBox::critical(&window, "GeoAiUsaBuildingFootprints", result->error);
                    }
                });
            worker->start();
        });

    constexpr int datasetCount = static_cast<int>(DemoDatasets.size());
    std::array<QString, DemoDatasets.size()> datasetPaths;
    auto selectDataset = [&](int index)
    {
        if (window.busy || index < 0 || index >= datasetCount)
            return;
        if (datasetPaths[index].isEmpty())
        {
            viewer->clearLayers();
            rasterPath.clear();
            credits->clear();
            details->setPlainText("Sample unavailable. Click Run to download and prepare it.");
            return;
        }
        credits->setText(
            (DemoDatasets[index].localSource || !DemoDatasets[index].licenseUrl)
                ? QString("Imagery: %1<br>Model: GeoAI / Qiusheng Wu; torchvision / PyTorch")
                      .arg(DemoDatasets[index].attribution)
                : QString("Imagery: %1 - <a href=\"https://creativecommons.org/licenses/by/4.0/\">CC BY "
                          "4.0</a><br>Model: GeoAI / Qiusheng Wu; torchvision / PyTorch")
                      .arg(DemoDatasets[index].attribution));
        rasterPath = datasetPaths[index];
        try
        {
            openBaseRaster(datasetPaths[index]);
            progress->setValue(0);
            progress->setFormat("Ready");
            details->setPlainText(
                QString("%1\n\nRun inference to extract building footprints.\nOriginal imagery is preserved. "
                        "Inference uses the original raster resolution.\nModel: GeoAI USA Mask R-CNN "
                        "Buildings.\nThe entire raster is processed at native resolution. No independent "
                        "accuracy benchmark is claimed.")
                    .arg(DemoDatasets[index].name));
        }
        catch (const std::exception &e)
        {
            details->setPlainText(QString::fromUtf8(e.what()));
        }
    };
    prepareAll = [&]
    {
        if (window.busy)
            return;
        window.busy = true;
        run->setEnabled(false);
        bool modelReady = QFileInfo::exists(QDir(modelPath).filePath("geokernel-model.json"));
        if (!modelReady && QUrl(modelUrl).scheme() == "https")
        {
            const QString manifest = ensureSampleFile(QUrl(modelUrl), ModelArchive, "geoai-usa-model",
                                                      "geokernel-model.json", &window, ModelArchiveSha256);
            if (!manifest.isEmpty())
            {
                modelPath = QFileInfo(manifest).absolutePath();
                modelReady = true;
            }
        }
        bool complete = true;
        for (int i = 0; i < datasetCount; ++i)
        {
            const auto &item = DemoDatasets[i];
            status->showMessage(
                QString("Preparing %1 (%2/%3)...").arg(item.name).arg(i + 1).arg(datasetCount));
            datasetPaths[i] = prepareDataset(item, &window);
            if (datasetPaths[i].isEmpty())
            {
                if (item.localSource)
                    continue;
                complete = false;
                break;
            }
            try
            {
                FootprintOutput preparation;
                datasetPaths[i] = preparation.metricRaster(datasetPaths[i], &window);
            }
            catch (const std::exception &e)
            {
                sampleError(&window, "Raster preparation", QString::fromUtf8(e.what()));
                complete = false;
                break;
            }
        }
        window.busy = false;
        run->setEnabled(true);
        selectDataset(0);
        status->showMessage(complete ? "Model preparation finished; sample raster is ready."
                                     : "Preparation interrupted. Retry to continue from cache.");
        if (!modelReady)
            details->append("\nModel preparation failed. Check your connection and click Run to retry.");
    };
    QDir().mkpath(sampleDataPath());
    window.show();
    QTimer::singleShot(0, &window, prepareAll);

    return app.exec();
}
