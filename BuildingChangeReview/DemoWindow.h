#pragma once
#include "ChangeWorkflow.h"
#include "SampleSupport.h"
#include "Viewer/GisViewer.h"
#include "Raster/Tiff/GisLayerTIFF.h"
#include "Symbology/GisSymbolRenderer.h"
#include <QApplication>
#include <QActionGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QKeySequence>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QThread>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <atomic>
#include <array>

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Core::CoordinateSystems;
using namespace GeoKernel::Core::Symbology;
using namespace GeoKernel::Formats::Raster::Tiff;
using namespace BuildingChange;

class ChangeRenderer final : public GisSymbolRenderer {
    bool beforeDate;
public:
    explicit ChangeRenderer(bool before = false) : beforeDate(before) {}
    GisSymbolRendererType type() const override { return GisSymbolRendererType::RuleBased; }
    bool usesFeatureAttributes() const override { return true; }
    std::unique_ptr<GisSymbolRenderer> clone() const override { return std::make_unique<ChangeRenderer>(beforeDate); }
    GisLayerStyle styleForFeature(const GisSymbolContext& context) const override {
        const auto status = context.attributes.value("review").toString();
        const QString color = status == "rejected" ? "#82909f" :
                              context.attributes.value("kind") == "new" ? "#20bf73" : "#ee554f";
        GisLayerStyle style;
        const bool belongsHere = beforeDate ? context.attributes.value("kind") == "removed" : context.attributes.value("kind") == "new";
        if (!belongsHere) {
            style.setFillOpacity(0); style.setLineColor("transparent"); style.setLineWidth(0);
            return style;
        }
        style.setFillColor(color); style.setLineColor(color);
        style.setFillOpacity(status == "rejected" ? 10 : status == "approved" ? 90 : 35);
        style.setLineWidth(status == "approved" ? 3.f : 2.f);
        return style;
    }
};

class DemoWindow final : public QMainWindow {
    friend class DemoWindowTestAccess;
    GisViewer *beforeView = nullptr, *afterView = nullptr;
    GisLayerVector *beforeOverlay = nullptr, *afterOverlay = nullptr;
    QComboBox *provider = nullptr, *scope = nullptr, *profile = nullptr;
    QDoubleSpinBox *threshold = nullptr, *minimumArea = nullptr, *overlap = nullptr, *tolerance = nullptr,
                   *minimumScore = nullptr, *maximumOtherScore = nullptr;
    QCheckBox* showCandidates = nullptr;
    QPushButton *run = nullptr, *cancelButton = nullptr, *approve = nullptr, *reject = nullptr,
                *pending = nullptr, *save = nullptr, *locate = nullptr;
    QProgressBar* progress = nullptr;
    QLabel *summary = nullptr, *details = nullptr;
    QToolButton* infoButton = nullptr;
    QTableWidget* table = nullptr;
    QLineEdit *reviewer = nullptr, *note = nullptr;
    QAction *editAction = nullptr, *panAction = nullptr, *undoAction = nullptr, *redoAction = nullptr;
    QString beforePath, afterPath, modelPath;
    std::shared_ptr<JobResult> result;
    QJsonArray audit;
    QString previousAuditHash;
    std::shared_ptr<std::atomic_bool> cancellation = std::make_shared<std::atomic_bool>(false);
    bool busy = false, syncing = false, selecting = false;
    GisExtent datasetExtent=datasetRegion();

    bool overlaysReady() const {
        return beforeOverlay && afterOverlay &&
            beforeView->mapLayerIndex(beforeOverlay) >= 0 && afterView->mapLayerIndex(afterOverlay) >= 0;
    }
    void clearCandidateOverlays() {
        QScopedValueRollback<bool> guard(selecting,true);
        beforeView->clearSelectedFeatures(); afterView->clearSelectedFeatures();
        const auto remove = [](GisViewer* view, GisLayerVector*& overlay) {
            const auto* old = overlay; overlay = nullptr;
            const int index = old ? view->mapLayerIndex(old) : -1;
            if (index >= 0) view->removeLayer(index);
        };
        remove(beforeView,beforeOverlay); remove(afterView,afterOverlay);
    }

