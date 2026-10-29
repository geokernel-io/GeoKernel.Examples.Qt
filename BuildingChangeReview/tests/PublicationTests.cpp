#include "DemoWindow.h"
#include <iostream>

class NonEditableReference final : public GisLayer {
public:
    NonEditableReference() { setName("Pinned reference imagery"); setCoordinateSystem(CoordinateSystemFactory::fromEpsg(25831)); }
    GisExtent extent() const override { return {432000,4583000,433024,4584024}; }
    void open() override { setIsOpen(true); }
    void close() override { setIsOpen(false); }
};

class DemoWindowTestAccess {
    static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
public:
    static void run() {
        DemoWindow window(false);
        require(window.threshold->value()==.4371 && window.minimumArea->value()==50 && window.tolerance->value()==2,"UI defaults must match the conservative analysis policy");
        GisSymbolContext styleContext;styleContext.attributes={{"kind","new"},{"review","pending"}};
        ChangeRenderer beforeRenderer(true),afterRenderer(false);
        require(beforeRenderer.styleForFeature(styleContext).fillOpacity()==0 && beforeRenderer.styleForFeature(styleContext).lineColor()=="transparent","Addition candidates must not cover the before imagery");
        require(afterRenderer.styleForFeature(styleContext).fillOpacity()>0,"Addition candidates must remain visible after");
        styleContext.attributes["kind"]="removed";
        require(afterRenderer.clone()->styleForFeature(styleContext).fillOpacity()==0 && beforeRenderer.styleForFeature(styleContext).fillOpacity()>0,"Removal candidates and cloned renderers must respect dates");
        for (auto* view : {window.beforeView,window.afterView}) {
            auto reference = std::make_unique<NonEditableReference>(); reference->open();
            view->addLayer(reference);
        }
        const auto beforeReference = window.beforeView->mapLayerAt(0);
        const auto afterReference = window.afterView->mapLayerAt(0);
        const auto publish = [&](bool hasCandidate) {
            window.setBusy(true);
            auto job = std::make_shared<JobResult>();
            if (hasCandidate) {
                Candidate item; item.id="new-test"; item.kind="new";
                auto geometry=std::make_unique<GisShapePolygon>();
                geometry->fromWkt("POLYGON((432400 4583400,432410 4583400,432410 4583410,432400 4583410,432400 4583400))");
                item.geometry=std::move(geometry);item.areaM2=100;
                job->candidates.push_back(std::move(item));
            }
            window.result=job;
            window.installResults();
            window.setBusy(false);
            if (hasCandidate) window.selectCandidate(0,false);
        };
        publish(true);
        require(window.beforeView->layerCount()==2 && window.afterView->layerCount()==2,"Publication did not retain source and candidate layers");
        require(window.beforeView->mapLayerIndex(beforeReference)>=0 && window.afterView->mapLayerIndex(afterReference)>=0,"Source imagery was removed");
        require(window.beforeOverlay && window.afterOverlay,"Candidate overlays were not published");
        require(window.afterOverlay->isEditing(),"The actual candidate layer must be editable");
        require(!window.afterView->isLayerEditing(window.afterView->mapLayerIndex(afterReference)),"The source layer must not enter editing");
        require(window.table->rowCount()==1 && window.approve->isEnabled(),"First candidate must be selectable after completion");
        require(window.beforeView->selectedFeatures().first().layerIndex==window.beforeView->mapLayerIndex(window.beforeOverlay),"Selection must use the current layer index");
        require(window.afterView->selectedFeatures().first().layerIndex==window.afterView->mapLayerIndex(window.afterOverlay),"Right selection has a stale layer index");
        window.showCandidates->setChecked(false);
        require(window.beforeView->selectedFeatures().isEmpty() && window.afterView->selectedFeatures().isEmpty() && !window.editAction->isEnabled(),"Hiding overlays must clear highlights and prevent invisible editing");
        window.showCandidates->setChecked(true);window.selectCandidate(0,false);
        require(window.editAction->isEnabled() && !window.afterView->selectedFeatures().isEmpty(),"Overlay inspection must be reversible");
        // Reordering the viewer stack must not redirect undo/redo to the source.
        const int originalIndex=window.afterView->mapLayerIndex(window.afterOverlay);
        require(window.afterView->moveLayer(originalIndex,1-originalIndex),"Could not reorder layers for the regression");
        auto edited=window.afterOverlay->getShape(1)->clone();
        auto* polygon=dynamic_cast<GisShapePolygon*>(edited.get());
        for(auto& ring:polygon->parts())for(auto& point:ring)point.setX(point.x()+1);
        require(window.afterOverlay->setShapeGeometryEdit(1,std::move(edited)),"Could not edit the actual overlay");
        window.captureEdits();
        window.undoAction->trigger();
        require(window.result->candidates[0].geometry->extent().xMin()==432400,"Undo used the wrong layer after reordering");
        window.redoAction->trigger();
        require(window.result->candidates[0].geometry->extent().xMin()==432401,"Redo used the wrong layer after reordering");
        publish(true);
        require(window.beforeView->mapLayerIndex(beforeReference)>=0 && window.afterView->mapLayerIndex(afterReference)>=0,"Rerun removed source imagery instead of old candidates");
        require(window.beforeView->layerCount()==2 && window.afterView->layerCount()==2,"Rerun duplicated candidate layers");
        publish(false);
        require(window.beforeView->mapLayerIndex(beforeReference)>=0 && window.afterView->mapLayerIndex(afterReference)>=0,"Empty result removed source imagery");
        require(window.table->rowCount()==0 && !window.approve->isEnabled() && !window.save->isEnabled(),"Empty result must disable review and export");
        window.selectCandidate(0,false); window.captureEdits();
        // Exercise the same completion handler as QThread::finished, including
        // publication failure followed by retry. No selection may use null layers.
        window.setBusy(true);
        auto malformed=std::make_shared<JobResult>();
        Candidate missingGeometry; missingGeometry.id="invalid-test";
        malformed->candidates.push_back(std::move(missingGeometry));
        window.finishAnalysis(malformed);
        require(!window.result && !window.beforeOverlay && !window.afterOverlay,"Publication failure left stale candidate pointers");
        require(!window.approve->isEnabled() && !window.editAction->isEnabled(),"Failed publication enabled review/edit controls");
        require(window.progress->format()=="Display failed","Failed publication must not report successful completion");
        require(window.beforeView->mapLayerIndex(beforeReference)>=0 && window.afterView->mapLayerIndex(afterReference)>=0,"Publication failure removed source imagery");
        publish(true);
        require(window.approve->isEnabled(),"Publication did not recover after a failed display");
        std::cout<<"Source preservation, overlay editing, selection, rerun and empty-result publication passed.\n";
    }
};

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try { DemoWindowTestAccess::run(); return 0; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
