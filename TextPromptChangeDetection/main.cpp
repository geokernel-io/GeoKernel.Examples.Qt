// Text-prompt change detection — find what changed between dates for any concept SAM 3 can
// segment from text, review the changes and play the timeline. All application C++ is here.
//
//   prompt + dates ─► TextPromptChangeSeries::run (one SAM 3 pass per date, objects tracked)
//                  ─► per-interval changes ─► AiQaReviewWorkflow (auto-accept / review / filter)
//                  ─► timeline: TemporalPlaybackController + TemporalFeatureIndex ─► GeoPackage
#include "CoordinateSystems/CoordinateSystemFactory.h"
#include "GeoKernelAI.h"
#include "GeoKernelAnalysis.h"
#include "ModelDownload.h"
#include "Raster/Tiff/GisLayerTIFF.h"
#include "Serialization/Wkb/GisWkbReader.h"
#include "Viewer/GisViewer.h"
#include <QtWidgets>
#include <atomic>

using namespace GeoKernel::AI;
using namespace GeoKernel::Analysis;
using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Temporal;
using GeoKernel::Core::CoordinateSystems::CoordinateSystemFactory;
using GeoKernel::Formats::Raster::Tiff::GisLayerTIFF;

// Almere / Oosterwold, Beeldmateriaal Nederland / PDOK annual orthophotos (CC BY 4.0), 0.5 m,
// EPSG:25831. The provider has no 2021 layer. Dates are nominal (1 July of the layer year).
constexpr int Years[] = {2016, 2017, 2018, 2019, 2020, 2022, 2023, 2024, 2025};
constexpr double DemoArea[] = {657885, 5800734, 658205, 5801054};  // 320 m, as in BuildingChangeReview

QString rasterName(int year) { return QString("oosterwold_%1-epsg25831-50cm.tif").arg(year); }
QDateTime nominalDate(int year) { return QDateTime(QDate(year, 7, 1), QTime(0, 0), QTimeZone::UTC); }

struct Engine
{
    QString key;
    std::unique_ptr<AiSamTextSession> session;
};

struct Job
{
    QString error, seriesDirectory;
    std::optional<TextPromptSeriesResult> result;
};

class Window final : public QMainWindow
{
  public:
    GisViewer* viewer = new GisViewer(this);
    QLineEdit* prompt = new QLineEdit("building");
    QDoubleSpinBox* score = new QDoubleSpinBox;
    QComboBox *dates = new QComboBox, *area = new QComboBox, *provider = new QComboBox;
    QPushButton *run = new QPushButton("Find changes"), *cancel = new QPushButton("Cancel"), *play = new QPushButton("▶ Play");
    QSlider* timeline = new QSlider(Qt::Horizontal);
    QLabel *dateLabel = new QLabel("—"), *status = new QLabel;
    QProgressBar* progress = new QProgressBar;
    QTableWidget* changes = new QTableWidget(0, 4);
    QLineEdit* reviewer = new QLineEdit;
    QPushButton *approve = new QPushButton("Approve"), *reject = new QPushButton("Reject"),
                *approvePending = new QPushButton("Approve all pending"), *saveAccepted = new QPushButton("Save accepted changes…"),
                *saveSeries = new QPushButton("Save series GeoPackage…");
    QJsonObject catalog;
    QString seriesDirectory;
    std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<Engine> engine = std::make_shared<Engine>();
    std::optional<TextPromptSeriesResult> series;
    std::vector<std::unique_ptr<AiQaReviewWorkflow>> reviews;
    QStringList seriesRasters;  // raster per result date
    std::unique_ptr<TemporalFeatureIndex> index;
    std::unique_ptr<TemporalPlaybackController> playback;
    QTimer playTimer, busyTimer;
    QElapsedTimer busyClock;  // SAM 3 needs seconds per tile on CPU: show that work goes on
    QString screenshot;  // --screenshot <png>: run the demo area, show 2019, save the window, quit
    QThread* worker = nullptr;
    bool busy = false, closing = false, crsSet = false;
    int shownDate = -1;

