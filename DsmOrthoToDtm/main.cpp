#include "OrthoInference.h"
#include <QSpinBox>
#include <QUuid>

class OrthoWindow final : public QMainWindow
{
public:
    explicit OrthoWindow(const QCommandLineParser& arguments)
        : arguments_(arguments), assets_(readJson(":/sample-assets.json")),
          modelAssets_{readJson(":/pix-model-assets.json"), readJson(":/rgb-model-assets.json")}
    {
        setWindowTitle("DSM + Orthophoto to DTM | Three independent methods — GeoKernel");
        resize(1920, 980);
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
        controls->addWidget(label("DSM + RGB → DTM\nCompare three methods", "title"));
        controls->addWidget(label("Classical · DSM-only AI · DSM+RGB AI.\nEach starts from the same full DSM."));
        addButton("Load Zurich DSM + orthophoto", "loadSample", controls,
                  [this]
                  {
                      loadSample();
                  });
        addButton("Open aligned DSM + RGB...", "openInputs", controls,
                  [this]
                  {
                      openInputs();
                  });
        referenceButton_ = addButton("Open reference (optional)...", "openReference", controls,
                                     [this]
                                     {
                                         openReference();
                                     });
        sourceInfo_ = label("Preparing sample...");
        sourceInfo_->setObjectName("sourceInfo");
        controls->addWidget(sourceInfo_);
        addButton("Download / verify both models", "prepareModels", controls,
                  [this]
                  {
                      prepareModels();
                  });
        addButton("Use pix_grad model ZIP...", "localPixModel", controls,
                  [this]
                  {
                      chooseModel(0);
                  });
        addButton("Use RGB model ZIP...", "localRgbModel", controls,
                  [this]
                  {
                      chooseModel(1);
                  });
        modelInfo_ = label("Models not prepared.");
        modelInfo_->setObjectName("modelInfo");
        controls->addWidget(modelInfo_);
        auto* form = new QFormLayout;
        overlap_ = new QSpinBox;
        overlap_->setObjectName("aiOverlap");
        overlap_->setRange(0, 128);
        overlap_->setSingleStep(32);
        overlap_->setValue(64);
        form->addRow("AI overlap (px)", overlap_);
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
        run_ = addButton("Run all three methods", "runComparison", controls,
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
        state_ = label("Load data and models, then Run.");
        state_->setObjectName("runState");
        controls->addWidget(state_);
        input_ = new QComboBox;
        input_->setObjectName("inputDisplay");
        input_->addItems({"Original DSM relief", "RGB orthophoto"});
        input_->setCurrentIndex(1);
        controls->addWidget(label("Input context"));
        controls->addWidget(input_);
        interactive_.push_back(input_);
        mode_ = new QComboBox;
        mode_->setObjectName("displayMode");
        mode_->addItems({"Terrain relief", "Removed height: DSM − DTM", "Signed reference errors"});
        controls->addWidget(label("Compare estimates"));
        controls->addWidget(mode_);
        interactive_.push_back(mode_);
        scope_ = new QComboBox;
        scope_->setObjectName("evaluationScope");
        scope_->addItems({"Full input", "Shared comparison crop"});
        scope_->setCurrentIndex(1);
        controls->addWidget(scope_);
        interactive_.push_back(scope_);
        save_ = addButton("Save comparison folder...", "saveResults", controls,
                          [this]
                          {
                              exportResults();
                          });
        probe_ = label("Click a source pixel to inspect RGB and all terrain estimates.");
        probe_->setObjectName("pixelProbe");
        probe_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        controls->addWidget(probe_);
        controls->addStretch();
        controls->addWidget(label(
            "Data: Federal Office of Topography swisstopo.\nAI weights: David Perez · CC BY 4.0.\nReference is evaluation-only; no datum conversion."));
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setFixedWidth(320);
        scroll->setWidget(sidebar);
        layout->addWidget(scroll);
        auto* workspace = new QVBoxLayout;
        auto* maps = new QHBoxLayout;
        maps->setSpacing(7);
        const QStringList titles = {"RGB ORTHOPHOTO · input context", "CLASSICAL · DSM only", "PIX_GRAD · DSM only",
                                    "RGB_GRAD_FM · DSM + RGB"};
        for (int pane = 0; pane < 4; ++pane)
        {
            auto* column = new QVBoxLayout;
            headings_[pane] = label(titles[pane], "heading");
            column->addWidget(headings_[pane]);
            holders_[pane] = new QVBoxLayout;
            column->addLayout(holders_[pane], 1);
            maps->addLayout(column, 1);
        }
        workspace->addLayout(maps, 1);
        legend_ = label("Inputs ready first; Run generates all three terrain estimates.");
        workspace->addWidget(legend_);
        scores_ = new QTableWidget(3, 5);
        scores_->setObjectName("metricTable");
        scores_->setHorizontalHeaderLabels({"Method", "Valid pixels", "RMSE (m)", "MAE (m)", "Bias (m)"});
        scores_->verticalHeader()->hide();
        scores_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        scores_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        scores_->setFixedHeight(130);
        workspace->addWidget(scores_);
        summary_ = label("No prepared prediction is loaded.");
        summary_->setObjectName("comparisonSummary");
        workspace->addWidget(summary_);
        workspace->addWidget(label(
            "Different pretrained checkpoints and objectives: differences do not isolate the causal benefit of RGB. Process full input, then evaluate the same crop. Experimental development scene."));
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
        connect(input_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this]
                {
                    showInput();
                });
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
                state_->setText("Controls changed. Run to update; maps and scores retain the last completed settings.");
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
    ~OrthoWindow() override
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
            for (int i = 0; i < 4; ++i)
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
        auto* b = new QPushButton(title);
        b->setObjectName(name);
        layout->addWidget(b);
        interactive_.push_back(b);
        connect(b, &QPushButton::clicked, this, fn);
        return b;
    }
    QString cache() const
    {
        return arguments_.isSet("data-dir") ? QFileInfo(arguments_.value("data-dir")).absoluteFilePath()
                                            : QDir(QCoreApplication::applicationDirPath()).filePath("data");
    }
    QString sessionDirectory()
    {
        const auto p = QDir(temporary_.path()).filePath(QString::number(++generation_));
        require(QDir().mkpath(p), "Cannot create session directory.");
        return p;
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
        for (auto* w : interactive_)
            w->setEnabled(!busy);
        run_->setEnabled(!busy && bool(scene_) && !modelDirectories_[0].isEmpty() && !modelDirectories_[1].isEmpty());
        referenceButton_->setEnabled(!busy && bool(scene_));
        save_->setEnabled(!busy && bool(comparison_));
        mode_->setEnabled(!busy && bool(comparison_));
        cancel_->setEnabled(busy);
        progress_->setVisible(busy);
    }
    void begin(std::function<OrthoOutcome()> fn)
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
                                      OrthoOutcome out;
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
    void chooseModel(int index)
    {
        const auto path = QFileDialog::getOpenFileName(this, "Choose the pinned " + MethodIds[index + 1] + " model ZIP",
                                                       QString(), "ZIP (*.zip)");
        if (!path.isEmpty())
            prepareModels(index, path);
    }
    void prepareModels(int selected = -1, QString local = QString())
    {
        if (busy_)
            return;
        cancelled_ = false;
        setBusy(true);
        try
        {
            std::array<QString, 2> directories;
            for (int model = 0; model < 2; ++model)
            {
                const QString prefix = model ? "rgb" : "pix";
                const QString archive = model == selected ? local : arguments_.value(prefix + "-model-archive");
                const QUrl url(arguments_.isSet(prefix + "-model-url") ? arguments_.value(prefix + "-model-url")
                                                                       : modelAssets_[model].value("url").toString());
                directories[model] = prepareSample(modelAssets_[model], cache(), archive, url, cancelled_,
                                                   [this, model](int p, const QString& text)
                                                   {
                                                       progress_->setValue(p);
                                                       statusBar()->showMessage(MethodIds[model + 1] + ": " + text);
                                                   });
                checkpoint(cancelled_);
                const auto package = AiModelPackage::open(directories[model]);
                require(package.manifest().id == "davpergan-dtm-dsm-gan-" + QString(model ? "rgb-grad-fm" : "pix-grad"),
                        "Unexpected model package.");
            }
            modelDirectories_ = directories;
            modelInfo_->setText("pix_grad + rgb_grad_fm ready\nVerified 256 × 256 tiles · CPU");
            setBusy(false);
            state_->setText(scene_ ? "Inputs and both models ready. Press Run."
                                   : "Models ready; load aligned DSM and RGB.");
        }
        catch (const std::exception& e)
        {
            failure(QString::fromUtf8(e.what()) + "\nUse the pinned model ZIPs or retry the published URLs.");
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
            const QUrl url(arguments_.isSet("sample-url") ? arguments_.value("sample-url")
                                                          : assets_.value("url").toString());
            const auto folder = prepareSample(assets_, cache(), arguments_.value("sample-archive"), url, cancelled_,
                                              [this](int p, const QString& text)
                                              {
                                                  progress_->setValue(p);
                                                  statusBar()->showMessage(text);
                                              });
            checkpoint(cancelled_);
            configureRuntime(folder);
            setupModels_ = modelDirectories_[0].isEmpty() || modelDirectories_[1].isEmpty();
            load(QDir(folder).filePath("dsm.tif"), QDir(folder).filePath("ortho_rgb.tif"),
                 QDir(folder).filePath("reference.tif"), readJson(QDir(folder).filePath("source-manifest.json")));
        }
        catch (const std::exception& e)
        {
            failure(QString::fromUtf8(e.what()));
        }
    }
    void openInputs()
    {
        const auto dsm =
            QFileDialog::getOpenFileName(this, "Open metre-height DSM", QString(), "GeoTIFF (*.tif *.tiff)");
        if (dsm.isEmpty())
            return;
        const auto rgb = QFileDialog::getOpenFileName(this, "Open exactly aligned RGB Byte orthophoto",
                                                      QFileInfo(dsm).absolutePath(), "GeoTIFF (*.tif *.tiff)");
        if (!rgb.isEmpty())
            load(dsm, rgb, QString(), {});
    }
    void load(QString path, QString rgb, QString reference, QJsonObject manifest)
    {
        const auto directory = sessionDirectory();
        begin(
            [this, path, rgb, reference, manifest, directory]
            {
                OrthoOutcome out;
                out.scene = std::make_shared<OrthoScene>();
                out.scene->manifest = manifest;
                phase(5, "Reading source DSM...");
                out.scene->dsm = readRaster(path, cancelled_);
                phase(25, "Checking RGB channels, grid and coverage...");
                out.scene->rgb = readOrtho(*out.scene->dsm, rgb, directory, cancelled_);
                if (!reference.isEmpty())
                {
                    phase(50, "Reading evaluation-only reference...");
                    out.scene->reference = readRaster(reference, cancelled_);
                    validateReference(*out.scene->dsm, *out.scene->reference);
                }
                if (manifest.value("standard_full_tile").toBool())
                {
                    const auto& d = *out.scene->dsm;
                    const auto e = d.extent();
                    const auto crs = CoordinateSystemFactory::fromUserInput(d.projection);
                    const auto expected = CoordinateSystemFactory::fromEpsg(2056);
                    require(crs && expected && CoordinateSystemFactory::areEquivalent(*crs, *expected) &&
                                d.width == 2000 && d.height == 2000 && d.pixel == .5 && e.xMin() == 2677000 &&
                                e.yMin() == 1251000 && e.xMax() == 2678000 && e.yMax() == 1252000,
                            "Unexpected Zurich sample grid.");
                }
                phase(70, "Preparing original DSM relief...");
                RasterRuntime runtime;
                const auto image = relief(*out.scene->dsm, out.scene->dsm->z, cancelled_);
                out.scene->original = QDir(directory).filePath("original.tif");
                runtime.write(*out.scene->dsm, out.scene->original, nullptr, 1, &image);
                checkpoint(cancelled_);
                out.message = "Aligned inputs ready. Prepare both models and Run.";
                return out;
            });
    }
    void process()
    {
        if (!scene_ || modelDirectories_[0].isEmpty() || modelDirectories_[1].isEmpty())
            return;
        const std::array<Parameters, 3> presets = {Parameters{}, Parameters{1, 80, .25, .08, .25, .2},
                                                   Parameters{2, 48, .6, .16, .8, .4}};
        auto parameters = presets[preset_->currentIndex()];
        const auto& d = *scene_->dsm;
        while (qint64(std::ceil(d.width / std::max(1., std::round(parameters.working / d.pixel)))) *
                   qint64(std::ceil(d.height / std::max(1., std::round(parameters.working / d.pixel)))) >
               262144)
            parameters.working += d.pixel;
        const auto scene = scene_;
        const auto models = modelDirectories_;
        const auto directory = sessionDirectory();
        const int overlap = overlap_->value(), threads = threads_->value();
        begin(
            [this, scene, models, directory, parameters, overlap, threads]
            {
                OrthoOutcome out;
                out.scene = scene;
                out.comparison =
                    compareOrtho(*scene, models, parameters, overlap, threads, directory, modelAssets_, cancelled_,
                                 [this](int p, const QString& t)
                                 {
                                     phase(p, t);
                                 });
                out.message = "All three methods completed from the same full source grid.";
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
        const auto directory = sessionDirectory();
        begin(
            [this, path, scene, comparison, directory]
            {
                OrthoOutcome out;
                out.scene = std::make_shared<OrthoScene>(*scene);
                phase(5, "Reading evaluation-only reference...");
                out.scene->reference = readRaster(path, cancelled_);
                validateReference(*scene->dsm, *out.scene->reference);
                if (comparison)
                {
                    out.comparison = std::make_shared<OrthoComparison>(*comparison);
                    RasterRuntime runtime;
                    for (int method = 0; method < 3; ++method)
                    {
                        Terrain terrain;
                        terrain.dtm = comparison->heights(method);
                        const auto image = diagnostic(*scene->dsm, terrain, 2, out.scene->reference.get(), cancelled_);
                        const auto file = QDir(directory).filePath(QString("reference-%1.tif").arg(method));
                        runtime.write(*scene->dsm, file, nullptr, 1, &image);
                        out.comparison->maps[method][2] = file;
                    }
                    const auto rect = comparisonWindow(*out.scene);
                    const auto cropped = cutRaster(*scene->dsm, rect);
                    const auto values = cutValues(out.scene->reference->z, scene->dsm->width, rect);
                    const auto file = QDir(directory).filePath("comparison-reference.tif");
                    runtime.write(cropped, file, values.constData(), 6);
                    out.comparison->files.insert("comparison-reference.tif", file);
                    evaluateOrtho(*out.scene, *out.comparison, modelAssets_);
                }
                checkpoint(cancelled_);
                out.message = "Reference updated; all generated height arrays are unchanged.";
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
            failure(!out.error.isEmpty() ? out.error : "Cancelled; last completed comparison retained.");
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
                    showInput();
                    for (int pane = 1; pane < 4; ++pane)
                        clear(*views_[pane]);
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
                sourceInfo_->setText(
                    QString("%1 + %2\n%3 × %4 · %5 m cells\n%6")
                        .arg(QFileInfo(scene_->dsm->path).fileName(), QFileInfo(scene_->rgb.path).fileName())
                        .arg(scene_->dsm->width)
                        .arg(scene_->dsm->height)
                        .arg(scene_->dsm->pixel)
                        .arg(scene_->reference ? "Aligned reference attached" : "No reference"));
                if (comparison_)
                {
                    display();
                    updateScores();
                }
                else
                {
                    scores_->clearContents();
                    summary_->setText("Aligned inputs loaded. Run to create all three terrain estimates.");
                    headings_[1]->setText("CLASSICAL · Run to generate");
                    headings_[2]->setText("PIX_GRAD · Run to generate");
                    headings_[3]->setText("RGB_GRAD_FM · Run to generate");
                }
            }
            setBusy(false);
            state_->setText(out.message);
            statusBar()->showMessage(out.message);
            if (closeQueued_)
                QTimer::singleShot(0, this, &QWidget::close);
            else if (setupModels_)
            {
                setupModels_ = false;
                QTimer::singleShot(0, this,
                                   [this]
                                   {
                                       prepareModels();
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
        const QStringList names = {"inputMap", "classicalMap", "pixMap", "rgbModelMap"};
        for (int pane = 0; pane < 4; ++pane)
        {
            if (!views_[pane])
            {
                auto* view = views_[pane] = new GisViewer;
                view->setObjectName(names[pane]);
                view->setMinimumSize(230, 260);
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
                                for (int other = 0; other < 4; ++other)
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
                        [this, pane](GisViewerTool, const QPointF& screen, const GisShapePoint&, Qt::MouseButton button,
                                     Qt::KeyboardModifiers)
                        {
                            if (button == Qt::LeftButton)
                                presses_[pane] = screen;
                        });
                connect(view, &GisViewer::mapMouseUp, this,
                        [this, pane](GisViewerTool tool, const QPointF& screen, const GisShapePoint& world,
                                     Qt::MouseButton button, Qt::KeyboardModifiers)
                        {
                            if (button == Qt::LeftButton)
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
    void tool(GisViewerTool value)
    {
        for (auto* view : views_)
            if (view)
                view->setActiveTool(value);
    }
    void showInput()
    {
        if (!scene_ || !views_[0])
            return;
        replace(*views_[0], input_->currentIndex() ? scene_->rgb.path : scene_->original);
        headings_[0]->setText(input_->currentIndex() ? "RGB ORTHOPHOTO · source input" : "ORIGINAL DSM · source input");
    }
    void fit()
    {
        if (!scene_ || !views_[0])
            return;
        const auto cropped = cutRaster(*scene_->dsm, comparisonWindow(*scene_)).extent();
        for (auto* overlay : overlays_)
            if (overlay)
            {
                overlay->area = scope_->currentIndex() ? std::optional<GisExtent>(cropped) : std::nullopt;
                overlay->update();
            }
        const auto extent = scope_->currentIndex() ? cropped : scene_->dsm->extent();
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
            statusBar()->showMessage("Attach an aligned reference to show errors.");
        }
        const QStringList suffix = {"DTM · relief", "REMOVED HEIGHT", "ERROR · DTM − reference"};
        for (int method = 0; method < 3; ++method)
        {
            replace(*views_[method + 1], comparison_->maps[method][mode_->currentIndex()]);
            headings_[method + 1]->setText(MethodIds[method].toUpper() + " " + suffix[mode_->currentIndex()]);
        }
        const QStringList legends = {
            "Same relief light/contrast for all methods; source orthophoto or DSM remains on the left.",
            "Shared removed-height scale: white 0 m → red ≥20 m. Display saturation only; numeric heights are unchanged.",
            "Shared signed-error scale: blue ≤−2 m · white 0 · red ≥+2 m. Scores use identical valid reference cells in the selected scope."};
        legend_->setText(legends[mode_->currentIndex()]);
    }
    void updateScores()
    {
        if (!comparison_)
            return;
        const auto& values = scope_->currentIndex() ? comparison_->crop : comparison_->full;
        const QStringList names = {"Classical · DSM", "pix_grad · DSM", "rgb_grad_fm · DSM + RGB"};
        const QStringList colors = {"#087e8b", "#8857bd", "#ba7020"};
        for (int method = 0; method < 3; ++method)
        {
            const auto& s = values[method];
            const QStringList cells = {
                names[method], QString::number(s.count), s.count ? QString::number(s.rmse, 'f', 3) : "—",
                s.count ? QString::number(s.mae, 'f', 3) : "—", s.count ? QString::number(s.bias, 'f', 3) : "—"};
            for (int column = 0; column < 5; ++column)
            {
                auto* item = new QTableWidgetItem(cells[column]);
                item->setForeground(QColor(colors[method]));
                scores_->setItem(method, column, item);
            }
        }
        const auto& r = *comparison_;
        const auto roi = comparisonWindow(*scene_);
        summary_->setText(
            QString(
                "Last run · pix_grad %1 tiles · rgb_grad_fm %2 tiles · overlap %3 px · CPU threads %4 · classical working %5 m / window %6 m\nEvaluation: %7 · common crop %8 × %9 cells (%10 × %11 m). All methods processed the full input; outside-crop map areas are dimmed.")
                .arg(r.ai[0].tiles)
                .arg(r.ai[1].tiles)
                .arg(r.overlap)
                .arg(r.threads)
                .arg(r.classical.working)
                .arg(r.classical.parameters.window)
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
        const auto color = scene_->rgb.image.pixelColor(x, y);
        QString text = QString("Source pixel %1 / %2\nRGB %3 / %4 / %5\n")
                           .arg(x)
                           .arg(y)
                           .arg(color.red())
                           .arg(color.green())
                           .arg(color.blue());
        if (!d.valid[i])
            text += "DSM NoData";
        else
        {
            text += QString("DSM %1 m").arg(d.z[i], 0, 'f', 2);
            if (comparison_)
                for (int method = 0; method < 3; ++method)
                    text += QString("\n%1 %2 m").arg(MethodIds[method]).arg(comparison_->heights(method)[i], 0, 'f', 2);
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
        const auto base =
            QFileDialog::getExistingDirectory(this, "Choose parent folder for a new complete result bundle");
        if (base.isEmpty())
            return;
        const auto result = comparison_;
        const QString name = "dsm-ortho-dtm-" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-hhmmss") + "-" +
                             QUuid::createUuid().toString(QUuid::Id128).left(6);
        begin(
            [this, result, base, name]
            {
                OrthoOutcome out;
                QTemporaryDir staging(QDir(base).filePath(".dsm-ortho-export-XXXXXX"));
                require(staging.isValid(), "Cannot create export staging directory.");
                const auto files = result->files;
                int done = 0;
                for (auto it = files.begin(); it != files.end(); ++it)
                {
                    checkpoint(cancelled_);
                    const QString source = QFileInfo(it.value().toString()).isAbsolute()
                                               ? it.value().toString()
                                               : QDir(result->directory).filePath(it.value().toString());
                    phase(++done * 90 / files.size(), "Saving " + it.key());
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
                require(QDir().rename(staging.path(), target), "Cannot commit export directory.");
                staging.setAutoRemove(false);
                out.message = "Saved complete comparison bundle: " + target;
                return out;
            });
    }

    const QCommandLineParser& arguments_;
    QJsonObject assets_;
    std::array<QJsonObject, 2> modelAssets_;
    std::shared_ptr<OrthoScene> scene_;
    std::shared_ptr<OrthoComparison> comparison_;
    std::array<QString, 2> modelDirectories_;
    std::array<GisViewer*, 4> views_{};
    std::array<ProbeOverlay*, 4> overlays_{};
    std::array<QVBoxLayout*, 4> holders_{};
    std::array<QLabel*, 4> headings_{};
    std::array<std::optional<QPointF>, 4> presses_;
    std::vector<QWidget*> interactive_;
    QSpinBox *overlap_ = nullptr, *threads_ = nullptr;
    QComboBox *preset_ = nullptr, *mode_ = nullptr, *scope_ = nullptr, *input_ = nullptr;
    QPushButton *run_ = nullptr, *cancel_ = nullptr, *save_ = nullptr, *referenceButton_ = nullptr;
    QLabel *sourceInfo_ = nullptr, *modelInfo_ = nullptr, *state_ = nullptr, *legend_ = nullptr, *summary_ = nullptr,
           *probe_ = nullptr;
    QTableWidget* scores_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QTimer poll_;
    QTemporaryDir temporary_;
    std::future<OrthoOutcome> pending_;
    std::atomic_bool cancelled_{false};
    std::atomic_int progressValue_{0};
    QMutex phaseMutex_;
    QString phase_;
    bool busy_ = false, closeQueued_ = false, syncing_ = false, setupModels_ = false;
    int generation_ = 0;
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("DsmOrthoToDtm");
    app.setApplicationVersion("1.0");
    auto palette = app.palette();
    palette.setColor(QPalette::Window, QColor("#f2f6f8"));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::WindowText, QColor("#233e50"));
    palette.setColor(QPalette::Text, QColor("#233e50"));
    app.setPalette(palette);
    QCommandLineParser parser;
    parser.setApplicationDescription("Compare independent classical, DSM-only AI and DSM+RGB AI terrain estimates.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"data-dir", "Data/model cache root.", "directory"});
    parser.addOption({"sample-archive", "Exact pinned DSM+ortho sample ZIP.", "zip"});
    parser.addOption({"sample-url", "HTTPS sample mirror; pinned hashes still apply.", "url"});
    for (const QString& model : {"pix", "rgb"})
    {
        parser.addOption({model + "-model-archive", "Pinned " + model + " model ZIP.", "zip"});
        parser.addOption({model + "-model-url", "HTTPS " + model + " model mirror; pinned hashes still apply.", "url"});
    }
    parser.process(app);
    try
    {
        OrthoWindow window(parser);
        window.show();
        return app.exec();
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(nullptr, "DsmOrthoToDtm", QString::fromUtf8(e.what()));
        return 1;
    }
}
