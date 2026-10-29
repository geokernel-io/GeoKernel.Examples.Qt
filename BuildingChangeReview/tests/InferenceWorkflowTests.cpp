#include "DemoWindow.h"
#include <QSaveFile>
#include <iostream>

using namespace BuildingChange;
void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
class DemoWindowTestAccess {
public:
    static void prepare(DemoWindow& window) {
        window.prepare();
        require(!window.beforePath.isEmpty() && !window.afterPath.isEmpty() && !window.modelPath.isEmpty(),
                "Application asset preparation did not complete.");
        require(window.beforeView->layerCount()==1 && window.afterView->layerCount()==1,
                "Both imagery layers must open before inference.");
    }
    static void publish(DemoWindow& window,const std::shared_ptr<JobResult>& job) {
        window.setBusy(true);window.finishAnalysis(job);
        require(window.result==job && window.overlaysReady(),"Real inference results did not publish to both viewers.");
        require(window.table->rowCount()==int(job->candidates.size()) && window.approve->isEnabled(),
                "Real candidates must populate the review table and be selectable.");
        require(window.details->text().contains(QString::number(Assets::BeforeYear)) &&
                window.details->text().contains(QString::number(Assets::AfterYear)),"Result details must use the imagery years.");
    }
};
int main(int argc,char** argv) {
    qputenv("QT_QPA_PLATFORM","offscreen");
    QApplication app(argc,argv);app.setProperty("headless",true);
    try {
        require(argc==3,"Pass the prepared asset-cache and report directories.");
        const QDir assets(QString::fromLocal8Bit(argv[1]));
        const QDir reports(QString::fromLocal8Bit(argv[2]));
        QDir().mkpath(reports.absolutePath());
        require(assets.absolutePath()==QDir(sampleDataPath()).absolutePath(),"Use the executable's data directory for the application integration test.");
        DemoWindow window(false);DemoWindowTestAccess::prepare(window);
        JobOptions options;
        options.before=assets.filePath(QString(Assets::DatasetCache)+"/"+Assets::BeforeFile);
        options.after=assets.filePath(QString(Assets::DatasetCache)+"/"+Assets::AfterFile);
        options.model=assets.filePath("hotosm-model");
        options.region=demoRegion();
        options.provider=GeoKernel::AI::AiExecutionProvider::CPU;
        auto job=std::make_shared<JobResult>();auto& result=*job;
        runAnalysis(options,result,[]{return false;},[](int progress,const QString& message){
            std::cout<<progress<<"% "<<message.toStdString()<<std::endl;
        });
        require(result.afterInstances>0,"The later image must produce real building instances; the earlier fields may contain none.");
        QSaveFile evidence(reports.filePath("comparison-evidence.json"));
        const auto evidenceBytes=QJsonDocument(result.comparisonEvidence).toJson(QJsonDocument::Indented);
        require(evidence.open(QIODevice::WriteOnly) && evidence.write(evidenceBytes)==evidenceBytes.size() && evidence.commit(),"Could not save comparison evidence.");
        QJsonArray sourceCandidates;
        for(const auto& item:result.candidates)sourceCandidates.append(QJsonObject{{"id",item.id},{"kind",item.kind},
            {"source_wkt",item.geometry->toWkt()},{"building_score",item.buildingScore},{"other_date_score",item.otherScore}});
        QSaveFile sourceReport(reports.filePath("candidate-source.json"));
        const auto sourceBytes=QJsonDocument(sourceCandidates).toJson(QJsonDocument::Indented);
        require(sourceReport.open(QIODevice::WriteOnly) && sourceReport.write(sourceBytes)==sourceBytes.size() && sourceReport.commit(),"Could not save source geometry evidence.");
        require(!result.candidates.empty(),"Expected reviewable changes in the selected real-image area.");
        DemoWindowTestAccess::publish(window,job);
        require(approvedCollection(result,{})["features"].toArray().isEmpty(),"Pending predictions must never be exported.");
        auto& candidate=result.candidates.front();
        reviewCandidate(candidate,"approved","workflow-test","Native integration verification; not expert ground truth.");
        require(approvedCollection(result,{})["features"].toArray().size()==1,"Approval must export exactly one candidate.");
        auto* polygon=dynamic_cast<GeoKernel::Core::Shapes::GisShapePolygon*>(candidate.geometry.get());
        require(polygon && !polygon->parts().isEmpty(),"Expected a polygon for vertex edit verification.");
        auto edited=candidate.geometry->clone();
        auto* changed=dynamic_cast<GeoKernel::Core::Shapes::GisShapePolygon*>(edited.get());
        for(auto& ring:changed->parts())for(auto& point:ring)point.setX(point.x()+.1);
        require(updateGeometry(candidate,*edited),"Geometry edit was not detected.");
        require(candidate.review=="pending" && candidate.reviewedGeometry.isEmpty(),"Editing approved geometry must invalidate approval.");
        require(approvedCollection(result,{})["features"].toArray().isEmpty(),"Edited geometry must require a new approval.");
        reviewCandidate(candidate,"rejected","workflow-test","Review rejection verification.");
        require(approvedCollection(result,{})["features"].toArray().isEmpty(),"Rejected predictions must never be exported.");
        reviewCandidate(candidate,"approved","workflow-test","Native integration verification; not expert ground truth.");
        const auto collection=approvedCollection(result,{});
        require(collection["features"].toArray().first().toObject()["properties"].toObject()["before_year"].toInt()==Assets::BeforeYear &&
                collection["features"].toArray().first().toObject()["properties"].toObject()["after_year"].toInt()==Assets::AfterYear,
                "Export must retain the new imagery years.");
        const auto bytes=QJsonDocument(collection).toJson(QJsonDocument::Indented);
        QSaveFile file(reports.filePath("approved-workflow-test.geojson"));
        require(file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size() && file.commit(),"Could not save integration export.");
        QJsonObject summary=result.provenance;
        summary["before_instances"]=result.beforeInstances;summary["after_instances"]=result.afterInstances;
        summary["candidate_polygons"]=qint64(result.candidates.size());
        summary["review_export_checks_passed"]=true;
        summary["application_preparation_and_publication_passed"]=true;
        QFile report(reports.filePath("inference-verification.json"));
        require(report.open(QIODevice::WriteOnly),"Could not create verification report.");
        report.write(QJsonDocument(summary).toJson(QJsonDocument::Indented));
        std::cout<<"Real-image inference, approval gating, edit invalidation and geographic export passed.\n";
        std::cout<<result.beforeInstances<<" / "<<result.afterInstances<<" instances; "<<result.candidates.size()<<" candidate polygons.\n";
        return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