    Window()
    {
        resize(1400, 900);
        setWindowTitle("Text-prompt change detection — GeoKernel");
        setWindowIcon(QIcon(":/geokernel.ico"));
        setCentralWidget(viewer);
        viewer->setActiveTool(GisViewerTool::Pan);
        QFile assets(":/change-assets.json");
        Sam3Download::require(assets.open(QIODevice::ReadOnly), "Missing pinned asset catalog");
        catalog = QJsonDocument::fromJson(assets.readAll()).object();

        auto dock = new QDockWidget("Change detection", this);
        auto scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        auto body = new QWidget;
        auto layout = new QVBoxLayout(body);
        scroll->setWidget(body);
        dock->setWidget(scroll);
        dock->setMinimumWidth(420);
        addDockWidget(Qt::LeftDockWidgetArea, dock);

        auto form = new QFormLayout;
        form->addRow("What to look for", prompt);
        score->setRange(0.05, 0.95);
        score->setSingleStep(0.05);
        score->setValue(0.5);
        score->setToolTip("SAM 3 score threshold. Small objects (e.g. solar panels in 0.5 m imagery) need ~0.2.");
        form->addRow("Score threshold", score);
        dates->addItems({"Two dates: 2016 → 2025", "Series: 9 dates 2016 – 2025"});
        dates->setCurrentIndex(1);
        form->addRow("Dates", dates);
        area->addItems({"Demo area (320 m)", "Full dataset (1 km)"});
        form->addRow("Area", area);
        provider->addItem("CPU", int(AiExecutionProvider::CPU));
        provider->addItem("CUDA", int(AiExecutionProvider::CUDA));
        if (AiSession::isProviderAvailable(AiExecutionProvider::CUDA))
            provider->setCurrentIndex(1);
        form->addRow("Provider", provider);
        layout->addLayout(form);
        auto runRow = new QHBoxLayout;
        runRow->addWidget(run);
        runRow->addWidget(cancel);
        layout->addLayout(runRow);
        layout->addWidget(progress);
        status->setWordWrap(true);
        layout->addWidget(status);

        auto timeBox = new QGroupBox("Timeline");
        auto timeLayout = new QGridLayout(timeBox);
        timeline->setEnabled(false);
        timeline->setPageStep(1);
        timeLayout->addWidget(timeline, 0, 0, 1, 2);
        timeLayout->addWidget(play, 1, 0);
        dateLabel->setWordWrap(true);
        timeLayout->addWidget(dateLabel, 1, 1);
        auto legend = new QLabel("<span style='color:#d4a000'>■</span> present &nbsp; <span style='color:#e02020'>■</span> new on this date"
                                 " &nbsp; <span style='color:#2070ff'>□</span> gone since the previous date");
        timeLayout->addWidget(legend, 2, 0, 1, 2);
        layout->addWidget(timeBox);

        auto reviewBox = new QGroupBox("Review the changes of this interval");
        auto reviewLayout = new QVBoxLayout(reviewBox);
        changes->setHorizontalHeaderLabels({"Change", "Score", "Area m²", "QA state"});
        changes->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        changes->setSelectionBehavior(QAbstractItemView::SelectRows);
        changes->setSelectionMode(QAbstractItemView::SingleSelection);
        changes->setEditTriggers(QAbstractItemView::NoEditTriggers);
        changes->setMinimumHeight(220);
        reviewLayout->addWidget(changes);
        auto who = new QFormLayout;
        reviewer->setPlaceholderText("Your name (recorded in the audit trail)");
        who->addRow("Reviewer", reviewer);
        reviewLayout->addLayout(who);
        auto decide = new QHBoxLayout;
        decide->addWidget(approve);
        decide->addWidget(reject);
        decide->addWidget(approvePending);
        reviewLayout->addLayout(decide);
        reviewLayout->addWidget(saveAccepted);
        reviewLayout->addWidget(saveSeries);
        layout->addWidget(reviewBox);
        auto help = new QLabel(
            "Each date is segmented once with SAM 3 and objects are tracked from date to date; an object "
            "missed on one date keeps its identity. Changes scoring 0.75 or more are accepted automatically, "
            "0.5–0.75 wait for review (TextPromptChangeReview::recommendedPolicy, calibrated on buildings).\n\n"
            "The SAM 3 text package (3.1 GB) is downloaded and SHA-256 checked on the first run, the 9-date "
            "series (56 MB) at startup. Set GEOKERNEL_SAM3_TEXT_PACKAGE to use an installed package.\n\n"
            "Imagery: Beeldmateriaal Nederland / PDOK, CC BY 4.0. Model: Meta SAM 3 (SAM License).");
        help->setWordWrap(true);
        layout->addWidget(help);
        layout->addStretch();

        connect(run, &QPushButton::clicked, this, [this] { findChanges(); });
        connect(cancel, &QPushButton::clicked, this, [this] { cancelled->store(true); });
        connect(timeline, &QSlider::valueChanged, this, [this](int value) { showDate(value); });
        connect(play, &QPushButton::clicked, this, [this] { togglePlayback(); });
        connect(&playTimer, &QTimer::timeout, this, [this] { playFrame(); });
        connect(&busyTimer, &QTimer::timeout, this, [this] { statusBar()->showMessage(QString("Working… %1 s").arg(busyClock.elapsed() / 1000)); });
        connect(changes, &QTableWidget::itemSelectionChanged, this, [this] { locateSelected(); });
        connect(approve, &QPushButton::clicked, this, [this] { decideSelected(true); });
        connect(reject, &QPushButton::clicked, this, [this] { decideSelected(false); });
        connect(approvePending, &QPushButton::clicked, this, [this] {
            if (auto* workflow = currentReview())
                workflow->approvePending(reviewer->text().trimmed());
            fillChanges();
        });
        connect(saveAccepted, &QPushButton::clicked, this, [this] { saveAcceptedChanges(); });
        connect(reviewer, &QLineEdit::textChanged, this, [this] { updateButtons(); });
        connect(saveSeries, &QPushButton::clicked, this, [this] { saveSeriesFile(); });
        updateButtons();
        prepareSeries();
    }

