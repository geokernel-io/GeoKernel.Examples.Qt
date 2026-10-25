#include "CoordinateSystems/CoordinateSystemFactory.h"
#include "CoordinateSystems/CoordinateTransformer.h"
#include "GeoKernelAI.h"
#include "ModelDownload.h"
#include "Sam3Overlay.h"
#include "Shapes/GisShapePolygon.h"
#include <vector>
#include "Raster/Tiff/GisLayerTIFF.h"
#include "Vector/GeoPackage/GisLayerGPKG.h"
#include "Viewer/GisViewer.h"
#include <QtWidgets>
#include <atomic>

using namespace GeoKernel::AI;
using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::CoordinateSystems;
using namespace GeoKernel::Formats::Raster::Tiff;
using namespace GeoKernel::Formats::Vector::GeoPackage;

struct Engine
{
    QString identity;
    std::unique_ptr<AiSamTextSession> text;
    std::unique_ptr<AiSamTrackerSession> tracker;
};

struct Result
{
    QString error, message, source, manifest;
    std::shared_ptr<QTemporaryDir> temporary;
    AiSamGisResult gis;
    std::unique_ptr<GisLayerTIFF> raster;
    std::unique_ptr<GisLayerVector> polygons;
};

class Window final : public QMainWindow
{
  public:
    GisViewer *viewer = new GisViewer(this);
    QComboBox *mode = new QComboBox, *provider = new QComboBox, *tool = new QComboBox;
    QLineEdit *manifest = new QLineEdit, *source = new QLineEdit, *prompt = new QLineEdit("building");
    QDoubleSpinBox *score = new QDoubleSpinBox;
    QPushButton *prepare = new QPushButton("Download / prepare sample"),
                *load = new QPushButton("Open selected raster"), *run = new QPushButton("Generate draft"),
                *accept = new QPushButton("Accept draft"), *discard = new QPushButton("Discard draft"),
                *save = new QPushButton("Save accepted GeoTIFF + GeoPackage"),
                *cancel = new QPushButton("Cancel");
    QLabel *status = new QLabel("Prepare sample to download the raster and selected SAM3 model."),
           *prompts = new QLabel("No points / box");
    QProgressBar *progress = new QProgressBar;
    QWidget *parameters = new QWidget;
    std::vector<std::unique_ptr<GeoKernel::Core::Shapes::GisShape>> promptMarkers;
    QVector<AiPromptPoint> points;
    std::optional<AiPromptBox> box;
    std::optional<QPointF> firstCorner;
    std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<Engine> engine = std::make_shared<Engine>();
    std::shared_ptr<QTemporaryDir> temporary;
    AiSamGisResult result;
    QJsonObject catalog;
    QString loadedSource;
    QThread *worker = nullptr;
    bool busy = false, closing = false, accepted = false;
    GisLayer *draft = nullptr;
    Window()
    {
        resize(1300, 850);
        setWindowTitle("SAM3 segmentation — GeoKernel");
        setWindowIcon(QIcon(":/geokernel.ico"));
        setCentralWidget(viewer);
        statusBar();
        connect(viewer, &GisViewer::drawingProgressChanged, this,
                [this](int value, const QString &text)
                {
                    if (!busy)
                    {
                        QString message = text;
                        message.replace("%p", QString::number(std::clamp(value, 0, 100)));
                        statusBar()->showMessage(message);
                    }
                });
        QFile index(":/sam3-assets.json");
        Sam3Download::require(index.open(QIODevice::ReadOnly), "Missing pinned model catalog");
        catalog = QJsonDocument::fromJson(index.readAll()).object();
        viewer->setActiveTool(GisViewerTool::Pan);
        auto dock = new QDockWidget("SAM3 inference", this);
        auto scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        auto body = new QWidget;
        auto layout = new QVBoxLayout(body);
        scroll->setWidget(body);
        dock->setWidget(scroll);
        dock->setMinimumWidth(370);
        dock->setMaximumWidth(440);
        addDockWidget(Qt::LeftDockWidgetArea, dock);
        auto form = new QFormLayout(parameters);
        mode->addItems({"Text segmentation", "Point / box segmentation"});
        form->addRow("Task", mode);
        provider->addItem("CPU", int(AiExecutionProvider::CPU));
        provider->addItem("CUDA", int(AiExecutionProvider::CUDA));
        if (AiSession::isProviderAvailable(AiExecutionProvider::CUDA))
            provider->setCurrentIndex(1);
        form->addRow("Provider", provider);
        form->addRow(prepare);
        form->addRow("Model manifest", manifest);
        auto browseModel = new QPushButton("Select local manifest…");
        form->addRow(browseModel);
        form->addRow("Raster", source);
        auto browseRaster = new QPushButton("Select local raster…");
        form->addRow(browseRaster);
        form->addRow(load);
        form->addRow("Text prompt", prompt);
        score->setRange(.01, .99);
        score->setValue(.5);
        score->setSingleStep(.05);
        form->addRow("Score threshold", score);
        tool->addItems({"Pan map", "Positive point", "Negative point", "Box: click two corners"});
        form->addRow("Map tool", tool);
        form->addRow(prompts);
        auto clear = new QPushButton("Clear prompts");
        form->addRow(clear);
        form->addRow(run);
        layout->addWidget(parameters);
        layout->addWidget(accept);
        layout->addWidget(discard);
        layout->addWidget(save);
        layout->addWidget(progress);
        layout->addWidget(cancel);
        status->setWordWrap(true);
        layout->addWidget(status);
        auto help = new QLabel(
            "Downloads are cached and SHA-256 checked. Text and tracker packages are separate, "
            "multi-GB downloads.\n\nResults first appear as a draft. Accept before saving; discard "
            "to run again. For per-feature approval/correction use GeoKernel.Editor's AI review "
            "workflow.\n\nPoint/box prompts use the raster CRS, converted from map clicks. Point "
            "mode uses the whole raster (up to 16M pixels). Text mode scans overlapping 512px tiles (up to "
            "200 objects per tile).");
        help->setWordWrap(true);
        layout->addWidget(help);
        layout->addStretch();
        updateButtons();
        connect(browseModel, &QPushButton::clicked, this,
                [this]
                {
                    auto p = QFileDialog::getOpenFileName(this, "SAM3 manifest", manifest->text(),
                                                          "JSON (*.json)");
                    if (!p.isEmpty())
                        manifest->setText(p);
                });
        connect(browseRaster, &QPushButton::clicked, this,
                [this]
                {
                    auto p = QFileDialog::getOpenFileName(this, "Raster", source->text(),
                                                          "GeoTIFF (*.tif *.tiff)");
                    if (!p.isEmpty())
                        source->setText(p);
                });
        connect(prepare, &QPushButton::clicked, this, [this] { prepareSample(); });
        connect(load, &QPushButton::clicked, this, [this] { loadRaster(); });
        connect(run, &QPushButton::clicked, this, [this] { segment(); });
        connect(accept, &QPushButton::clicked, this,
                [this]
                {
                    accepted = true;
                    draft->setName("SAM3 accepted instances");
                    status->setText("Draft accepted. Save to a new output folder.");
                    updateButtons();
                });
        connect(discard, &QPushButton::clicked, this,
                [this]
                {
                    clearDraft();
                    status->setText("Draft discarded.");
                });
        connect(save, &QPushButton::clicked, this, [this] { exportResult(); });
        connect(cancel, &QPushButton::clicked, this,
                [this]
                {
                    cancelled->store(true);
                    cancel->setEnabled(false);
                    status->setText("Cancelling… Backend/model-loading calls may need to finish.");
                });
        connect(clear, &QPushButton::clicked, this,
                [this]
                {
                    points.clear();
                    box.reset();
                    firstCorner.reset();
                    updatePrompts();
                });
        connect(mode, &QComboBox::currentIndexChanged, this,
                [this]
                {
                    manifest->clear();
                    tool->setCurrentIndex(0);
                });
        connect(tool, &QComboBox::currentIndexChanged, this,
                [this]
                {
                    firstCorner.reset();
                    updatePrompts();
                    viewer->setActiveTool(tool->currentIndex() == 0 ? GisViewerTool::Pan
                                                                    : GisViewerTool::Info);
                });
        connect(viewer, &GisViewer::mapClicked, this,
                [this](GisViewerTool, const QPointF &, const GeoKernel::Core::Shapes::GisShapePoint &world,
                       Qt::KeyboardModifiers)
                {
                    if (busy || draft || loadedSource.isEmpty() || tool->currentIndex() == 0)
                        return;
                    try
                    {
                        auto rasterCrs = viewer->mapLayerAt(0)->coordinateSystem();
                        auto mapCrs = viewer->coordinateSystem();
                        Sam3Download::require(rasterCrs && mapCrs, "Raster/map CRS unavailable");
                        CoordinateTransformer transform(*mapCrs, *rasterCrs);
                        auto p = transform.transform(world);
                        if (tool->currentIndex() == 3)
                        {
                            if (!firstCorner)
                                firstCorner = QPointF(p.x(), p.y());
                            else
                            {
                                box = AiPromptBox{
                                    std::min(firstCorner->x(), p.x()), std::min(firstCorner->y(), p.y()),
                                    std::max(firstCorner->x(), p.x()), std::max(firstCorner->y(), p.y())};
                                firstCorner.reset();
                            }
                        }
                        else
                            points.push_back({p.x(), p.y(), tool->currentIndex() == 1});
                        updatePrompts();
                        status->setText(
                            firstCorner
                                ? "First corner selected. Click the opposite corner."
                                : "Selection ready. Click Generate draft to segment the selected object.");
                    }
                    catch (const std::exception &e)
                    {
                        status->setText(QString::fromUtf8(e.what()));
                    }
                });

        QTimer::singleShot(0, this, [this] { prepareSample(); });
    }

