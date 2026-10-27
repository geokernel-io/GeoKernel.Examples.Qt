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
#include "SampleSupport.h"
#include "Raster/Tiff/GisLayerTIFF.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::AI;
using namespace GeoKernel::Viewer;
using namespace GeoKernel::Formats::Raster::Tiff;
using namespace GeoKernel::Formats::Common::Gdal;

struct Assets
{
    QString model, input, rgb, valid;
};

Assets prepareAssets(QWidget *parent = nullptr)
{

    Assets assets;
    const QString manifest = ensureSampleFile(QUrl(ModelUrl), ModelArchive, "dynamicworld-model",
                                              "geokernel-model.json", parent, ModelSha);

    if (manifest.isEmpty())
        return {};

    assets.model = QFileInfo(manifest).absolutePath();

    auto sample = [&](const char *name)
    {
        return ensureSampleFile(QUrl(DatasetUrl), DatasetArchive, "dynamicworld-spain", name, parent,
                                DatasetSha);
    };

    assets.input = sample("dynamicworld_spain_sentinel2_l1c.tif");
    if (assets.input.isEmpty())
        return {};

    assets.rgb = sample("dynamicworld_spain_rgb.tif");
    if (assets.rgb.isEmpty())
        return {};

    assets.valid = sample("dynamicworld_spain_valid_mask.tif");
    if (assets.valid.isEmpty())
        return {};

    return assets;
}

bool ready(const Assets &a)
{
    return QFileInfo::exists(QDir(a.model).filePath("geokernel-model.json")) && QFileInfo::exists(a.input) &&
           QFileInfo::exists(a.rgb) && QFileInfo::exists(a.valid);
}

struct Result
{
    QString error, preview;
    QByteArray geoJson;
    AiRasterInferenceResult classes;
    QVector<qint64> histogram = QVector<qint64>(9, 0);
    qint64 polygons = 0;
    std::shared_ptr<QTemporaryDir> workspace;
};

