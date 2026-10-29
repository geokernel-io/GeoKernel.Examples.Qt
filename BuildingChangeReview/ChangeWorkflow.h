#pragma once
#include "ChangeLogic.h"
#include "DemoAssets.h"
#include "FootprintOutput.h"
#include "GeometrySupport.h"
#include "RasterCrop.h"
#include <QDateTime>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QCryptographicHash>

namespace BuildingChange {
struct Candidate {
    QString id;
    QString kind;
    QString review = "pending";
    QString note;
    QString reviewedBy;
    qint64 instanceId = 0;
    double overlap = 0;
    double areaM2 = 0;
    double buildingScore = -1, otherScore = -1;
    QByteArray reviewedGeometry;
    std::unique_ptr<GeoKernel::Core::Shapes::GisShape> geometry;
};
struct JobResult {
    QString error;
    bool cancelled = false;
    std::vector<Candidate> candidates;
    QJsonObject provenance;
    int beforeInstances = 0, afterInstances = 0;
    qint64 edgeInstances = 0, invalidPolygons = 0;
    qint64 uncertainInstances = 0;
    QJsonArray comparisonEvidence;
    qint64 elapsedMilliseconds = 0;
};
struct JobOptions {
    QString before, after, model;
    GeoKernel::Core::Shapes::GisExtent region;
    GeoKernel::AI::AiExecutionProvider provider = GeoKernel::AI::AiExecutionProvider::Auto;
    double foregroundThreshold = .4371;
    MatchOptions matching;
};
using Progress = std::function<void(int,const QString&)>;
// Fields in the earlier image and residential plots in the later image.
// Choosing this extent is demo presentation, not a model accuracy evaluation.
inline GeoKernel::Core::Shapes::GisExtent demoRegion() { return {657885,5800734,658205,5801054}; }
inline GeoKernel::Core::Shapes::GisExtent datasetRegion() {
    return {Assets::DatasetBounds[0],Assets::DatasetBounds[1],Assets::DatasetBounds[2],Assets::DatasetBounds[3]};
}

inline void runAnalysis(const JobOptions& options, JobResult& result, const Cancel& cancel,
                        const Progress& progress) {
    using namespace GeoKernel::AI;
    using namespace GeoKernel::Analysis;
    QElapsedTimer timer; timer.start();
    QTemporaryDir workspace;
    if (!workspace.isValid()) throw std::runtime_error("Could not create analysis workspace.");
    const auto package = AiModelPackage::open(options.model);
    const auto errors = package.validationErrors();
    if (!errors.isEmpty()) throw std::runtime_error(errors.join("\n").toStdString());
    const auto manifest = package.manifest();
    if (manifest.id != Assets::ModelId) throw std::runtime_error("The HOTOSM DINOv3-S building package is required.");
    checkpoint(cancel);
    progress(1, "Preparing the same analysis extent for both dates...");
    const QString before = prepareInferenceRaster(options.before, workspace.filePath("before.tif"), 2048, options.region);
    const QString after = prepareInferenceRaster(options.after, workspace.filePath("after.tif"), 2048, options.region);
    checkpoint(cancel);
    const auto infer = [&](const QString& raster, const QString& output, int start, const QString& year) {
        AiInstanceVectorizationRequest request;
        request.inference.modelPackagePath = options.model;
        request.inference.rasterPath = raster;
        request.inference.sessionOptions.provider = options.provider;
        request.inference.sessionOptions.intraOpThreads = 2;
        request.inference.sessionOptions.interOpThreads = 1;
        request.inference.isCancellationRequested = cancel;
        request.outputPath = output;
        return runDemoInference(request, [&](const AnalysisProgress& state) {
            progress(start + std::clamp(state.percent,0,100) * 43 / 100,
                     year + ": " + state.message);
        }, options.foregroundThreshold);
    };
    const QString beforePolygons = workspace.filePath("before.geojson");
    const QString afterPolygons = workspace.filePath("after.geojson");
    auto a = infer(before, beforePolygons, 3, QString::number(Assets::BeforeYear));
    checkpoint(cancel);
    auto b = infer(after, afterPolygons, 47, QString::number(Assets::AfterYear));
    checkpoint(cancel);
    progress(91, "Comparing building instances across dates...");
    // The supplied demo uses a verified projected metre CRS. Never apply metric
    // area/tolerance parameters to an arbitrary geographic raster.
    GeoKernel::Formats::Common::Gdal::GdalRasterProvider source;
    QString error;
    if (!source.open(before, &error) || source.metadata().epsgCode != 25831)
        throw std::runtime_error("This example requires the pinned EPSG:25831 imagery.");
    const auto matches = compare(a.inference.labels, b.inference.labels, options.matching, cancel,
        &a.inference.decodedOutputs[0].raster,&b.inference.decodedOutputs[0].raster);
    result.beforeInstances = matches.before.size(); result.afterInstances = matches.after.size();
    result.edgeInstances = matches.edgeInstances;
    const auto recordEvidence = [&](const QHash<qint32,InstanceMatch>& stats,const QString& kind) {
        for (auto it=stats.cbegin();it!=stats.cend();++it) {
            const auto& stat=it.value();
            if(stat.touchesEdge || stat.pixels*matches.pixelAreaM2 < options.matching.minimumAreaM2)continue;
            const bool selected=isCandidate(stat,matches.pixelAreaM2,options.matching);
            result.uncertainInstances += !selected;
            result.comparisonEvidence.append(QJsonObject{{"kind",kind},{"instance_id",it.key()},
                {"area_m2",stat.pixels*matches.pixelAreaM2},{"coverage",stat.overlap()},
                {"building_score",stat.buildingScore()},{"other_date_score",stat.otherScore()},{"selected",selected}});
        }
    };
    recordEvidence(matches.before,"removed");recordEvidence(matches.after,"new");
    FootprintOutput reader;
    GeometrySupport geometry;
    const auto append = [&](const QString& path, const QHash<qint32,InstanceMatch>& stats,
                            const QString& kind) {
        auto layer = reader.loadOverlay(path, a.inference.labels.projectionWkt);
        for (const auto* shape : layer->items()) {
            checkpoint(cancel);
            const qint32 instanceId = shape->attributes().value("instance_id").toInt();
            const auto found = stats.constFind(instanceId);
            if (found == stats.cend() || !isCandidate(found.value(), matches.pixelAreaM2, options.matching)) continue;
            double area = 0;
            try { area = geometry.area(*shape); }
            catch (const std::exception&) { ++result.invalidPolygons; continue; }
            if (area < options.matching.minimumAreaM2) continue;
            Candidate item;
            item.id = QString("%1-%2").arg(kind).arg(result.candidates.size()+1, 4, 10, QChar('0'));
            item.kind = kind; item.instanceId = instanceId; item.overlap = found->overlap();
            item.areaM2 = area; item.geometry = shape->clone();
            item.buildingScore = found->buildingScore(); item.otherScore = found->otherScore();
            result.candidates.push_back(std::move(item));
        }
    };
    append(afterPolygons, matches.after, "new");
    append(beforePolygons, matches.before, "removed");
    result.elapsedMilliseconds = timer.elapsed();
    QJsonArray transform;
    for (const auto value : a.inference.labels.geoTransform) transform.append(value);
    result.provenance = {
        {"created_at_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"before_year", Assets::BeforeYear}, {"after_year",Assets::AfterYear}, {"source_crs", "EPSG:25831"},
        {"before_sha256", Assets::BeforeSha256}, {"after_sha256",Assets::AfterSha256},
        {"dataset_url",Assets::DatasetUrl}, {"dataset_archive_sha256",Assets::DatasetSha256},
        {"model_id",manifest.id}, {"model_version",manifest.version}, {"model_sha256",manifest.modelSha256},
        {"model_archive_sha256",Assets::ModelSha256},
        {"execution_provider",aiExecutionProviderName(a.inference.labels.provider)},
        {"foreground_threshold",options.foregroundThreshold},
        {"minimum_area_m2",options.matching.minimumAreaM2},
        {"maximum_other_date_coverage",options.matching.maximumOverlap},
        {"matching_tolerance_m",options.matching.toleranceM},
        {"minimum_building_score",options.matching.minimumBuildingScore},
        {"maximum_other_date_score",options.matching.maximumOtherScore},
        {"weak_building_threshold",options.matching.weakBuildingThreshold},
        {"analysis_width",a.inference.labels.width}, {"analysis_height",a.inference.labels.height},
        {"analysis_geotransform",transform}, {"elapsed_ms",result.elapsedMilliseconds},
        {"edge_instances_excluded",result.edgeInstances}, {"invalid_polygons_excluded",result.invalidPolygons},
        {"uncertain_or_matched_instances_excluded",result.uncertainInstances},
        {"attribution",Assets::Attribution}, {"imagery_license","https://creativecommons.org/licenses/by/4.0/"},
        {"method","Independent building predictions; strong own-date mean score, low other-date mean score and low buffered weak-score coverage propose new/removed candidates. Raw scores are not calibrated change probabilities. No image registration."}
    };
    checkpoint(cancel);
    progress(100, "Candidate analysis complete. Review before exporting.");
}

inline bool updateGeometry(Candidate& item, const GeoKernel::Core::Shapes::GisShape& shape) {
    if (shape.toWkb() == item.geometry->toWkb()) return false;
    item.geometry = shape.clone(); item.review = "pending";
    item.reviewedGeometry.clear(); item.reviewedBy.clear();
    item.note = "Geometry edited; review again.";
    try { GeometrySupport geometry; item.areaM2 = geometry.area(*item.geometry); }
    catch (const std::exception&) { item.areaM2 = 0; }
    return true;
}

inline void reviewCandidate(Candidate& item, const QString& decision, const QString& actor, const QString& note) {
    if (actor.trimmed().isEmpty()) throw std::invalid_argument("A reviewer name is required.");
    if (decision != "approved" && decision != "rejected" && decision != "pending")
        throw std::invalid_argument("Unknown review decision.");
    if (decision == "approved") { GeometrySupport geometry; item.areaM2 = geometry.area(*item.geometry); }
    item.review = decision; item.note = note.trimmed(); item.reviewedBy = actor.trimmed();
    item.reviewedGeometry = decision == "approved" ? item.geometry->toWkb() : QByteArray{};
}

inline QJsonObject approvedCollection(const JobResult& result, const QJsonArray& audit) {
    GeometrySupport geometry; QJsonArray features;
    for (const auto& item : result.candidates) {
        if (item.review != "approved") continue;
        if (item.reviewedGeometry != item.geometry->toWkb())
            throw std::runtime_error("An approved geometry changed; review it again.");
        const double area = geometry.area(*item.geometry);
        features.append(QJsonObject{{"type","Feature"},{"id",item.id},
            {"geometry",geometry.geographicGeometry(*item.geometry)},
            {"properties",QJsonObject{{"candidate_id",item.id},{"change_type",item.kind},
                {"review_status",item.review},{"review_note",item.note},{"reviewed_by",item.reviewedBy},
                {"area_m2",area},{"source_instance_id",item.instanceId},{"other_date_coverage",item.overlap},
                {"building_score",item.buildingScore},{"other_date_score",item.otherScore},
                {"before_year",Assets::BeforeYear},{"after_year",Assets::AfterYear},{"model_id",result.provenance["model_id"]}}}});
    }
    return {{"type","FeatureCollection"},{"features",features},{"geokernel_provenance",result.provenance},
        {"geokernel_review_audit",audit},
        {"coordinate_note","GeoJSON coordinates are WGS84 longitude/latitude. area_m2 is measured in source EPSG:25831."}};
}
}