    void updatePrompts()
    {
        viewer->clearShapes();
        promptMarkers.clear();
        if (viewer->layerCount() > 0 && viewer->coordinateSystem())
        {
            using namespace GeoKernel::Core::Shapes;
            auto rasterCrs = viewer->mapLayerAt(0)->coordinateSystem();
            if (rasterCrs)
            {
                CoordinateTransformer toMap(*rasterCrs, *viewer->coordinateSystem());
                auto marker = [&](double x, double y, const QString &color)
                {
                    auto p = toMap.transform(GisShapePoint(x, y));
                    auto shape = std::make_unique<GisShapePoint>(p.x(), p.y());
                    GisLayerStyle style;
                    style.setPointColor(color);
                    style.setPointSize(12);
                    viewer->addShape(shape.get(), style);
                    promptMarkers.push_back(std::move(shape));
                };
                for (const auto &p : points)
                    marker(p.x, p.y, p.positive ? "#00e676" : "#ff1744");
                if (firstCorner)
                    marker(firstCorner->x(), firstCorner->y(), "#ffeb3b");
                if (box)
                {
                    auto shape = std::make_unique<GisShapePolygon>();
                    QVector<GisShapePoint> ring;
                    for (const auto &p : QVector<GisShapePoint>{{box->minimumX, box->minimumY},
                                                                {box->maximumX, box->minimumY},
                                                                {box->maximumX, box->maximumY},
                                                                {box->minimumX, box->maximumY},
                                                                {box->minimumX, box->minimumY}})
                        ring.append(toMap.transform(p));
                    shape->parts().append(ring);
                    GisLayerStyle style;
                    style.setLineColor("#ffeb3b");
                    style.setLineWidth(2);
                    style.setFillColor("#ffeb3b");
                    style.setFillOpacity(25);
                    viewer->addShape(shape.get(), style);
                    promptMarkers.push_back(std::move(shape));
                }
            }
        }
        prompts->setText(QString("%1 points • box %2%3")
                             .arg(points.size())
                             .arg(box ? "set" : "none")
                             .arg(firstCorner ? " • click second corner" : ""));
    }