    QDoubleSpinBox* spin(QFormLayout* form, const QString& title, double low, double high,
                         double value, double step, int decimals = 2) {
        auto* box = new QDoubleSpinBox;
        box->setRange(low,high); box->setDecimals(decimals); box->setSingleStep(step); box->setValue(value);
        form->addRow(title,box); return box;
    }
    void setBusy(bool value, bool inference = false) {
        busy = value;
        for (auto* control : std::array<QWidget*,10>{run,provider,scope,profile,threshold,minimumArea,overlap,tolerance,minimumScore,maximumOtherScore})
            control->setEnabled(!value);
        cancelButton->setEnabled(value && inference);
        cancelButton->setVisible(value && inference);
        editAction->setEnabled(!value && overlaysReady() && showCandidates->isChecked());
        undoAction->setEnabled(!value && overlaysReady() && showCandidates->isChecked());
        redoAction->setEnabled(!value && overlaysReady() && showCandidates->isChecked());
        table->setEnabled(!value); reviewer->setEnabled(!value); note->setEnabled(!value);
        refreshButtons();
    }
    int row() const {
        const int index = table->currentRow();
        return result && index >= 0 && size_t(index) < result->candidates.size() ? index : -1;
    }
    void refreshButtons() {
        const bool selected = !busy && overlaysReady() && row() >= 0;
        for (auto* button : {approve,reject,pending,locate}) button->setEnabled(selected);
        int count = 0;
        if (result) for (const auto& item : result->candidates) count += item.review == "approved";
        save->setEnabled(!busy && overlaysReady() && count > 0);
    }
    void refreshMaps() {
        beforeView->invalidateRenderCache(true,true); beforeView->refreshLayers();
        afterView->invalidateRenderCache(true,true); afterView->refreshLayers();
    }
    void setDetails(const QString& text, bool error = false) {
        details->setText(text);
        if (error) infoButton->setChecked(true);
    }
    void applyOverlayVisibility() {
        for (auto* view : {beforeView,afterView}) {
            auto* layer = view == beforeView ? beforeOverlay : afterOverlay;
            const int index = layer ? view->mapLayerIndex(layer) : -1;
            if (index >= 0) view->setLayerVisible(index,showCandidates->isChecked());
            if (!showCandidates->isChecked()) view->clearSelectedFeatures();
        }
        if (!showCandidates->isChecked()) {
            panAction->trigger();
        }
        setBusy(busy,busy && cancelButton->isEnabled());
        refreshMaps();
    }
    void refreshTable() {
        if (!result) return;
        int approvedCount = 0, rejectedCount = 0, newCount = 0;
        for (size_t index = 0; index < result->candidates.size(); ++index) {
            const auto& item = result->candidates[index];
            const QStringList values{item.id,item.kind,QString::number(item.areaM2,'f',1),item.review};
            for (int column = 0; column < values.size(); ++column) {
                auto* cell = table->item(int(index),column);
                if (!cell) { cell = new QTableWidgetItem; table->setItem(int(index),column,cell); }
                cell->setText(values[column]);
                cell->setToolTip(QString("Mean building score: %1; other date: %2\nOther-date buffered coverage: %3%\nRaw scores are not change probabilities.\n%4")
                    .arg(item.buildingScore,0,'f',3).arg(item.otherScore,0,'f',3).arg(item.overlap*100,0,'f',1).arg(item.note));
            }
            approvedCount += item.review == "approved"; rejectedCount += item.review == "rejected";
            newCount += item.kind == "new";
        }
        summary->setText(QString("%1 new · %2 removed\n%3 approved · %4 rejected · %5 pending")
            .arg(newCount).arg(result->candidates.size()-newCount).arg(approvedCount).arg(rejectedCount)
            .arg(result->candidates.size()-approvedCount-rejectedCount));
        if (result->candidates.empty()) summary->setText("No candidates passed the filters.");
        refreshButtons();
    }
    void appendAudit(const Candidate& item, const QString& action) {
        QJsonObject event{{"sequence",audit.size()+1},
            {"recorded_at_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"candidate_id",item.id},{"action",action},{"actor",reviewer->text().trimmed()},
            {"note",item.note},{"geometry_sha256",QString(QCryptographicHash::hash(item.geometry->toWkb(),QCryptographicHash::Sha256).toHex())},
            {"previous_hash",previousAuditHash}};
        previousAuditHash = QCryptographicHash::hash(QJsonDocument(event).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex();
        event["hash"] = previousAuditHash; audit.append(event);
    }
    void updateAttributes(int index) {
        auto& item = result->candidates[size_t(index)];
        for (auto* layer : {beforeOverlay,afterOverlay}) {
            if (auto* shape = layer ? layer->getShape(index+1) : nullptr) {
                shape->attributes()["review"] = item.review;
                shape->attributes()["kind"] = item.kind;
            }
        }
    }
    // A changed geometry always loses its earlier approval, including undo/redo.
    void captureEdits() {
        if (busy || !result || !overlaysReady()) return;
        bool changed = false;
        for (size_t index = 0; index < result->candidates.size(); ++index) {
            auto& item = result->candidates[index];
            auto* current = afterOverlay->getShape(int(index)+1);
            if (!current || !BuildingChange::updateGeometry(item,*current)) continue;
            beforeView->clearSelectedFeatures();
            beforeOverlay->setShapeGeometryEdit(int(index)+1,item.geometry->clone());
            updateAttributes(int(index)); appendAudit(item,"geometry-edited"); changed = true;
        }
        if (changed) { refreshTable(); refreshMaps(); }
    }
    void decide(const QString& decision) {
        captureEdits();
        const int index = row(); if (index < 0 || busy) return;
        if (reviewer->text().trimmed().isEmpty()) {
            QMessageBox::warning(this,"Review","Enter a reviewer name before recording a decision."); return;
        }
        auto& item = result->candidates[size_t(index)];
        try {
            reviewCandidate(item,decision,reviewer->text(),note->text());
            updateAttributes(index); appendAudit(item,decision); refreshTable(); refreshMaps();
            statusBar()->showMessage(item.id + ": " + decision);
        } catch (const std::exception& error) { QMessageBox::warning(this,"Review",error.what()); }
    }
    void selectCandidate(int index, bool zoom) {
        if (busy || selecting || !result || !overlaysReady() || index < 0 || size_t(index) >= result->candidates.size()) return;
        if (!beforeOverlay->getShape(index+1) || !afterOverlay->getShape(index+1)) return;
        QScopedValueRollback<bool> guard(selecting,true);
        if (table->currentRow() != index) table->selectRow(index);
        note->setText(result->candidates[size_t(index)].note);
        const auto selectedExtent = result->candidates[size_t(index)].geometry->extent();
        for (auto* view : {beforeView,afterView}) {
            if (!showCandidates->isChecked()) break;
            auto* layer = view == beforeView ? beforeOverlay : afterOverlay;
            FeatureSources::FeatureHitTestResult hit;
            hit.layer = layer; hit.shape = layer->getShape(index+1); hit.featureId = index+1; hit.layerIndex = view->mapLayerIndex(layer);
            view->setSelectedFeature(hit);
        }
        if (zoom) {
            const auto extent = selectedExtent.inflate(std::max(10.,selectedExtent.width()*.5),std::max(10.,selectedExtent.height()*.5));
            beforeView->setViewExtent(extent); afterView->setViewExtent(extent);
        }
        refreshButtons();
    }
    void installResults() {
        const auto extent = beforeView->viewExtent();
        std::array<std::unique_ptr<GisLayerVector>,2> layers;
        for (int side=0;side<2;++side) {
            if (result->candidates.empty()) break;
            auto& layer=layers[side];
            layer = GisLayerVector::createInMemory("Building change candidates",GisShapeType::Polygon);
            layer->setCoordinateSystem(CoordinateSystemFactory::fromEpsg(25831));
            layer->addAttributeDefinition({"candidate_id",GisAttributeType::String,64,0});
            layer->addAttributeDefinition({"kind",GisAttributeType::String,16,0});
            layer->addAttributeDefinition({"review",GisAttributeType::String,16,0});
            layer->setSymbolRenderer(std::make_unique<ChangeRenderer>(side == 0));
            for (const auto& candidate : result->candidates) {
                if (!candidate.geometry || candidate.geometry->isEmpty())
                    throw std::runtime_error("Candidate geometry is missing or empty.");
                auto shape = candidate.geometry->clone();
                shape->attributes() = {{"candidate_id",candidate.id},{"kind",candidate.kind},{"review",candidate.review}};
                if (!layer->addShape(std::move(shape))) throw std::runtime_error("Could not add candidate polygon.");
            }
        }
        clearCandidateOverlays();
        for (int side=0;side<2;++side) {
            if (!layers[side]) continue;
            auto* view = side == 0 ? beforeView : afterView;
            auto* raw = layers[side].get(); view->addLayer(layers[side]);
            const int index = view->mapLayerIndex(raw);
            if (index < 0) throw std::runtime_error("Candidate layer was not accepted by the viewer.");
            if (side == 0) beforeOverlay = raw; else afterOverlay = raw;
            if (!view->beginEditLayer(index) || !view->setActiveEditLayerIndex(index))
                throw std::runtime_error("Could not start the candidate edit session.");
        }
        beforeView->setViewExtent(extent); afterView->setViewExtent(extent);
        table->setRowCount(int(result->candidates.size())); refreshTable();
        applyOverlayVisibility();
        if (!result->candidates.empty()) selectCandidate(0,false);
        setDetails(QString("%8: %1 instances · %9: %2 instances\nElapsed: %3 s · provider: %4\n"
            "Excluded: %5 edge, %6 invalid, %7 matched/uncertain instances.\nNo reference labels; candidate counts are not accuracy.")
            .arg(result->beforeInstances).arg(result->afterInstances).arg(result->elapsedMilliseconds/1000.,0,'f',1)
            .arg(result->provenance["execution_provider"].toString()).arg(result->edgeInstances).arg(result->invalidPolygons).arg(result->uncertainInstances)
            .arg(Assets::BeforeYear).arg(Assets::AfterYear));
        refreshMaps();
    }
    void finishAnalysis(const std::shared_ptr<JobResult>& job) {
        if (cancellation->load() || job->cancelled) {
            progress->setValue(0); progress->setFormat("Cancelled");
            statusBar()->showMessage("Analysis cancelled. Previous completed results, if any, are retained.");
        } else if (!job->error.isEmpty()) {
            progress->setValue(0); progress->setFormat("Analysis failed");
            setDetails(job->error,true); statusBar()->showMessage("Analysis failed. Previous completed results, if any, are retained.");
        } else {
            result = job; audit = {}; previousAuditHash.clear();
            try {
                installResults(); progress->setValue(100); progress->setFormat("Complete");
                statusBar()->showMessage("Analysis complete. Select a candidate to review.");
            } catch (const std::exception& error) {
                clearCandidateOverlays(); result.reset(); table->setRowCount(0);
                summary->setText("Candidate display failed. Run analysis to retry.");
                setDetails(error.what(),true); progress->setValue(0); progress->setFormat("Display failed");
                statusBar()->showMessage("Could not display candidate results. Source imagery is retained.");
            }
        }
        setBusy(false);
        if (result && !result->candidates.empty() && overlaysReady()) selectCandidate(0,false);
    }
    void prepare() {
        if (busy) return;
        setBusy(true); progress->setRange(0,0); statusBar()->showMessage("Preparing verified sample data...");
        try {
            QString first;
            for (int attempt = 0; attempt < 2; ++attempt) {
                first = ensureSampleFile(QUrl(Assets::DatasetUrl),Assets::DatasetArchive,Assets::DatasetCache,
                                         Assets::BeforeFile,this,Assets::DatasetSha256);
                if (first.isEmpty()) throw std::runtime_error("Sample download interrupted. Click Run to retry.");
                beforePath = first; afterPath = findFile(QFileInfo(first).absolutePath(),Assets::AfterFile);
                const auto valid = [](const QString& path, const QByteArray& expected) {
                    QFile file(path); QCryptographicHash hash(QCryptographicHash::Sha256);
                    return file.open(QIODevice::ReadOnly) && hash.addData(&file) && hash.result().toHex() == expected;
                };
                if (valid(beforePath,Assets::BeforeSha256) && valid(afterPath,Assets::AfterSha256)) break;
                QFile::remove(QDir(sampleDataPath()).filePath(QString(Assets::DatasetCache)+"/.archive-sha256"));
                first.clear();
            }
            if (first.isEmpty()) throw std::runtime_error("Sample rasters failed their individual SHA-256 checks.");
            if (beforeView->layerCount() == 0) {
                for (auto* view : {beforeView,afterView}) {
                    auto layer = std::make_unique<GisLayerTIFF>(view == beforeView ? beforePath : afterPath);
                    layer->setName(QString("PDOK %1").arg(view == beforeView ? Assets::BeforeYear : Assets::AfterYear)); layer->open();
                    view->setCoordinateSystem(CoordinateSystemFactory::fromEpsg(25831)); view->addLayer(layer);
                    view->setViewExtent(demoRegion());
                }
            }
            statusBar()->showMessage("Preparing the HOTOSM building model...");
            const auto manifest = ensureSampleFile(QUrl(Assets::ModelUrl),Assets::ModelArchive,"hotosm-model",
                                                   "geokernel-model.json",this,Assets::ModelSha256);
            if (manifest.isEmpty()) throw std::runtime_error("Model preparation interrupted. Click Run to retry.");
            modelPath = QFileInfo(manifest).absolutePath();
            statusBar()->showMessage("Ready. Pan/zoom to choose an area, then run the two-date analysis.");
            progress->setRange(0,100); progress->setValue(0); progress->setFormat("Ready");
        } catch (const std::exception& error) {
            statusBar()->showMessage(QString::fromUtf8(error.what()));
            setDetails(QString::fromUtf8(error.what()),true);
            progress->setRange(0,100); progress->setValue(0); progress->setFormat("Not ready");
        }
        setBusy(false);
    }
    void startAnalysis() {
        if (busy) return;
        if (modelPath.isEmpty() || beforePath.isEmpty() || afterPath.isEmpty()) prepare();
        if (modelPath.isEmpty() || beforePath.isEmpty() || afterPath.isEmpty()) return;
        captureEdits();
        if (result && !result->candidates.empty() && QMessageBox::question(this,"Run analysis",
            "A completed new run replaces the current session's candidates and review decisions. Continue?") != QMessageBox::Yes) return;
        JobOptions options;
        options.before = beforePath; options.after = afterPath; options.model = modelPath;
        options.region = scope->currentIndex() == 0 ? demoRegion() : scope->currentIndex() == 1 ? beforeView->viewExtent() : datasetExtent;
        options.foregroundThreshold = threshold->value();
        options.provider = static_cast<GeoKernel::AI::AiExecutionProvider>(provider->currentData().toInt());
        options.matching = {minimumArea->value(),overlap->value(),tolerance->value(),minimumScore->value(),maximumOtherScore->value(),.25};
        const double x0=std::max(options.region.xMin(),datasetExtent.xMin());
        const double y0=std::max(options.region.yMin(),datasetExtent.yMin());
        const double x1=std::min(options.region.xMax(),datasetExtent.xMax());
        const double y1=std::min(options.region.yMax(),datasetExtent.yMax());
        if (x1<=x0 || y1<=y0) { QMessageBox::warning(this,"Analysis area","The view does not intersect the imagery."); return; }
        options.region = GisExtent(x0,y0,x1,y1);
        cancellation->store(false); setBusy(true,true);
        panAction->setChecked(true); beforeView->setActiveTool(GisViewerTool::Pan); afterView->setActiveTool(GisViewerTool::Pan);
        progress->setValue(0); progress->setFormat("Starting...");
        statusBar()->showMessage("Analysing both dates. Candidate results will appear after completion.");
        const auto job = std::make_shared<JobResult>(); const auto cancel = cancellation;
        auto* worker = QThread::create([this,options,job,cancel] {
            try {
                runAnalysis(options,*job,[cancel] { return cancel->load(); },[this](int value,const QString& message) {
                    QMetaObject::invokeMethod(this,[this,value,message] {
                        progress->setValue(value); progress->setFormat("%p%");
                        progress->setToolTip(message); statusBar()->showMessage(message);
                    },Qt::QueuedConnection);
                });
            } catch (const std::exception& error) {
                if (cancel->load()) job->cancelled = true; else job->error = QString::fromUtf8(error.what());
            }
        });
        connect(worker,&QThread::finished,this,[this,worker,job] {
            worker->deleteLater();
            finishAnalysis(job);
        });
        worker->start();
    }
    void exportApproved() {
        captureEdits();
        if (busy || !result) return;
        try {
            const auto collection = approvedCollection(*result,audit);
            const auto features = collection["features"].toArray();
            if (features.isEmpty()) return;
            const auto path = QFileDialog::getSaveFileName(this,"Save approved building changes","building_changes.geojson","GeoJSON (*.geojson)");
            if (path.isEmpty()) return;
            const QString output = path.endsWith(".geojson",Qt::CaseInsensitive) ? path : path+".geojson";
            const auto bytes = QJsonDocument(collection).toJson(QJsonDocument::Indented);
            QSaveFile file(output);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
                throw std::runtime_error("Could not save the approved change dataset.");
            statusBar()->showMessage(QString("Saved %1 approved polygons: %2").arg(features.size()).arg(QDir::toNativeSeparators(output)));
        } catch (const std::exception& error) { QMessageBox::critical(this,"Export",error.what()); }
    }
protected:
    void closeEvent(QCloseEvent* event) override {
        if (busy) {
            cancellation->store(true); statusBar()->showMessage("Cancelling; wait for work to finish before closing."); event->ignore();
        } else QMainWindow::closeEvent(event);
    }
public:
    explicit DemoWindow(bool prepareAssets = true) {
        resize(1460,860); setWindowTitle("BuildingChangeReview"); setWindowIcon(QIcon(":/icons/geokernel.ico"));
        auto* splitter = new QSplitter(this); setCentralWidget(splitter);
        for (int i=0;i<2;++i) {
            auto* pane = new QWidget; auto* layout = new QVBoxLayout(pane); layout->setContentsMargins(0,0,0,0);
            auto* label = new QLabel(i == 0 ? QString("%1 — BEFORE / REMOVAL CANDIDATES").arg(Assets::BeforeYear) : QString("%1 — AFTER / ADDITION CANDIDATES").arg(Assets::AfterYear));
            label->setAlignment(Qt::AlignCenter); label->setMargin(6); layout->addWidget(label);
            auto* view = new GisViewer(pane); view->setActiveTool(GisViewerTool::Pan); layout->addWidget(view,1);
            if (!i) beforeView=view; else afterView=view; splitter->addWidget(pane);
        }
        splitter->setChildrenCollapsible(false);
        connect(beforeView,&GisViewer::visibleExtentChanged,this,[this](const GisExtent& extent) {
            if (!syncing) { syncing=true; afterView->setViewExtent(extent); syncing=false; }
        });
        connect(afterView,&GisViewer::visibleExtentChanged,this,[this](const GisExtent& extent) {
            if (!syncing) { syncing=true; beforeView->setViewExtent(extent); syncing=false; }
        });
        auto* toolbar = addToolBar("Map navigation"); toolbar->setMovable(false); toolbar->setIconSize(QSize(32,32));
        auto* zoomIn = toolbar->addAction(QIcon(":/icons/zoom-in.svg"),"Zoom In");
        auto* zoomOut = toolbar->addAction(QIcon(":/icons/zoom-out.svg"),"Zoom Out");
        auto* full = toolbar->addAction(QIcon(":/icons/full-extent.svg"),"Full Extent");
        connect(zoomIn,&QAction::triggered,beforeView,[this]{beforeView->zoomIn();});
        connect(zoomOut,&QAction::triggered,beforeView,[this]{beforeView->zoomOut();});
        connect(full,&QAction::triggered,beforeView,[this]{beforeView->setViewExtent(datasetExtent);});
        auto* demoArea=toolbar->addAction("Demo area");
        connect(demoArea,&QAction::triggered,beforeView,[this]{beforeView->setViewExtent(demoRegion());});
        auto* tools = new QActionGroup(this); tools->setExclusive(true);
        panAction=toolbar->addAction(QIcon(":/icons/pan.svg"),"Pan");
        auto* box=toolbar->addAction(QIcon(":/icons/zoom-box.svg"),"Zoom Rect");
        editAction=toolbar->addAction(QString("Edit Vertices (%1)").arg(Assets::AfterYear));
        editAction->setShortcut(QKeySequence("Ctrl+E"));
        for (auto* action : {panAction,box,editAction}) { action->setCheckable(true); tools->addAction(action); }
        panAction->setChecked(true);
        connect(panAction,&QAction::triggered,this,[this]{beforeView->setActiveTool(GisViewerTool::Pan);afterView->setActiveTool(GisViewerTool::Pan);});
        connect(box,&QAction::triggered,this,[this]{beforeView->setActiveTool(GisViewerTool::ZoomBox);afterView->setActiveTool(GisViewerTool::ZoomBox);});
        connect(editAction,&QAction::triggered,this,[this]{beforeView->setActiveTool(GisViewerTool::Pan);afterView->setActiveTool(GisViewerTool::EditVertices);});
        toolbar->addSeparator(); undoAction=toolbar->addAction("Undo edit"); redoAction=toolbar->addAction("Redo edit");
        connect(undoAction,&QAction::triggered,this,[this]{const int index=afterOverlay?afterView->mapLayerIndex(afterOverlay):-1;if(index>=0){afterView->undoEditLayer(index);captureEdits();}});
        connect(redoAction,&QAction::triggered,this,[this]{const int index=afterOverlay?afterView->mapLayerIndex(afterOverlay):-1;if(index>=0){afterView->redoEditLayer(index);captureEdits();}});
        auto* panel = new QWidget; auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(10,10,10,10);layout->setSpacing(8);
        auto* title = new QLabel(QString("<b>Oosterwold</b> · %1 → %2").arg(Assets::BeforeYear).arg(Assets::AfterYear)); layout->addWidget(title);
        auto* form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        scope=new QComboBox; scope->addItems({"Demo area (fixed)","Current view (both dates)","Full dataset"}); form->addRow("Area",scope);
        profile=new QComboBox; profile->addItems({"Conservative","Balanced","Custom"}); form->addRow("Filter",profile);
        layout->addLayout(form);
        auto* settingsToggle=new QToolButton;settingsToggle->setText("Settings");settingsToggle->setCheckable(true);
        settingsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);settingsToggle->setArrowType(Qt::RightArrow);
        settingsToggle->setAutoRaise(true);settingsToggle->setShortcut(QKeySequence("Alt+S"));
        settingsToggle->setToolTip("Analysis parameters (Alt+S)");layout->addWidget(settingsToggle);
        auto* advanced=new QWidget;auto* advancedForm=new QFormLayout(advanced);advancedForm->setContentsMargins(0,0,0,0);
        advancedForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);advancedForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
        provider = new QComboBox;
        using GeoKernel::AI::AiExecutionProvider;
        for (auto choice : {AiExecutionProvider::Auto,AiExecutionProvider::CPU,AiExecutionProvider::CUDA,AiExecutionProvider::DirectML})
            provider->addItem(GeoKernel::AI::aiExecutionProviderName(choice),int(choice));
        advancedForm->addRow("Provider",provider);
        threshold=spin(advancedForm,"Building threshold",.05,.95,.4371,.025,4);
        threshold->setToolTip("Model package default: 0.4371. A higher threshold can miss existing buildings and create false changes.");
        minimumArea=spin(advancedForm,"Min. area (m²)",1,1000,50,5,1);
        tolerance=spin(advancedForm,"Tolerance (m)",0,5,2,.5,1);
        tolerance->setToolTip("Square buffering of weak building evidence (score >= 0.25). Does not register the imagery.");
        overlap=spin(advancedForm,"Other-date coverage <",.01,.95,.10,.05);
        minimumScore=spin(advancedForm,"Own-date mean ≥",0,1,.75,.05);
        maximumOtherScore=spin(advancedForm,"Other-date mean ≤",0,1,.20,.05);
        overlap->setToolTip("Buffered coverage of raw building scores >= 0.25, including buildings missed by instance separation.");
        minimumScore->setToolTip("Raw mean building score within the predicted instance; not a calibrated change probability.");
        maximumOtherScore->setToolTip("Suppress the candidate when the same footprint has substantial building evidence in the other date.");
        layout->addWidget(advanced);advanced->hide();
        connect(settingsToggle,&QToolButton::toggled,this,[advanced,settingsToggle](bool expanded){
            advanced->setVisible(expanded);settingsToggle->setArrowType(expanded?Qt::DownArrow:Qt::RightArrow);
        });
        connect(profile,qOverload<int>(&QComboBox::activated),this,[this](int index){
            if(index>1)return;
            threshold->setValue(.4371);minimumArea->setValue(index==0?50:20);tolerance->setValue(index==0?2:1);
            overlap->setValue(index==0?.10:.20);minimumScore->setValue(index==0?.75:.55);maximumOtherScore->setValue(index==0?.20:.35);
            profile->setCurrentIndex(index);
        });
        for(auto* box:{threshold,minimumArea,tolerance,overlap,minimumScore,maximumOtherScore})
            connect(box,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{profile->setCurrentIndex(2);});
        run=new QPushButton("Run analysis"); cancelButton=new QPushButton("Cancel");
        run->setShortcut(QKeySequence("Ctrl+R")); run->setToolTip("Run analysis (Ctrl+R)");
        auto* runRow=new QHBoxLayout;runRow->addWidget(run,1);runRow->addWidget(cancelButton);layout->addLayout(runRow);
        connect(run,&QPushButton::clicked,this,[this]{startAnalysis();});
        connect(cancelButton,&QPushButton::clicked,this,[this]{cancellation->store(true);cancelButton->setEnabled(false);progress->setFormat("Cancelling...");});
        progress=new QProgressBar; progress->setRange(0,100); progress->setValue(0); progress->setFormat("Ready");
        progress->setFixedHeight(18);progress->setAlignment(Qt::AlignCenter);
        progress->setStyleSheet("QProgressBar { border: 1px solid #cbd0d5; border-radius: 3px; background: #f1f3f5; text-align: center; } QProgressBar::chunk { background: #20bf73; }");
        layout->addWidget(progress);
        summary=new QLabel("Run analysis to find candidates.");summary->setWordWrap(true);layout->addWidget(summary);
        summary->setToolTip("Model predictions require human review before export.");
        table=new QTableWidget(0,4);table->setHorizontalHeaderLabels({"Candidate","Type","m²","Review"});
        table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->verticalHeader()->hide();
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
        table->horizontalHeader()->setStretchLastSection(false);table->verticalHeader()->setDefaultSectionSize(26);
        table->setMinimumHeight(200);layout->addWidget(table,1);
        connect(table,&QTableWidget::itemSelectionChanged,this,[this]{selectCandidate(row(),false);});
        connect(table,&QTableWidget::cellDoubleClicked,this,[this](int index,int){selectCandidate(index,true);});
        auto* reviewForm=new QFormLayout; reviewer=new QLineEdit("demo-reviewer");note=new QLineEdit;
        reviewForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);reviewForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
        reviewer->setMaxLength(100);note->setMaxLength(512);reviewForm->addRow("Reviewer",reviewer);reviewForm->addRow("Note",note);layout->addLayout(reviewForm);
        auto* reviewRow=new QHBoxLayout; approve=new QPushButton("Approve");reject=new QPushButton("Reject");pending=new QPushButton("Pending");
        approve->setShortcut(QKeySequence("Ctrl+Return")); approve->setToolTip("Approve selected candidate (Ctrl+Enter)");
        for(auto* button:{approve,reject,pending})reviewRow->addWidget(button);layout->addLayout(reviewRow);
        locate=new QPushButton("Locate");locate->setToolTip("Zoom to the selected candidate on both maps; double-clicking a table row does the same.");
        connect(approve,&QPushButton::clicked,this,[this]{decide("approved");});
        connect(reject,&QPushButton::clicked,this,[this]{decide("rejected");});
        connect(pending,&QPushButton::clicked,this,[this]{decide("pending");});
        connect(locate,&QPushButton::clicked,this,[this]{selectCandidate(row(),true);});
        save=new QPushButton("Export GeoJSON");save->setToolTip("Export approved candidates only.");
        auto* exportRow=new QHBoxLayout;exportRow->addWidget(locate);exportRow->addWidget(save,1);layout->addLayout(exportRow);
        connect(save,&QPushButton::clicked,this,[this]{exportApproved();});
        save->setShortcut(QKeySequence::Save);
        showCandidates=new QCheckBox("Overlays");showCandidates->setChecked(true);
        showCandidates->setToolTip("Show candidate polygons. Uncheck to inspect the imagery.");
        auto* legend=new QLabel("<font color='#20bf73'>● New</font> &nbsp; <font color='#ee554f'>● Removed</font> &nbsp; <font color='#ef9b00'>● Selected</font>");
        legend->setToolTip(QString("Green: addition candidates in %1. Red: removal candidates in %2. Orange: selected location on both dates. Stronger fill: approved; gray: rejected. Geometry edits require approval again.").arg(Assets::AfterYear).arg(Assets::BeforeYear));
        auto* overlayRow=new QHBoxLayout;overlayRow->addWidget(showCandidates);overlayRow->addStretch();overlayRow->addWidget(legend);layout->addLayout(overlayRow);
        connect(showCandidates,&QCheckBox::toggled,this,[this]{applyOverlayVisibility();});
        auto* credits=new QLabel("<a href='https://www.beeldmateriaal.nl/dataroom'>Beeldmateriaal / PDOK</a> · <a href='https://creativecommons.org/licenses/by/4.0/'>CC BY 4.0</a>");
        credits->setOpenExternalLinks(true);credits->setToolTip(Assets::Attribution);
        infoButton=new QToolButton;infoButton->setText("Details");infoButton->setCheckable(true);infoButton->setAutoRaise(true);
        infoButton->setShortcut(QKeySequence("Alt+D"));infoButton->setToolTip("Run details, source and model information (Alt+D)");
        auto* footer=new QHBoxLayout;footer->addWidget(credits);footer->addStretch();footer->addWidget(infoButton);layout->addLayout(footer);
        auto* infoPanel=new QWidget;auto* infoLayout=new QVBoxLayout(infoPanel);infoLayout->setContentsMargins(0,0,0,0);
        details=new QLabel("Run analysis to see processing details.");details->setWordWrap(true);infoLayout->addWidget(details);
        auto* attribution=new QLabel(Assets::Attribution);attribution->setWordWrap(true);infoLayout->addWidget(attribution);
        layout->addWidget(infoPanel);infoPanel->hide();connect(infoButton,&QToolButton::toggled,infoPanel,&QWidget::setVisible);
        auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);scroll->setWidget(panel);
        auto* dock=new QDockWidget("Change review",this);dock->setMinimumWidth(350);dock->setWidget(scroll);addDockWidget(Qt::RightDockWidgetArea,dock);
        resizeDocks({dock},{390},Qt::Horizontal);
        auto* watcher=new QTimer(this);watcher->setInterval(250);connect(watcher,&QTimer::timeout,this,[this]{captureEdits();});watcher->start();
        connect(afterView,&GisViewer::selectionChanged,this,[this](int) {
            if(selecting || busy || !result)return;
            const auto selected=afterView->selectedFeatures();
            if(selected.isEmpty() || selected.first().layer!=afterOverlay)return;
            const auto id=selected.first().attributes().value("candidate_id").toString();
            for(size_t i=0;i<result->candidates.size();++i)if(result->candidates[i].id==id){selectCandidate(int(i),false);break;}
        });
        setBusy(false);if (prepareAssets) QTimer::singleShot(0,this,[this]{prepare();});
    }
};