std::shared_ptr<Result> classify(const Assets &assets, AiExecutionProvider provider, const std::shared_ptr<std::atomic_bool> &cancellation, AiRasterInferenceProgressCallback progress = {})
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
        const auto package = AiModelPackage::open(assets.model);
        const auto errors = package.validationErrors();

        if (!errors.isEmpty())
            throw std::runtime_error(errors.join("\n").toStdString());

        const auto &m = package.manifest();

        if (m.id != "google-dynamicworld" || m.inputAdapters.isEmpty() || m.outputDecoders.isEmpty())
            throw std::runtime_error("The fixed Dynamic World package is invalid.");

        GdalRasterProvider input, validity;
        QString error;

        if (!input.open(assets.input, &error) || !validity.open(assets.valid, &error))
            throw std::runtime_error(error.toStdString());

        const auto meta = input.metadata(), maskMeta = validity.metadata();

        if (meta.bandCount != 9 || !meta.hasWorldTransform || meta.width != maskMeta.width ||
            meta.height != maskMeta.height || meta.projectionWkt != maskMeta.projectionWkt)
            throw std::runtime_error(
                "The nine-band input and validity mask must share their dimensions and CRS.");

        const auto &a = meta.worldTransform;
        const auto &b = maskMeta.worldTransform;

        if (a.upperLeftCenterX != b.upperLeftCenterX || a.upperLeftCenterY != b.upperLeftCenterY ||
            a.pixelSizeX != b.pixelSizeX || a.pixelSizeY != b.pixelSizeY || a.rotationX != b.rotationX ||
            a.rotationY != b.rotationY)
            throw std::runtime_error("The validity mask does not match the input pixel grid.");

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
                                                                 auto value = state;
                                                                 value.percent = state.percent * 85 / 100;
                                                                 progress(value);
                                                             }
                                                         });

        if (result->classes.cancelled || stop())
            return result;

        if (result->classes.dataType != AiTensorDataType::Int32 || result->classes.bandCount != 1)
            throw std::runtime_error("Unexpected classification output.");

        GdalRasterWindowRequest window;
        window.sourceWidth = meta.width;
        window.sourceHeight = meta.height;
        window.targetWidth = meta.width;
        window.targetHeight = meta.height;
        window.forcedOverviewLevel = -1;

        const QImage mask = validity.readWindow(window, &error).convertToFormat(QImage::Format_RGB888);

        if (mask.isNull())
            throw std::runtime_error(error.toStdString());

        auto *values = reinterpret_cast<qint32 *>(result->classes.data.data());

        for (int y = 0; y < meta.height; ++y)
        {

            if (stop())
                return result;

            const uchar *row = mask.constScanLine(y);

            for (int x = 0; x < meta.width; ++x)
            {

                auto &id = values[y * meta.width + x];

                if (row[x * 3] == 0)
                    id = -1;
                else if (id < 0 || id > 8)
                    throw std::runtime_error("Invalid land-cover class ID.");

                if (id >= 0)
                    ++result->histogram[id];
            }
        }

        result->workspace = std::make_shared<QTemporaryDir>();

        if (!result->workspace->isValid())
            throw std::runtime_error("Could not create temporary inference workspace.");

        LandCoverOutput writer;
        result->preview = result->workspace->filePath("classification-rgba.tif");

        writer.writePreview(result->classes, result->preview, ClassColors);

        if (progress)
            progress({result->classes.processedTiles, result->classes.processedTiles, 90,
                      "Creating class polygons..."});

        const QString path = result->workspace->filePath("landcover.geojson");

        result->polygons =
            writer.write(result->classes, path, [cancellation] { return cancellation->load(); });

        if (stop())
            return result;

        QFile file(path);

        if (!file.open(QIODevice::ReadOnly))
            throw std::runtime_error("Could not read class polygons.");

        auto document = QJsonDocument::fromJson(file.readAll()).object();
        auto features = document["features"].toArray();

        for (int i = 0; i < features.size(); ++i)
        {

            auto f = features[i].toObject();
            auto properties = f["properties"].toObject();
            const int id = properties["class_id"].toInt(-1);

            if (id < 0 || id > 8)
                throw std::runtime_error("Invalid exported class ID.");

            properties["class_name"] = ClassNames[id];
            f["properties"] = properties;
            features[i] = f;
        }

        document["features"] = features;
        result->geoJson = QJsonDocument(document).toJson(QJsonDocument::Compact);
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
    QString text = QString("Dynamic World land-cover classification\n\nProvider: %1\nRaster: %2 x %3, 9 "
                           "input bands\nTiles: %4\nInference: %5 ms\nClass polygons: %6\n\n")
                       .arg(aiExecutionProviderName(r.provider))
                       .arg(r.width)
                       .arg(r.height)
                       .arg(r.processedTiles)
                       .arg(r.elapsedMilliseconds)
                       .arg(result.polygons);

    for (int i = 0; i < 9; ++i)
        text += QString("%1: %2 pixels\n").arg(ClassNames[i]).arg(result.histogram[i]);

    return text + "\nResults are session-only. Use Save Shapefile to export. Cloud/shadow detection is not "
                  "included.";
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
    window.setWindowTitle("DynamicWorldInference");
    window.setWindowIcon(QIcon(":/icons/geokernel.ico"));
    auto *status = window.statusBar();
    auto *viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);
    auto *panel = new QWidget(&window);
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel("<b>Google Dynamic World — land cover</b>", panel));
    auto *description =
        new QLabel("Classify the Spain Sentinel-2 L1C sample into nine land-cover classes.", panel);
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

        if (auto *layer = viewer->mapLayerByName("Dynamic World classification"))
        {
            layer->setOpacity(transparency->value() / 100.0);
            viewer->invalidateRenderCache(true, true);
            viewer->update();
        }
    };

    QObject::connect(transparency, &QSlider::valueChanged, &window, [=](int) { applyTransparency(); });

    auto *run = new QPushButton("Run land-cover classification", panel);
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

    QString legend = "<b>Land-cover classes</b><br>";

    for (int i = 0; i < 9; ++i)
        legend += QString("<span style='color:%1'>●</span> %2<br>").arg(ClassColors[i], ClassNames[i]);

    auto *labels = new QLabel(legend, panel);
    layout->addWidget(labels);
    auto *credits = new QLabel("Contains modified Copernicus Sentinel data 2019.<br>Model: Google / Dynamic "
                               "World Project.<br>National Geographic; World Resources Institute.<br>Model: "
                               "Apache-2.0. See downloaded notices.",
                               panel);
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

    QObject::connect(toolbar->addAction(QIcon(":/icons/zoom-in.svg"), "Zoom In"), &QAction::triggered, viewer, &GisViewer::zoomIn);
    QObject::connect(toolbar->addAction(QIcon(":/icons/zoom-out.svg"), "Zoom Out"), &QAction::triggered, viewer, &GisViewer::zoomOut);
    QObject::connect(toolbar->addAction(QIcon(":/icons/full-extent.svg"), "Full Extent"), &QAction::triggered, viewer, &GisViewer::fullExtent);
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
        assets = prepareAssets(&window);
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
            addRaster(viewer, assets.rgb, "Spain - RGB preview");
            viewer->fullExtent();
            progress->setValue(0);
            progress->setFormat("Ready");
            details->setPlainText(
                "Spain / Alicante\n512 x 512 pixels, 10 m, nine Sentinel-2 L1C bands.\n\nClick Run to "
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
                                     QMessageBox::critical(&window, "DynamicWorldInference",
                                                           completed->error);
                                     return;
                                 }

                                 try
                                 {
                                     viewer->clearLayers();
                                     lastResult.reset();
                                     addRaster(viewer, assets.rgb, "Spain - RGB preview");
                                     addRaster(viewer, completed->preview, "Dynamic World classification");
                                     lastResult = completed;
                                     transparency->setEnabled(true);
                                     applyTransparency();
                                     viewer->fullExtent();
                                     details->setPlainText(diagnostics(*completed));
                                     progress->setValue(100);
                                     progress->setFormat("Classification complete");
                                     save->setEnabled(completed->polygons > 0);
                                     status->showMessage(QString("Nine-class land cover generated in %1 ms.")
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
                         QString path = QFileDialog::getSaveFileName(
                             &window, "Save land-cover polygons", "landcover.shp", "ESRI Shapefile (*.shp)");

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