    // ---------------------------------------------------------------- background work

    std::function<void(int, const QString&)> reporter()
    {
        return [this](int value, const QString& text) {
            QMetaObject::invokeMethod(
                this, [this, value, text] {
                    progress->setValue(std::clamp(value, 0, 100));
                    status->setText(text);
                },
                Qt::QueuedConnection);
        };
    }

    void launch(std::function<void(Job&)> action, std::function<void(Job&)> apply)
    {
        if (busy)
            return;
        busy = true;
        cancelled->store(false);
        updateButtons();
        busyClock.start();
        busyTimer.start(1000);
        auto job = std::make_shared<Job>();
        worker = QThread::create([job, action] {
            try
            {
                action(*job);
            }
            catch (const std::exception& e)
            {
                job->error = QString::fromUtf8(e.what());
            }
        });
        connect(worker, &QThread::finished, this, [this, job, apply] {
            worker->deleteLater();
            worker = nullptr;
            busy = false;
            busyTimer.stop();
            statusBar()->clearMessage();
            if (!job->error.isEmpty())
                status->setText(cancelled->load() ? "Cancelled. The previous result is kept." : job->error);
            else
                try
                {
                    apply(*job);
                }
                catch (const std::exception& e)
                {
                    status->setText(QString::fromUtf8(e.what()));
                }
            updateButtons();
            if (closing)
                close();
        });
        worker->start();
    }