    void updateButtons()
    {
        parameters->setEnabled(!busy && !draft);
        run->setEnabled(!busy && !draft);
        accept->setEnabled(!busy && draft && !accepted);
        discard->setEnabled(!busy && draft);
        save->setEnabled(!busy && draft && accepted);
        cancel->setEnabled(busy);
    }

    void closeEvent(QCloseEvent *e) override
    {
        if (busy)
        {
            closing = true;
            cancelled->store(true);
            e->ignore();
            status->setText("Waiting for background task to stop…");
        }
        else
            QMainWindow::closeEvent(e);
    }

    void launch(std::function<void(Result &)> action, std::function<void(Result &)> apply)
    {
        if (busy)
            return;
        busy = true;
        cancelled->store(false);
        updateButtons();
        progress->setRange(0, 0);
        auto out = std::make_shared<Result>();
        worker = QThread::create(
            [out, action]
            {
                try
                {
                    action(*out);
                }
                catch (const std::exception &e)
                {
                    out->error = QString::fromUtf8(e.what());
                }
            });
        connect(worker, &QThread::finished, this,
                [this, out, apply]
                {
                    worker->deleteLater();
                    worker = nullptr;
                    busy = false;
                    progress->setRange(0, 100);
                    if (!out->error.isEmpty())
                        status->setText(out->error);
                    else if (cancelled->load())
                        status->setText("Cancelled. No draft applied.");
                    else
                        try
                        {
                            apply(*out);
                        }
                        catch (const std::exception &e)
                        {
                            status->setText(QString::fromUtf8(e.what()));
                        }
                    updateButtons();
                    if (closing)
                        close();
                });
        worker->start();
    }

