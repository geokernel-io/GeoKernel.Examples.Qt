#include "TerrainSupport.h"
#include <GeoKernelAI.h>
#include <QSpinBox>
#include <QUuid>
#include <cstring>

using namespace GeoKernel::AI;

struct AiHeight
{
    Values raw, dtm;
    QJsonObject provenance;
    int tiles = 0, contextFilled = 0, clamped = 0;
};
AiHeight inferAi(const Raster& dsm, const QString& modelDirectory, int overlap, int threads, const QString& directory,
                 std::atomic_bool& cancelled, const Progress& progress)
{
    const auto package = AiModelPackage::open(modelDirectory);
    require(package.manifest().modelSha256 == "e400e27f6afc75bcf4e1a61432ee9557529683903996dd953d3e8e3fcbe90eb6",
            "This example requires the pinned pix_grad adaptation.");
    AiHeight output;
    QString context = dsm.path;
    for (uchar valid : dsm.valid)
        output.contextFilled += !valid;
    if (output.contextFilled)
    {
        progress(38, "Filling missing DSM context from DSM support only...");
        const auto nearest = supportMap(dsm.valid, dsm.width, dsm.height, cancelled);
        auto filled = dsm.z;
        for (int i = 0; i < filled.size(); ++i)
            if (!dsm.valid[i])
                filled[i] = dsm.z[nearest[i]];
        context = QDir(directory).filePath("dsm-context.tif");
        RasterRuntime runtime;
        runtime.write(dsm, context, filled.constData(), 6);
    }
    AiSessionOptions session;
    session.provider = AiExecutionProvider::CPU;
    session.intraOpThreads = threads;
    session.interOpThreads = 1;
    auto request = AiRasterInferenceExecutor::requestFromPackage(package, dsm.path, session);
    request.sourceBands = {{context, 1}};
    request.preserveSourceMask = true;
    request.outputNoData = NoData;
    request.tiling.overlapX = request.tiling.overlapY = overlap;
    request.isCancellationRequested = [&cancelled]
    {
        return cancelled.load();
    };
    const auto result = AiRasterInferenceExecutor::run(
        request,
        [&](const AiRasterInferenceProgress& p)
        {
            progress(40 + int(p.percent * .4),
                     QString("AI tiles %1 / %2 · %3").arg(p.completedTiles).arg(p.totalTiles).arg(p.message));
        });
    checkpoint(cancelled);
    require(!result.cancelled && result.width == dsm.width && result.height == dsm.height && result.bandCount == 1 &&
                result.dataType == AiTensorDataType::Float32 &&
                result.data.size() == dsm.z.size() * qint64(sizeof(float)),
            "AI result dimensions or numeric type do not match the source.");
    const auto a = CoordinateSystemFactory::fromUserInput(dsm.projection),
               b = CoordinateSystemFactory::fromUserInput(result.projectionWkt);
    require(a && b && CoordinateSystemFactory::areEquivalent(*a, *b) && result.geoTransform == dsm.transform,
            "AI inference changed the source grid or CRS.");
    output.raw.resize(dsm.z.size());
    std::memcpy(output.raw.data(), result.data.constData(), size_t(result.data.size()));
    output.dtm = output.raw;
    for (int i = 0; i < dsm.z.size(); ++i)
    {
        if (!dsm.valid[i])
        {
            output.raw[i] = output.dtm[i] = NoData;
            continue;
        }
        require(std::isfinite(output.raw[i]), "AI returned a nonfinite valid-source height.");
        if (output.dtm[i] > dsm.z[i])
        {
            output.dtm[i] = dsm.z[i];
            ++output.clamped;
        }
    }
    output.provenance = result.provenance.toJson();
    output.tiles = result.processedTiles;
    return output;
}
QRect comparisonWindow(const Scene& scene)
{
    const auto& dsm = *scene.dsm;
    if (scene.manifest.value("standard_full_tile").toBool())
        return QRect(200, 800, 500, 500);
    const int left = dsm.width / 5, top = dsm.height / 5;
    return QRect(left, top, dsm.width - 2 * left, dsm.height - 2 * top);
}
Score areaScore(const Raster& dsm, const Values& z, const Raster* reference, const QRect& area)
{
    Score m;
    if (!reference)
        return m;
    for (int y = area.top(); y < area.top() + area.height(); ++y)
        for (int x = area.left(); x < area.left() + area.width(); ++x)
        {
            const int i = y * dsm.width + x;
            if (!dsm.valid[i] || !reference->valid[i])
                continue;
            const double e = double(z[i]) - reference->z[i];
            ++m.count;
            m.rmse += e * e;
            m.mae += std::abs(e);
            m.bias += e;
        }
    if (m.count)
    {
        m.rmse = std::sqrt(m.rmse / m.count);
        m.mae /= m.count;
        m.bias /= m.count;
    }
    return m;
}
Raster cutRaster(const Raster& source, const QRect& rect)
{
    Raster output = source;
    output.width = rect.width();
    output.height = rect.height();
    output.transform[0] += rect.left() * source.pixel;
    output.transform[3] -= rect.top() * source.pixel;
    output.z.resize(output.width * output.height);
    output.valid.resize(output.z.size());
    for (int y = 0; y < output.height; ++y)
        for (int x = 0; x < output.width; ++x)
        {
            const int a = y * output.width + x, b = (rect.top() + y) * source.width + rect.left() + x;
            output.z[a] = source.z[b];
            output.valid[a] = source.valid[b];
        }
    return output;
}
Values cutValues(const Values& source, int sourceWidth, const QRect& rect)
{
    Values values(rect.width() * rect.height());
    for (int y = 0; y < rect.height(); ++y)
        std::copy_n(source.constData() + (rect.top() + y) * sourceWidth + rect.left(), rect.width(),
                    values.data() + y * rect.width());
    return values;
}
struct Comparison
{
    Terrain classical;
    AiHeight ai;
    int overlap = 64, threads = 4;
    QString modelDirectory;
    QJsonObject modelManifest;
    std::array<std::array<QString, 3>, 2> maps;
    std::array<Score, 2> full, crop;
    QJsonObject files, report;
    QString directory;
};
void evaluateComparison(const Scene& scene, Comparison& result, const QJsonObject& modelAssets)
{
    const auto& dsm = *scene.dsm;
    const auto rect = comparisonWindow(scene);
    for (int method = 0; method < 2; ++method)
    {
        const auto& heights = method ? result.ai.dtm : result.classical.dtm;
        result.full[method] = score(dsm, heights, scene.reference.get());
        result.crop[method] = areaScore(dsm, heights, scene.reference.get(), rect);
    }
    QJsonArray transform;
    for (double v : dsm.transform)
        transform.append(v);
    result.report = {
        {"schema_version", 1},
        {"method", "pix_grad_live_and_independent_native_classical"},
        {"source_dsm", dsm.path},
        {"source_sha256", dsm.sha256},
        {"reference", scene.reference ? QJsonValue(scene.reference->path) : QJsonValue()},
        {"reference_sha256", scene.reference ? QJsonValue(scene.reference->sha256) : QJsonValue()},
        {"reference_used_for_processing", false},
        {"classical_uses_ai", false},
        {"ai_uses_classical", false},
        {"model_archive_sha256", modelAssets.value("sha256")},
        {"model_sha256", result.modelManifest.value("modelSha256")},
        {"model_manifest", result.modelManifest},
        {"sdk_inference", result.ai.provenance},
        {"ai", QJsonObject{{"overlap_pixels", result.overlap},
                           {"cpu_threads", result.threads},
                           {"processed_tiles", result.ai.tiles},
                           {"context_filled_source_pixels", result.ai.contextFilled},
                           {"context_fill", "four-neighbour DSM-only support propagation"},
                           {"upper_bound_clamped_pixels", result.ai.clamped},
                           {"raw_output_retained", true}}},
        {"classical", QJsonObject{{"method", Method},
                                  {"parameters", result.classical.parameters.json()},
                                  {"summary", result.classical.summary}}},
        {"grid",
         QJsonObject{
             {"width", dsm.width}, {"height", dsm.height}, {"transform", transform}, {"crs_wkt", dsm.projection}}},
        {"evaluation",
         QJsonObject{{"full", QJsonObject{{"classical", result.full[0].json()}, {"ai", result.full[1].json()}}},
                     {"comparison_crop", QJsonObject{{"pixel_window", QJsonObject{{"column", rect.x()},
                                                                                  {"row", rect.y()},
                                                                                  {"width", rect.width()},
                                                                                  {"height", rect.height()}}},
                                                     {"classical", result.crop[0].json()},
                                                     {"ai", result.crop[1].json()}}}}},
        {"source_manifest", scene.manifest},
        {"output_files", result.files},
        {"attribution",
         "Terrain sample: Federal Office of Topography swisstopo. Model weights: David Perez, Zenodo 7973800, CC BY 4.0; deterministic ONNX adaptation."},
        {"limits",
         "Experimental regional model; fixed development scene, not held-out benchmark. Metre heights, no vertical datum conversion. Source filename years differ."}};
}
std::shared_ptr<Comparison> compare(const Scene& scene, const QString& modelDirectory, Parameters parameters,
                                    int overlap, int threads, const QString& directory, const QJsonObject& modelAssets,
                                    std::atomic_bool& cancelled, const Progress& progress)
{
    auto output = std::make_shared<Comparison>();
    auto& result = *output;
    const auto& dsm = *scene.dsm;
    result.directory = directory;
    result.modelDirectory = modelDirectory;
    result.overlap = overlap;
    result.threads = threads;
    result.modelManifest = readJson(QDir(modelDirectory).filePath("geokernel-model.json"));
    result.classical = estimate(dsm, parameters, cancelled,
                                [&](int percent, const QString& text)
                                {
                                    progress(int(percent * .35), "Classical: " + text);
                                });
    result.ai = inferAi(dsm, modelDirectory, overlap, threads, directory, cancelled, progress);
    RasterRuntime runtime;
    const auto numeric = [&](const QString& name, const Values& values)
    {
        checkpoint(cancelled);
        runtime.write(dsm, QDir(directory).filePath(name), values.constData(), 6);
        result.files.insert(name, name);
    };
    progress(82, "Writing native-resolution numeric results...");
    numeric("classical-dtm.tif", result.classical.dtm);
    numeric("ai-dtm.tif", result.ai.dtm);
    numeric("ai-raw.tif", result.ai.raw);
    numeric("classical-removed.tif", result.classical.removed);
    Terrain aiDisplay;
    aiDisplay.dtm = result.ai.dtm;
    aiDisplay.removed.resize(dsm.z.size());
    for (int i = 0; i < dsm.z.size(); ++i)
        aiDisplay.removed[i] = dsm.valid[i] ? std::max(0.f, dsm.z[i] - result.ai.dtm[i]) : NoData;
    numeric("ai-removed.tif", aiDisplay.removed);
    runtime.write(dsm, QDir(directory).filePath("classical-qa.tif"), result.classical.quality.constData(), 1);
    result.files.insert("classical-qa.tif", "classical-qa.tif");
    for (int method = 0; method < 2; ++method)
    {
        const auto& terrain = method ? aiDisplay : result.classical;
        for (int mode = 0; mode < 3; ++mode)
        {
            progress(86 + method * 4 + mode, QString("Preparing %1 display...").arg(method ? "AI" : "classical"));
            const QImage image = mode == 0
                                     ? relief(dsm, terrain.dtm, cancelled)
                                     : diagnostic(dsm, terrain, mode == 1 ? 0 : 2, scene.reference.get(), cancelled);
            const QString path = QDir(directory).filePath(QString("view-%1-%2.tif").arg(method).arg(mode));
            runtime.write(dsm, path, nullptr, 1, &image);
            result.maps[method][mode] = path;
        }
    }
    progress(96, "Preparing the shared comparison crop and scores...");
    const QRect rect = comparisonWindow(scene);
    const Raster cropped = cutRaster(dsm, rect);
    const auto crop = [&](QString name, const Values& data)
    {
        const auto values = cutValues(data, dsm.width, rect);
        runtime.write(cropped, QDir(directory).filePath(name), values.constData(), 6);
        result.files.insert(name, name);
    };
    crop("comparison-dsm.tif", dsm.z);
    crop("comparison-classical.tif", result.classical.dtm);
    crop("comparison-ai.tif", result.ai.dtm);
    if (scene.reference)
        crop("comparison-reference.tif", scene.reference->z);
    evaluateComparison(scene, result, modelAssets);
    checkpoint(cancelled);
    return output;
}
struct AiOutcome
{
    std::shared_ptr<Scene> scene;
    std::shared_ptr<Comparison> comparison;
    QString message, error;
};
class AiWindow final : public QMainWindow
{
public:
    explicit AiWindow(const QCommandLineParser& arguments)
        : arguments_(arguments), sampleAssets_(readJson(":/sample-assets.json")),
          modelAssets_(readJson(":/model-assets.json")), terrainAssets_(readJson(":/terrain-assets.json"))
    {
        setWindowTitle("DSM to DTM | Live AI versus classical — GeoKernel");
        resize(1760, 980);
        setWindowIcon(QIcon(":/icons/geokernel.ico"));
        setStyleSheet(
            "QMainWindow,QWidget#page{background:#f2f6f8;color:#233e50;} QLabel#title{font-size:22px;font-weight:600;color:#145e72;} QLabel#heading{background:#e4eef2;color:#125a70;padding:9px;font-weight:600;} QPushButton{padding:7px;} QToolBar{background:white;border-bottom:1px solid #d5e2e9;spacing:8px;padding:7px;}");
        const auto label = [](QString text, const char* name = nullptr)
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
        auto* sidebar = new QWidget;
        auto* controls = new QVBoxLayout(sidebar);
        controls->setContentsMargins(0, 0, 8, 0);
        controls->setSpacing(8);
        controls->addWidget(label("AI / Classical\nRun both methods", "title"));
        controls->addWidget(label("One DSM · two independent estimates.\npix_grad · experimental DSM-only model."));
        addButton("Load full Zurich 1 km tile", "loadSample", controls,
                  [this]
                  {
                      loadSample();
                  });
        addButton("Open DSM GeoTIFF...", "openDsm", controls,
                  [this]
                  {
                      openDsm();
                  });
        referenceButton_ = addButton("Open reference (optional)...", "openReference", controls,
                                     [this]
                                     {
                                         openReference();
                                     });
        sourceInfo_ = label("Preparing source data...");
        sourceInfo_->setObjectName("sourceInfo");
        controls->addWidget(sourceInfo_);
        addButton("Download / verify model", "prepareModel", controls,
                  [this]
                  {
                      prepareModel();
                  });
        addButton("Use model ZIP...", "localModel", controls,
                  [this]
                  {
                      const auto path = QFileDialog::getOpenFileName(this, "Choose the pinned pix_grad model ZIP",
                                                                     QString(), "ZIP (*.zip)");
                      if (!path.isEmpty())
                          prepareModel(path);
                  });
        modelInfo_ = label("Model not prepared.");
        modelInfo_->setObjectName("modelInfo");
        controls->addWidget(modelInfo_);
        auto* form = new QFormLayout;
        overlap_ = new QSpinBox;
        overlap_->setObjectName("aiOverlap");
        overlap_->setRange(0, 128);
        overlap_->setSingleStep(32);
        overlap_->setValue(64);
        form->addRow("Tile overlap (px)", overlap_);
        interactive_.push_back(overlap_);
        threads_ = new QSpinBox;
        threads_->setObjectName("cpuThreads");
        threads_->setRange(1, 8);
        threads_->setValue(4);
        form->addRow("CPU threads", threads_);
        interactive_.push_back(threads_);
        preset_ = new QComboBox;
        preset_->setObjectName("classicalPreset");
        preset_->addItems({"Balanced", "Remove more objects", "Preserve more relief"});
        form->addRow("Classical preset", preset_);
        interactive_.push_back(preset_);
        controls->addLayout(form);
        run_ = addButton("Run AI + classical", "runComparison", controls,
                         [this]
                         {
                             process();
                         });
        cancel_ = new QPushButton("Cancel");
        cancel_->setObjectName("cancelProcessing");
        controls->addWidget(cancel_);
        connect(cancel_, &QPushButton::clicked, this,
                [this]
                {
                    cancelled_ = true;
                    cancel_->setEnabled(false);
                    phase(progressValue_, "Cancelling; completed comparison retained...");
                });
        state_ = label("Load source and model, then Run.");
        state_->setObjectName("runState");
        controls->addWidget(state_);
        controls->addWidget(label("Compare maps"));
        mode_ = new QComboBox;
        mode_->setObjectName("displayMode");
        mode_->addItems({"Terrain relief", "Removed height: DSM − DTM", "Signed reference errors"});
        controls->addWidget(mode_);
        interactive_.push_back(mode_);
        scope_ = new QComboBox;
        scope_->setObjectName("evaluationScope");
        scope_->addItems({"Full input", "Shared comparison crop"});
        controls->addWidget(scope_);
        interactive_.push_back(scope_);
        controls->addWidget(label(
            "Source DSM remains on the left. Display and evaluation scope never change the full input used for processing."));
        save_ = addButton("Save comparison folder...", "saveResults", controls,
                          [this]
                          {
                              exportResults();
                          });
        probe_ = label("Click a source pixel to inspect both estimates.");
        probe_->setObjectName("pixelProbe");
        probe_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        controls->addWidget(probe_);
        controls->addStretch();
        controls->addWidget(label(
            "Terrain: Federal Office of Topography swisstopo.\nWeights: David Perez · CC BY 4.0 · Zenodo 7973800.\nReference is evaluation-only. No vertical datum conversion."));
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setFixedWidth(320);
        scroll->setWidget(sidebar);
        layout->addWidget(scroll);
        auto* workspace = new QVBoxLayout;
        auto* maps = new QHBoxLayout;
        maps->setSpacing(8);
        const QStringList titles = {"ORIGINAL DSM · shared input", "CLASSICAL DTM · Run to generate",
                                    "AI DTM · Run to generate"};
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
        legend_ = label("Same light and contrast for both methods. No conversion has run yet.");
        workspace->addWidget(legend_);
        scores_ = new QTableWidget(2, 5);
        scores_->setObjectName("metricTable");
        scores_->setHorizontalHeaderLabels({"Method", "Valid pixels", "RMSE (m)", "MAE (m)", "Bias (m)"});
        scores_->verticalHeader()->hide();
        scores_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        scores_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        scores_->setFixedHeight(101);
        workspace->addWidget(scores_);
        summary_ = label("No prepared terrain result is loaded.");
        summary_->setObjectName("comparisonSummary");
        workspace->addWidget(summary_);
        workspace->addWidget(label(
            "Both methods process the complete supplied DSM. The shared crop changes only the view and scores. Experimental model; this development scene is not a held-out benchmark."));
        layout->addLayout(workspace, 1);
        setCentralWidget(page);
        auto* toolbar = addToolBar("Navigation");
        toolbar->setMovable(false);
        toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        const auto action = [&](QString title, QString icon, auto fn)
        {
            auto* a = toolbar->addAction(QIcon(icon), title);
            connect(a, &QAction::triggered, this, fn);
        };
        action("Pan", ":/icons/pan.svg",
               [this]
               {
                   tool(GisViewerTool::Pan);
               });
        action("Zoom box", ":/icons/zoom-box.svg",
               [this]
               {
                   tool(GisViewerTool::ZoomBox);
               });
        action("Zoom in", ":/icons/zoom-in.svg",
               [this]
               {
                   if (views_[0])
                       views_[0]->zoomIn(1.5);
               });
        action("Zoom out", ":/icons/zoom-out.svg",
               [this]
               {
                   if (views_[0])
                       views_[0]->zoomOut(1.5);
               });
        action("Fit comparison", ":/icons/full-extent.svg",
               [this]
               {
                   fit();
               });
        progress_ = new QProgressBar;
        progress_->setFixedWidth(230);
        statusBar()->addPermanentWidget(progress_);
        connect(mode_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this]
                {
                    display();
                });
        connect(scope_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this]
                {
                    updateScores();
                    fit();
                });
        const auto changed = [this]
        {
            if (comparison_)
                state_->setText(
                    "Controls changed. Run to update; displayed maps and scores use the last completed settings.");
        };
        connect(overlap_, qOverload<int>(&QSpinBox::valueChanged), this, changed);
        connect(threads_, qOverload<int>(&QSpinBox::valueChanged), this, changed);
        connect(preset_, qOverload<int>(&QComboBox::currentIndexChanged), this, changed);
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
    ~AiWindow() override
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
        }
        else
            QMainWindow::closeEvent(event);
    }