    static QString cacheRoot()
    {
        return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)).filePath("GeoKernel/examples");
    }

    void prepareSeries()
    {
        auto stop = [flag = cancelled] { return flag->load(); };
        auto pinned = catalog;
        auto report = reporter();
        status->setText("Preparing the 9-date Oosterwold series…");
        launch(
            [=](Job& job) {
                const auto first = Sam3Download::ensure(pinned, "series", cacheRoot(), stop, report);
                job.seriesDirectory = QFileInfo(first).absolutePath();
            },
            [this](Job& job) {
                seriesDirectory = job.seriesDirectory;
                showRasterOnly(Years[0]);
                viewer->setViewExtent({DemoArea[0], DemoArea[1], DemoArea[2], DemoArea[3]});
                progress->setValue(100);
                status->setText("Series ready (2016 shown). Enter what to look for and click Find changes.");
                if (!screenshot.isEmpty())
                    QTimer::singleShot(0, this, [this] { findChanges(); });
            });
    }

    void findChanges()
    {
        if (seriesDirectory.isEmpty() || prompt->text().trimmed().isEmpty())
            return;
        stopPlayback();
        TextPromptSeriesRequest request;
        request.prompt = prompt->text().trimmed();
        request.scoreThreshold = score->value();
        request.matching.minimumArea = 20.0;
        request.maximumGap = 1;
        if (area->currentIndex() == 0)
            request.extent = {DemoArea[0], DemoArea[1], DemoArea[2], DemoArea[3]};
        QStringList rasters;
        for (const int year : Years)
            if (dates->currentIndex() == 1 || year == Years[0] || year == Years[std::size(Years) - 1])
            {
                rasters.append(QDir(seriesDirectory).filePath(rasterName(year)));
                request.dates.append({nominalDate(year), rasters.last()});
            }
        AiSessionOptions options;
        options.provider = AiExecutionProvider(provider->currentData().toInt());
        options.intraOpThreads = std::max(2, QThread::idealThreadCount() / 2);
        auto stop = [flag = cancelled] { return flag->load(); };
        request.isCancellationRequested = stop;
        auto pinned = catalog;
        auto report = reporter();
        auto state = engine;
        launch(
            [=](Job& job) mutable {
                QString manifest = qEnvironmentVariable("GEOKERNEL_SAM3_TEXT_PACKAGE");
                if (!manifest.isEmpty() && QFileInfo(manifest).isDir())
                    manifest = QDir(manifest).filePath("sam3-package.json");
                if (manifest.isEmpty())
                    manifest = Sam3Download::ensure(pinned, "text", cacheRoot(), stop, report);
                const auto key = manifest + '|' + QString::number(int(options.provider));
                if (!state->session || state->key != key)
                {
                    report(0, "Loading SAM 3 (several GB)…");
                    state->session.reset();
                    state->session = std::make_unique<AiSamTextSession>(manifest, options);
                    state->key = key;
                }
                QTemporaryDir work(QDir::temp().filePath("geokernel-change-XXXXXX"));
                request.workingDirectory = work.path();
                job.result = TextPromptChangeSeries::run(request, state->session.get(),
                                                         [report](const AnalysisProgress& p) { report(p.percent, p.message); });
            },
            [this, rasters](Job& job) { applyResult(std::move(*job.result), rasters); });
    }

    void applyResult(TextPromptSeriesResult result, const QStringList& rasters)
    {
        series = std::move(result);
        seriesRasters = rasters;
        reviews.clear();
        for (const auto& step : series->steps)
        {
            auto workflow = std::make_unique<AiQaReviewWorkflow>(TextPromptChangeReview::recommendedPolicy());
            workflow->ingest(TextPromptChangeReview::qaFeatures(step), "text-prompt-change");
            reviews.push_back(std::move(workflow));
        }
        index = std::make_unique<TemporalFeatureIndex>(TextPromptChangeSeries::timeline(*series),
                                                       TemporalIndexOptions{TemporalMode::Interval, "visible_from", "visible_until", false});
        timeline->blockSignals(true);
        timeline->setRange(0, int(series->dates.size()) - 1);
        timeline->setValue(timeline->maximum());
        timeline->blockSignals(false);
        shownDate = -1;
        showDate(timeline->maximum());
        qint64 appeared = 0;
        for (const auto& step : series->steps)
            appeared += step.appeared;
        progress->setValue(100);
        status->setText(QString("%1 objects tracked over %2 dates in %3 s; %4 appeared after %5. Move the timeline or play it.")
                            .arg(series->objects.size())
                            .arg(series->dates.size())
                            .arg(series->elapsedMilliseconds / 1000)
                            .arg(appeared)
                            .arg(series->dates.first().date().year()));
        if (!screenshot.isEmpty())
        {
            reviewer->setText("analyst");
            timeline->setValue(std::min(3, timeline->maximum()));  // 2019 in the series
            if (changes->rowCount() > 0)
                changes->selectRow(0);
            viewer->setViewExtent({DemoArea[0], DemoArea[1], DemoArea[2], DemoArea[3]});
            QTimer::singleShot(2500, this, [this] {
                grab().save(screenshot);
                qInfo().noquote() << "screenshot" << screenshot << "|" << status->text() << "|" << dateLabel->text();
                QApplication::quit();
            });
        }
    }

    // ---------------------------------------------------------------- map

    std::unique_ptr<GisLayerVector> polygonLayer(const QString& name, const QString& fill, int opacity, const QString& line)
    {
        auto layer = GisLayerVector::createInMemory(name, GeoKernel::Core::Shapes::GisShapeType::Polygon);
        layer->setCoordinateSystem(CoordinateSystemFactory::fromUserInput(series->coordinateSystemWkt));
        layer->style().setFillColor(fill);
        layer->style().setFillOpacity(opacity);
        layer->style().setLineColor(line);
        layer->style().setLineWidth(1.6F);
        return layer;
    }

    void showRasterOnly(int year)
    {
        viewer->clearLayers();
        auto raster = std::make_unique<GisLayerTIFF>(QDir(seriesDirectory).filePath(rasterName(year)));
        raster->open();
        raster->setName(QString("Orthophoto %1").arg(year));
        if (!crsSet && raster->coordinateSystem())
        {
            viewer->setCoordinateSystem(CoordinateSystemFactory::fromUserInput(raster->coordinateSystem()->definition()));
            crsSet = true;
        }
        viewer->addLayer(raster);
    }

    void showDate(int date)
    {
        if (!series || date < 0 || date >= series->dates.size() || date == shownDate)
            return;
        shownDate = date;
        const int year = series->dates[date].date().year();
        showRasterOnly(year);
        auto present = polygonLayer("Present", "#ffd000", 40, "#d4a000");
        auto added = polygonLayer("New on this date", "#ff2020", 120, "#ff6060");
        auto gone = polygonLayer("Gone since the previous date", "#2070ff", 0, "#2070ff");
        for (const auto& object : series->objects)
        {
            if (object.firstDate > date || object.lastDate < date)
                continue;
            const int at = object.geometryDate(date);
            if (auto shape = GeoKernel::Core::Serialization::Wkb::GisWkbReader::read(object.wkbByDate[at]))
                (object.firstDate == date && date > 0 ? added : present)->addShape(std::move(shape));
        }
        if (date > 0)
            for (const auto& change : series->steps[date - 1].features)
                if (change.kind == TextPromptChangeKind::Disappeared)
                    if (auto shape = GeoKernel::Core::Serialization::Wkb::GisWkbReader::read(change.wkb))
                        gone->addShape(std::move(shape));
        for (auto* layer : {&present, &added, &gone})
            viewer->addLayer(*layer);
        viewer->invalidateRenderCache(true, true);
        viewer->update();
        const TextPromptChangeResult* step = date > 0 ? &series->steps[date - 1] : nullptr;
        dateLabel->setText(step ? QString("<b>%1</b> · %2 present · %3 new · %4 gone since %5")
                                      .arg(year)
                                      .arg(series->presentAt(date))
                                      .arg(step->appeared)
                                      .arg(step->disappeared)
                                      .arg(series->dates[date - 1].date().year())
                                : QString("<b>%1</b> · %2 present (first date)").arg(year).arg(series->presentAt(date)));
        fillChanges();
    }

    // ---------------------------------------------------------------- timeline playback

    void togglePlayback()
    {
        if (playback)
        {
            stopPlayback();
            return;
        }
        if (!series)
            return;
        playback = std::make_unique<TemporalPlaybackController>(TextPromptChangeSeries::playbackOptions(*series));
        playback->setLayers({"objects"});
        playback->play();
        play->setText("■ Stop");
        playTimer.start(1100);
        playFrame();
    }

    void stopPlayback()
    {
        playTimer.stop();
        playback.reset();
        play->setText("▶ Play");
    }

    void playFrame()
    {
        if (!playback || !series)
            return;
        QElapsedTimer cost;
        cost.start();
        const auto frame = playback->beginFrame();
        const int date = TextPromptChangeSeries::dateIndexAt(*series, frame.range.startUtc);
        timeline->setValue(date);  // draws the date
        // The index answers which objects are visible in the frame window; it matches presentAt.
        const auto visible = index->query(frame.range).rowIndices.size();
        statusBar()->showMessage(QString("Frame %1 · %2 objects visible").arg(frame.range.startUtc.date().toString(Qt::ISODate)).arg(visible));
        playback->submitLayerFrame(frame.generation, "objects", true, cost.elapsed());
        if (playback->snapshot().committedUtc >= series->dates.last())
            stopPlayback();
    }

    // ---------------------------------------------------------------- review

    AiQaReviewWorkflow* currentReview()
    {
        return series && shownDate > 0 ? reviews[shownDate - 1].get() : nullptr;
    }

    QMap<QString, QString> qaStates()
    {
        QMap<QString, QString> states;
        if (auto* workflow = currentReview())
            for (const auto& value : workflow->snapshot()["features"].toArray())
            {
                const auto feature = value.toObject();
                auto state = feature["state"].toString();
                state.replace("pending-review", "pending").replace("rejected-by-threshold", "filtered");
                if (!feature["reviewer"].toString().isEmpty())
                    state += " (" + feature["reviewer"].toString() + ")";
                states.insert(feature["featureId"].toString(), state);
            }
        return states;
    }

    void fillChanges()
    {
        changes->setRowCount(0);
        if (!currentReview())
        {
            updateButtons();
            return;
        }
        const auto states = qaStates();
        const auto& step = series->steps[shownDate - 1];
        for (int i = 0; i < step.features.size(); ++i)
        {
            const auto& change = step.features[i];
            if (change.kind == TextPromptChangeKind::Unchanged)
                continue;
            const auto id = QString("%1-%2").arg(textPromptChangeKindName(change.kind)).arg(i);
            const int row = changes->rowCount();
            changes->insertRow(row);
            auto* kind = new QTableWidgetItem(textPromptChangeKindName(change.kind));
            kind->setData(Qt::UserRole, id);
            kind->setData(Qt::UserRole + 1, i);
            changes->setItem(row, 0, kind);
            changes->setItem(row, 1, new QTableWidgetItem(QString::number(change.score, 'f', 3)));
            changes->setItem(row, 2, new QTableWidgetItem(QString::number(change.area, 'f', 0)));
            changes->setItem(row, 3, new QTableWidgetItem(states.value(id)));
        }
        updateButtons();
    }

    void locateSelected()
    {
        const auto rows = changes->selectionModel()->selectedRows();
        if (rows.isEmpty() || !currentReview())
            return;
        const int feature = changes->item(rows.first().row(), 0)->data(Qt::UserRole + 1).toInt();
        if (auto shape = GeoKernel::Core::Serialization::Wkb::GisWkbReader::read(series->steps[shownDate - 1].features[feature].wkb))
        {
            viewer->setViewExtent(shape->extent().inflate(40, 40));
        }
    }

    void decideSelected(bool approved)
    {
        const auto rows = changes->selectionModel()->selectedRows();
        auto* workflow = currentReview();
        if (rows.isEmpty() || !workflow || reviewer->text().trimmed().isEmpty())
            return;
        const int row = rows.first().row();
        const auto id = changes->item(row, 0)->data(Qt::UserRole).toString();
        try
        {
            if (approved)
                workflow->approve(id, reviewer->text().trimmed());
            else
                workflow->reject(id, reviewer->text().trimmed());
        }
        catch (const std::exception& e)
        {
            status->setText(QString::fromUtf8(e.what()));
        }
        fillChanges();
        if (row + 1 < changes->rowCount())
            changes->selectRow(row + 1);
    }

    void saveAcceptedChanges()
    {
        auto* workflow = currentReview();
        if (!workflow)
            return;
        const auto path = QFileDialog::getSaveFileName(this, "Save accepted changes", QString("accepted-changes-%1.gpkg").arg(series->dates[shownDate].date().year()),
                                                       "GeoPackage (*.gpkg);;GeoJSON (*.geojson)");
        if (path.isEmpty())
            return;
        try
        {
            QFile::remove(path);  // the dialog already confirmed overwriting
            const auto written = TextPromptChangeReview::writeAccepted(*workflow, series->steps[shownDate - 1], path);
            status->setText(QString("%1 accepted changes written to %2 (EPSG:25831, with QA state and reviewer).").arg(written).arg(path));
        }
        catch (const std::exception& e)
        {
            status->setText(QString::fromUtf8(e.what()));
        }
    }

    void saveSeriesFile()
    {
        if (!series)
            return;
        const auto path = QFileDialog::getSaveFileName(this, "Save series", "change-series.gpkg", "GeoPackage (*.gpkg)");
        if (path.isEmpty())
            return;
        try
        {
            QFile::remove(path);
            TextPromptChangeSeries::write(*series, path);
            status->setText(QString("Series written to %1: layer 'objects' (lifespans) and 'changes' (every interval).").arg(path));
        }
        catch (const std::exception& e)
        {
            status->setText(QString::fromUtf8(e.what()));
        }
    }

    void updateButtons()
    {
        const bool ready = !busy && !seriesDirectory.isEmpty();
        run->setEnabled(ready);
        cancel->setEnabled(busy);
        for (QWidget* widget : std::initializer_list<QWidget*>{prompt, score, dates, area, provider})
            widget->setEnabled(!busy);
        timeline->setEnabled(!busy && series.has_value());
        play->setEnabled(!busy && series.has_value());
        const bool reviewing = !busy && currentReview() != nullptr;
        for (QWidget* widget : std::initializer_list<QWidget*>{approve, reject, approvePending})
            widget->setEnabled(reviewing && !reviewer->text().trimmed().isEmpty());
        saveAccepted->setEnabled(reviewing);
        saveSeries->setEnabled(!busy && series.has_value());
        if (!busy && progress->value() == 0)
            progress->setRange(0, 100);
    }

  protected:
    void closeEvent(QCloseEvent* event) override
    {
        if (busy)
        {
            closing = true;
            cancelled->store(true);
            status->setText("Stopping after the current tile…");
            event->ignore();
            return;
        }
        event->accept();
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("TextPromptChangeDetection");
    Window window;
    if (const auto arguments = QApplication::arguments(); arguments.size() == 3 && arguments[1] == "--screenshot")
        window.screenshot = arguments[2];
    window.show();
    return app.exec();
}