    auto reporter()
    {
        return [this](int value, const QString &text)
        {
            QMetaObject::invokeMethod(
                this,
                [this, value, text]
                {
                    if (!cancelled->load())
                    {
                        progress->setRange(0, 100);
                        progress->setValue(value);
                        status->setText(text);
                    }
                },
                Qt::QueuedConnection);
        };
    }

    void applyRaster(Result &r)
    {
        Sam3Download::require(r.raster->coordinateSystem() != nullptr, "Raster requires a known CRS");
        auto crs = CoordinateSystemFactory::fromUserInput(r.raster->coordinateSystem()->definition());
        Sam3Download::require(bool(crs), "Cannot resolve raster CRS");
        viewer->clearLayers();
        r.raster->setName("SAM3 source raster");
        viewer->setCoordinateSystem(crs);
        viewer->addLayer(r.raster);
        viewer->fullExtent();
        loadedSource = r.source;
        source->setText(loadedSource);
        points.clear();
        box.reset();
        firstCorner.reset();
        updatePrompts();
        status->setText("Raster ready. Generate a draft.");
        progress->setValue(100);
    }

    void prepareSample()
    {
        auto kind = mode->currentIndex() == 0 ? "text" : "tracker";
        auto root = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("sam3");
        auto stopped = cancelled;
        auto callback = reporter();
        auto pinned = catalog;
        launch(
            [=](Result &r)
            {
                auto stop = [stopped] { return stopped->load(); };
                r.source = Sam3Download::ensure(pinned, "sample", root, stop, callback);
                r.manifest = Sam3Download::ensure(pinned, kind, root, stop, callback);
                Sam3Download::check(stop);
                r.raster = std::make_unique<GisLayerTIFF>(r.source);
                r.raster->open();
            },
            [this](Result &r)
            {
                manifest->setText(r.manifest);
                applyRaster(r);
            });
    }

    void loadRaster()
    {
        auto path = source->text();
        launch(
            [path](Result &r)
            {
                r.source = path;
                r.raster = std::make_unique<GisLayerTIFF>(path);
                r.raster->open();
            },
            [this](Result &r) { applyRaster(r); });
    }