private:
    template <class F> QPushButton* addButton(QString title, QString name, QVBoxLayout* layout, F fn)
    {
        auto* button = new QPushButton(title);
        button->setObjectName(name);
        layout->addWidget(button);
        interactive_.push_back(button);
        connect(button, &QPushButton::clicked, this, fn);
        return button;
    }
    QString cache() const
    {
        return arguments_.isSet("data-dir") ? QFileInfo(arguments_.value("data-dir")).absoluteFilePath()
                                            : QDir(QCoreApplication::applicationDirPath()).filePath("data");
    }
    QString sessionDirectory()
    {
        const auto path = QDir(temporary_.path()).filePath(QString::number(++generation_));
        require(QDir().mkpath(path), "Cannot create session directory.");
        return path;
    }
    void phase(int value, const QString& text)
    {
        progressValue_ = value;
        QMutexLocker lock(&phaseMutex_);
        phase_ = text;
    }
    void setBusy(bool busy)
    {
        busy_ = busy;
        for (auto* widget : interactive_)
            widget->setEnabled(!busy);
        run_->setEnabled(!busy && bool(scene_) && !modelDirectory_.isEmpty());
        referenceButton_->setEnabled(!busy && bool(scene_));
        save_->setEnabled(!busy && bool(comparison_));
        mode_->setEnabled(!busy && bool(comparison_));
        cancel_->setEnabled(busy);
        progress_->setVisible(busy);
    }
    void begin(std::function<AiOutcome()> fn)
    {
        cancelled_ = false;
        progressValue_ = 0;
        setBusy(true);
        pending_ = std::async(std::launch::async,
                              [fn = std::move(fn)]
                              {
                                  try
                                  {
                                      return fn();
                                  }
                                  catch (const std::exception& e)
                                  {
                                      AiOutcome out;
                                      out.error = QString::fromUtf8(e.what());
                                      return out;
                                  }
                              });
        poll_.start();
    }
    void failure(const QString& text)
    {
        setBusy(false);
        state_->setText(text);
        statusBar()->showMessage(text);
        if (closeQueued_)
            QTimer::singleShot(0, this, &QWidget::close);
        else if (!cancelled_)
            QMessageBox::warning(this, "Unable to complete operation", text);
    }
    void prepareModel(QString localArchive = QString())
    {
        if (busy_)
            return;
        cancelled_ = false;
        setBusy(true);
        try
        {
            if (localArchive.isEmpty())
                localArchive = arguments_.value("model-archive");
            const QUrl url(arguments_.isSet("model-url") ? arguments_.value("model-url")
                                                         : modelAssets_.value("url").toString());
            const auto folder = prepareSample(modelAssets_, cache(), localArchive, url, cancelled_,
                                              [this](int value, const QString& text)
                                              {
                                                  progress_->setValue(value);
                                                  statusBar()->showMessage("Model: " + text);
                                              });
            checkpoint(cancelled_);
            const auto package = AiModelPackage::open(folder);
            require(package.manifest().id == "davpergan-dtm-dsm-gan-pix-grad", "Unexpected model package.");
            modelDirectory_ = folder;
            modelInfo_->setText("pix_grad ready · 256 × 256 tiles\nVerified ONNX · CPU · experimental");
            setBusy(false);
            state_->setText(scene_ ? "Source and model ready. Press Run AI + classical." : "Model ready; load a DSM.");
        }
        catch (const std::exception& e)
        {
            failure(QString::fromUtf8(e.what()) + "\nUse the exact model ZIP or retry its public URL.");
        }
    }
    void loadSample()
    {
        if (busy_)
            return;
        cancelled_ = false;
        setBusy(true);
        try
        {
            const QUrl sampleUrl(arguments_.isSet("sample-url") ? arguments_.value("sample-url")
                                                                : sampleAssets_.value("url").toString());
            const auto aux =
                prepareSample(sampleAssets_, cache(), arguments_.value("sample-archive"), sampleUrl, cancelled_,
                              [this](int v, const QString& t)
                              {
                                  progress_->setValue(v);
                                  statusBar()->showMessage("Projection/sample support: " + t);
                              });
            configureRuntime(aux);
            const QString folder = QDir(cache()).filePath(terrainAssets_.value("cache").toString());
            require(QDir().mkpath(folder), "Cannot create terrain cache.");
            const auto sources = terrainAssets_.value("sources").toObject();
            for (const QString& name : {QString("dsm"), QString("reference")})
            {
                const auto asset = sources.value(name).toObject();
                const QString path = QDir(folder).filePath(asset.value("file").toString());
                if (hashFile(path) != asset.value("sha256").toString().toLatin1())
                    download(QUrl(asset.value("url").toString()), path, cancelled_,
                             [this, name](int v, const QString& t)
                             {
                                 progress_->setValue(v);
                                 statusBar()->showMessage("Full 1 km " + name + ": " + t);
                             });
                checkpoint(cancelled_);
                require(QFileInfo(path).size() == asset.value("bytes").toDouble() &&
                            hashFile(path) == asset.value("sha256").toString().toLatin1(),
                        "Full terrain source failed size/SHA-256 verification.");
            }
            auto manifest = terrainAssets_;
            manifest.insert("standard_full_tile", true);
            setupModel_ = modelDirectory_.isEmpty();
            load(QDir(folder).filePath("dsm.tif"), QDir(folder).filePath("reference.tif"), manifest);
        }
        catch (const std::exception& e)
        {
            failure(QString::fromUtf8(e.what()));
        }
    }
    void openDsm()
    {
        const auto path =
            QFileDialog::getOpenFileName(this, "Open metre-height DSM", QString(), "GeoTIFF (*.tif *.tiff)");
        if (!path.isEmpty())
            load(path, QString(), {});
    }
    void load(QString path, QString reference, QJsonObject manifest)
    {
        const QString directory = sessionDirectory();
        begin(
            [this, path, reference, manifest, directory]
            {
                AiOutcome out;
                out.scene = std::make_shared<Scene>();
                out.scene->manifest = manifest;
                phase(5, "Reading source DSM...");
                out.scene->dsm = readRaster(path, cancelled_);
                if (!reference.isEmpty())
                {
                    out.scene->reference = readRaster(reference, cancelled_);
                    validateReference(*out.scene->dsm, *out.scene->reference);
                }
                if (manifest.value("standard_full_tile").toBool())
                {
                    const auto& d = *out.scene->dsm;
                    const auto e = d.extent();
                    require(d.width == 2000 && d.height == 2000 && d.pixel == .5 && e.xMin() == 2677000 &&
                                e.yMin() == 1251000 && e.xMax() == 2678000 && e.yMax() == 1252000,
                            "Unexpected full Zurich grid.");
                }
                phase(60, "Preparing original DSM display...");
                RasterRuntime runtime;
                auto image = relief(*out.scene->dsm, out.scene->dsm->z, cancelled_);
                out.scene->original = QDir(directory).filePath("original.tif");
                runtime.write(*out.scene->dsm, out.scene->original, nullptr, 1, &image);
                checkpoint(cancelled_);
                out.message = "Source ready. Prepare model and Run.";
                return out;
            });
    }
    void process()
    {
        if (!scene_ || modelDirectory_.isEmpty())
            return;
        const std::array<Parameters, 3> presets = {Parameters{}, Parameters{1, 80, .25, .08, .25, .2},
                                                   Parameters{2, 48, .6, .16, .8, .4}};
        Parameters parameters = presets[preset_->currentIndex()];
        // Keep the same algorithm settings while respecting the working-grid cap.
        const auto& d = *scene_->dsm;
        while (qint64(std::ceil(d.width / std::max(1., std::round(parameters.working / d.pixel)))) *
                   qint64(std::ceil(d.height / std::max(1., std::round(parameters.working / d.pixel)))) >
               262144)
            parameters.working += d.pixel;
        const auto scene = scene_;
        const QString model = modelDirectory_, directory = sessionDirectory();
        const int overlap = overlap_->value(), threads = threads_->value();
        begin(
            [this, scene, model, directory, parameters, overlap, threads]
            {
                AiOutcome out;
                out.scene = scene;
                out.comparison =
                    compare(*scene, model, parameters, overlap, threads, directory, modelAssets_, cancelled_,
                            [this](int p, const QString& t)
                            {
                                phase(p, t);
                            });
                out.message = "Both methods completed from the same full DSM.";
                return out;
            });
    }
    void openReference()
    {
        if (!scene_)
            return;
        const auto path = QFileDialog::getOpenFileName(this, "Open aligned evaluation-only reference", QString(),
                                                       "GeoTIFF (*.tif *.tiff)");
        if (path.isEmpty())
            return;
        const auto scene = scene_;
        const auto comparison = comparison_;
        const QString directory = sessionDirectory();
        begin(
            [this, path, scene, comparison, directory]
            {
                AiOutcome out;
                out.scene = std::make_shared<Scene>(*scene);
                phase(5, "Reading evaluation-only reference...");
                out.scene->reference = readRaster(path, cancelled_);
                validateReference(*scene->dsm, *out.scene->reference);
                if (comparison)
                {
                    out.comparison = std::make_shared<Comparison>(*comparison);
                    RasterRuntime runtime;
                    for (int method = 0; method < 2; ++method)
                    {
                        Terrain terrain;
                        terrain.dtm = method ? comparison->ai.dtm : comparison->classical.dtm;
                        const auto image = diagnostic(*scene->dsm, terrain, 2, out.scene->reference.get(), cancelled_);
                        const auto file = QDir(directory).filePath(QString("reference-%1.tif").arg(method));
                        runtime.write(*scene->dsm, file, nullptr, 1, &image);
                        out.comparison->maps[method][2] = file;
                    }
                    // Give the new reference crop its own immutable result file.
                    const auto rect = comparisonWindow(*out.scene);
                    const auto cropped = cutRaster(*scene->dsm, rect);
                    const auto values = cutValues(out.scene->reference->z, scene->dsm->width, rect);
                    const auto file = QDir(directory).filePath("comparison-reference.tif");
                    runtime.write(cropped, file, values.constData(), 6);
                    out.comparison->files.insert("comparison-reference.tif", file);
                    evaluateComparison(*out.scene, *out.comparison, modelAssets_);
                }
                out.message = "Reference updated. Both generated height arrays are unchanged.";
                checkpoint(cancelled_);
                return out;
            });
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
        if (!out.error.isEmpty() || (cancelled_ && bool(out.scene)))
        {
            failure(!out.error.isEmpty() ? out.error : "Cancelled; completed comparison retained.");
            return;
        }
        try
        {
            if (out.scene)
            {
                const bool changed = !scene_ || out.scene->dsm != scene_->dsm;
                scene_ = out.scene;
                comparison_ = out.comparison;
                ensureViews();
                if (changed)
                {
                    replace(*views_[0], scene_->original);
                    clear(*views_[1]);
                    clear(*views_[2]);
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
                sourceInfo_->setText(QString("%1 · %2 × %3\n%4 m cells · %5")
                                         .arg(QFileInfo(scene_->dsm->path).fileName())
                                         .arg(scene_->dsm->width)
                                         .arg(scene_->dsm->height)
                                         .arg(scene_->dsm->pixel)
                                         .arg(scene_->reference ? "aligned reference" : "no reference"));
                if (comparison_)
                {
                    display();
                    updateScores();
                }
                else
                {
                    scores_->clearContents();
                    summary_->setText("No terrain conversion has run for this input.");
                    headings_[1]->setText("CLASSICAL DTM · Run to generate");
                    headings_[2]->setText("AI DTM · Run to generate");
                }
            }
            setBusy(false);
            state_->setText(out.message);
            statusBar()->showMessage(out.message);
            if (closeQueued_)
                QTimer::singleShot(0, this, &QWidget::close);
            else if (setupModel_)
            {
                setupModel_ = false;
                QTimer::singleShot(0, this,
                                   [this]
                                   {
                                       prepareModel();
                                   });
            }
        }
        catch (const std::exception& e)
        {
            failure(QString::fromUtf8(e.what()));
        }
    }
    void ensureViews()
    {
        const auto crs = CoordinateSystemFactory::fromUserInput(scene_->dsm->projection);
        const QStringList names = {"originalMap", "classicalMap", "aiMap"};
        for (int pane = 0; pane < 3; ++pane)
        {
            if (!views_[pane])
            {
                auto* view = views_[pane] = new GisViewer;
                view->setObjectName(names[pane]);
                view->setMinimumSize(250, 270);
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
    void tool(GisViewerTool tool)
    {
        for (auto* view : views_)
            if (view)
                view->setActiveTool(tool);
    }
    void fit()
    {
        if (!scene_ || !views_[0])
            return;
        const auto& d = *scene_->dsm;
        const auto cropped = cutRaster(d, comparisonWindow(*scene_)).extent();
        for (auto* overlay : overlays_)
            if (overlay)
            {
                overlay->area = scope_->currentIndex() ? std::optional<GisExtent>(cropped) : std::nullopt;
                overlay->update();
            }
        const auto extent = scope_->currentIndex() ? cropped : d.extent();
        views_[0]->setViewExtent(extent.inflate(extent.width() * .025, extent.height() * .025));
    }
    void display()
    {
        if (!comparison_ || !views_[1])
            return;
        if (mode_->currentIndex() == 2 && !scene_->reference)
        {
            const QSignalBlocker blocker(mode_);
            mode_->setCurrentIndex(0);
            statusBar()->showMessage("Attach an aligned reference to compare errors.");
        }
        for (int method = 0; method < 2; ++method)
            replace(*views_[method + 1], comparison_->maps[method][mode_->currentIndex()]);
        const QStringList suffix = {"DTM · terrain relief", "REMOVED HEIGHT · DSM − DTM", "ERROR · DTM − reference"};
        headings_[1]->setText("CLASSICAL " + suffix[mode_->currentIndex()]);
        headings_[2]->setText("AI " + suffix[mode_->currentIndex()]);
        const QStringList legends = {
            "Identical relief light and contrast. Left: original DSM. Both estimates are independently computed from this full input.",
            "One shared removed-height scale: white 0 m → red ≥20 m. Display saturation only; numeric heights are not clipped.",
            "One shared signed-error scale: blue ≤−2 m · white 0 · red ≥+2 m. Scores use all common valid pixels in the selected scope."};
        legend_->setText(legends[mode_->currentIndex()]);
    }
    void updateScores()
    {
        if (!comparison_)
            return;
        const auto& values = scope_->currentIndex() ? comparison_->crop : comparison_->full;
        for (int method = 0; method < 2; ++method)
        {
            const auto& score = values[method];
            const QStringList cells = {method ? "AI · pix_grad" : "Classical", QString::number(score.count),
                                       score.count ? QString::number(score.rmse, 'f', 3) : "—",
                                       score.count ? QString::number(score.mae, 'f', 3) : "—",
                                       score.count ? QString::number(score.bias, 'f', 3) : "—"};
            for (int column = 0; column < 5; ++column)
            {
                auto* item = new QTableWidgetItem(cells[column]);
                item->setForeground(QColor(method ? "#8857bd" : "#087e8b"));
                scores_->setItem(method, column, item);
            }
        }
        const auto& r = *comparison_;
        const auto& p = r.classical.parameters;
        const auto roi = comparisonWindow(*scene_);
        summary_->setText(
            QString(
                "Last completed run · %1 AI tiles · overlap %2 px · CPU threads %3 · classical working %4 m / window %5 m\nScores: %6 · crop %7 × %8 cells (%9 × %10 m). Full input was used by both methods. Reference is evaluation-only.")
                .arg(r.ai.tiles)
                .arg(r.overlap)
                .arg(r.threads)
                .arg(r.classical.working)
                .arg(p.window)
                .arg(scope_->currentIndex() ? "shared crop" : "full input")
                .arg(roi.width())
                .arg(roi.height())
                .arg(roi.width() * scene_->dsm->pixel)
                .arg(roi.height() * scene_->dsm->pixel));
    }
    void inspect(QPointF point)
    {
        if (!scene_ || busy_)
            return;
        const auto& d = *scene_->dsm;
        const double col = (point.x() - d.transform[0]) / d.pixel, row = (d.transform[3] - point.y()) / d.pixel;
        if (!std::isfinite(col) || !std::isfinite(row) || col < 0 || row < 0 || col >= d.width || row >= d.height)
            return;
        const int x = int(col), y = int(row), i = y * d.width + x;
        QString text = QString("Source pixel %1 / %2\n").arg(x).arg(y);
        if (!d.valid[i])
            text += "NoData";
        else
        {
            text += QString("DSM %1 m").arg(d.z[i], 0, 'f', 2);
            if (comparison_)
                text += QString("\nClassical %1 m\nAI %2 m")
                            .arg(comparison_->classical.dtm[i], 0, 'f', 2)
                            .arg(comparison_->ai.dtm[i], 0, 'f', 2);
            if (scene_->reference && scene_->reference->valid[i])
                text += QString("\nReference %1 m").arg(scene_->reference->z[i], 0, 'f', 2);
        }
        probe_->setText(text);
        const QPointF centre(d.transform[0] + (x + .5) * d.pixel, d.transform[3] - (y + .5) * d.pixel);
        for (auto* overlay : overlays_)
        {
            overlay->point = centre;
            overlay->update();
        }
    }
    void exportResults()
    {
        if (!comparison_)
            return;
        const auto base = QFileDialog::getExistingDirectory(this, "Choose parent folder for a new result bundle");
        if (base.isEmpty())
            return;
        const auto result = comparison_;
        const QString name = "dsm-dtm-ai-" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-hhmmss") + "-" +
                             QUuid::createUuid().toString(QUuid::Id128).left(6);
        begin(
            [this, result, base, name]
            {
                AiOutcome out;
                QTemporaryDir staging(QDir(base).filePath(".dsm-dtm-export-XXXXXX"));
                require(staging.isValid(), "Cannot create export staging directory.");
                const auto files = result->files;
                int complete = 0;
                for (auto it = files.begin(); it != files.end(); ++it)
                {
                    checkpoint(cancelled_);
                    const QString source = QFileInfo(it.value().toString()).isAbsolute()
                                               ? it.value().toString()
                                               : QDir(result->directory).filePath(it.value().toString());
                    phase(++complete * 90 / files.size(), "Saving " + it.key());
                    atomicCopy(source, QDir(staging.path()).filePath(it.key()), cancelled_);
                }
                auto report = result->report;
                QJsonObject exported;
                for (auto it = files.begin(); it != files.end(); ++it)
                    exported.insert(it.key(), it.key());
                report.insert("output_files", exported);
                writeJson(QDir(staging.path()).filePath("comparison.json"), report);
                checkpoint(cancelled_);
                const auto target = QDir(base).filePath(name);
                require(QDir().rename(staging.path(), target), "Cannot commit result directory.");
                staging.setAutoRemove(false);
                out.message = "Saved complete comparison bundle: " + target;
                return out;
            });
    }

    const QCommandLineParser& arguments_;
    QJsonObject sampleAssets_, modelAssets_, terrainAssets_;
    std::shared_ptr<Scene> scene_;
    std::shared_ptr<Comparison> comparison_;
    QString modelDirectory_;
    std::array<GisViewer*, 3> views_{};
    std::array<ProbeOverlay*, 3> overlays_{};
    std::array<QVBoxLayout*, 3> holders_{};
    std::array<QLabel*, 3> headings_{};
    std::array<std::optional<QPointF>, 3> presses_;
    std::vector<QWidget*> interactive_;
    QSpinBox *overlap_ = nullptr, *threads_ = nullptr;
    QComboBox *preset_ = nullptr, *mode_ = nullptr, *scope_ = nullptr;
    QPushButton *run_ = nullptr, *cancel_ = nullptr, *save_ = nullptr, *referenceButton_ = nullptr;
    QLabel *sourceInfo_ = nullptr, *modelInfo_ = nullptr, *state_ = nullptr, *legend_ = nullptr, *summary_ = nullptr,
           *probe_ = nullptr;
    QTableWidget* scores_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QTimer poll_;
    QTemporaryDir temporary_;
    std::future<AiOutcome> pending_;
    std::atomic_bool cancelled_{false};
    std::atomic_int progressValue_{0};
    QMutex phaseMutex_;
    QString phase_;
    bool busy_ = false, closeQueued_ = false, syncing_ = false, setupModel_ = false;
    int generation_ = 0;
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("DsmToDtmAI");
    app.setApplicationVersion("1.0");
    auto palette = app.palette();
    palette.setColor(QPalette::Window, QColor("#f2f6f8"));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::WindowText, QColor("#233e50"));
    palette.setColor(QPalette::Text, QColor("#233e50"));
    app.setPalette(palette);
    QCommandLineParser parser;
    parser.setApplicationDescription("Live pix_grad AI and classical DSM-to-DTM comparison on one full input.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"data-dir", "Data/model cache directory.", "directory"});
    parser.addOption({"sample-archive", "Pinned Zurich v2 support ZIP.", "zip"});
    parser.addOption({"sample-url", "HTTPS support ZIP mirror; hashes still apply.", "url"});
    parser.addOption({"model-archive", "Pinned pix_grad model ZIP.", "zip"});
    parser.addOption({"model-url", "HTTPS model ZIP mirror; hashes still apply.", "url"});
    parser.process(app);
    try
    {
        AiWindow window(parser);
        window.show();
        return app.exec();
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(nullptr, "DsmToDtmAI", QString::fromUtf8(e.what()));
        return 1;
    }
}