    void segment()
    {
        if (loadedSource.isEmpty() || source->text() != loadedSource)
        {
            status->setText("Open selected raster first.");
            return;
        }

        bool text = mode->currentIndex() == 0;

        if (!text && points.isEmpty() && !box)
        {
            status->setText("Add a positive point or a box on the map.");
            return;
        }

        auto package = manifest->text(), path = loadedSource, words = prompt->text();
        auto selectedPoints = points;
        auto selectedBox = box;
        auto chosen = AiExecutionProvider(provider->currentData().toInt());
        double threshold = score->value();
        auto stopped = cancelled;
        auto state = engine;
        auto callback = reporter();
        launch(
            [=](Result &r)
            {
                auto stop = [stopped] { return stopped->load(); };
                Sam3Download::require(!package.isEmpty(), "Prepare sample or select a local SAM3 manifest.");
                callback(0, "Validating / loading SAM3 model…");
                auto key = package + QString::number(int(chosen)) + (text ? "text" : "tracker");
                if (state->identity != key)
                {
                    state->text.reset();
                    state->tracker.reset();
                    state->identity.clear();
                    AiSessionOptions options;
                    options.provider = chosen;
                    options.intraOpThreads = 8;
                    if (text)
                        state->text = std::make_unique<AiSamTextSession>(package, options);
                    else
                        state->tracker = std::make_unique<AiSamTrackerSession>(package, options);
                    state->identity = key;
                }
                Sam3Download::check(stop);
                r.temporary = std::make_shared<QTemporaryDir>();
                Sam3Download::require(r.temporary->isValid(), "Cannot create result workspace");
                AiSamGisOptions gis;
                gis.outputDirectory = r.temporary->filePath("result");
                gis.isCancellationRequested = stop;
                gis.execution.maximumMilliseconds = 1800000;
                if (text)
                {
                    AiSamTiledRequest request;
                    request.inference.rasterPath = path;
                    request.tiling = {512, 512, 96, 96};
                    request.inference.maximumInstances = 200;
                    request.inference.text = words;
                    request.inference.scoreThreshold = threshold;
                    request.inference.isCancellationRequested = stop;
                    r.gis =
                        state->text->segmentToGis(request, gis, [callback](const AiRasterInferenceProgress &p)
                                                  { callback(p.percent, p.message); });
                }
                else
                {
                    AiSamTrackerRequest request;
                    request.rasterPath = path;
                    request.points = selectedPoints;
                    request.box = selectedBox;
                    request.sourceCrsCoordinates = true;
                    request.isCancellationRequested = stop;
                    r.gis = state->tracker->segmentToGis(request, gis);
                }
                Sam3Download::require(r.gis.completed, r.gis.timedOut ? "Inference timed out"
                                                                      : "Inference cancelled or incomplete");
                Sam3Download::check(stop);
                r.polygons = Sam3Overlay().loadOverlay(r.gis.vectorPath);
                Sam3Download::require(r.polygons->count() == r.gis.instances,
                                      "Prediction overlay count differs from the GIS result.");
            },
            [this](Result &r)
            {
                r.polygons->setName("SAM3 draft — review before saving");
                r.polygons->style().setFillColor("#ff7038");
                r.polygons->style().setFillOpacity(110);
                r.polygons->style().setLineColor("#ffb060");
                r.polygons->style().setLineWidth(1.25F);
                r.polygons->setVisible(true);
                draft = r.polygons.get();
                viewer->addLayer(r.polygons);
                viewer->invalidateRenderCache(true, true);
                viewer->update();
                temporary = r.temporary;
                result = r.gis;
                accepted = false;
                progress->setValue(100);
                status->setText(QString("%1 draft instances. Review on the map, then accept or discard.")
                                    .arg(result.instances));
            });
    }

    void clearDraft()
    {
        for (int i = 0; i < viewer->layerCount(); ++i)
            if (viewer->mapLayerAt(i) == draft)
            {
                viewer->removeLayer(i);
                break;
            }
        draft = nullptr;
        temporary.reset();
        result = {};
        accepted = false;
        updateButtons();
    }

    void exportResult()
    {
        auto parent = QFileDialog::getExistingDirectory(this, "Choose output parent folder");
        if (parent.isEmpty())
            return;
        bool ok = false;
        auto name =
            QInputDialog::getText(this, "New output folder", "Name", QLineEdit::Normal, "sam3-result", &ok);
        if (!ok || name.isEmpty() || name == "." || name == ".." || name.contains('/') || name.contains('\\'))
            return;
        auto target = QDir(parent).filePath(name);
        auto from = result.directoryPath;
        auto keep = temporary;
        auto stop = cancelled;
        launch(
            [=](Result &r)
            {
                Sam3Download::require(!QFileInfo::exists(target), "Output folder already exists");
                QTemporaryDir stage(parent + "/.sam3-export-XXXXXX");
                Sam3Download::require(stage.isValid(), "Cannot create export staging folder");
                for (const QString &file : {"instances.tif", "instances.gpkg", "manifest.json"})
                {
                    QFile in(QDir(from).filePath(file)), out(stage.filePath(file));
                    Sam3Download::require(in.open(QIODevice::ReadOnly) && out.open(QIODevice::WriteOnly),
                                          "Cannot copy output");
                    while (!in.atEnd())
                    {
                        Sam3Download::check([stop] { return stop->load(); });
                        auto bytes = in.read(1024 * 1024);
                        Sam3Download::require(!bytes.isEmpty() && out.write(bytes) == bytes.size(),
                                              "Output copy failed");
                    }
                    Sam3Download::require(out.flush(), "Cannot flush output");
                }
                Sam3Download::check([stop] { return stop->load(); });
                Sam3Download::require(QDir().rename(stage.path(), target), "Cannot publish output folder");
                stage.setAutoRemove(false);
                r.message = "Saved accepted result: " + target;
            },
            [this](Result &r) { status->setText(r.message); });
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("Sam3SegmentationInference");
    app.setOrganizationName("GeoKernel");

    try
    {
        Window window;
        window.show();
        return app.exec();
    }
    catch (const std::exception &e)
    {
        QMessageBox::critical(nullptr, "SAM3", QString::fromUtf8(e.what()));
        return 1;
    }
}
