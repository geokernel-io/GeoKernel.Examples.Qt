#pragma once
#include "GeoKernelAI.h"
#include <QHash>
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>

namespace BuildingChange {
using GeoKernel::AI::AiRasterInferenceResult;
using GeoKernel::AI::AiTensorDataType;
using Cancel = std::function<bool()>;

inline void checkpoint(const Cancel& cancel) {
    if (cancel && cancel()) throw std::runtime_error("Analysis cancelled.");
}

struct MatchOptions {
    double minimumAreaM2 = 50.;
    double maximumOverlap = .10;
    double toleranceM = 2.;
    double minimumBuildingScore = .75;
    double maximumOtherScore = .20;
    double weakBuildingThreshold = .25;
};
struct InstanceMatch {
    qint64 pixels = 0;
    qint64 overlappingPixels = 0;
    bool touchesEdge = false;
    double buildingScoreSum = 0;
    double otherScoreSum = 0;
    bool hasScores = false;
    double overlap() const { return pixels ? double(overlappingPixels) / double(pixels) : 0.; }
    double buildingScore() const { return hasScores && pixels ? buildingScoreSum / double(pixels) : -1.; }
    double otherScore() const { return hasScores && pixels ? otherScoreSum / double(pixels) : -1.; }
};
struct Matches {
    QHash<qint32, InstanceMatch> before;
    QHash<qint32, InstanceMatch> after;
    double pixelAreaM2 = 0;
    qint64 edgeInstances = 0;
};

inline void validateGrid(const AiRasterInferenceResult& raster) {
    const qint64 count = qint64(raster.width) * raster.height;
    if (raster.width <= 0 || raster.height <= 0 || count > 16 * 1024 * 1024 ||
        raster.bandCount != 1 || raster.dataType != AiTensorDataType::Int32 ||
        raster.data.size() != count * qint64(sizeof(qint32)) || raster.geoTransform.size() != 6 ||
        raster.projectionWkt.isEmpty() || raster.cancelled)
        throw std::invalid_argument("Expected a complete georeferenced single-band Int32 instance grid.");
    const auto& t = raster.geoTransform;
    for (double value : t) if (!std::isfinite(value))
        throw std::invalid_argument("Nonfinite raster transform.");
    if (t[1] <= 0 || t[5] >= 0 || t[2] != 0 || t[4] != 0)
        throw std::invalid_argument("The comparison requires north-up grids in a shared metric CRS.");
}

// Square tolerance in source metres. It cushions small footprint-boundary shifts;
// it is not image registration and does not repair major roof displacement.
inline std::vector<unsigned char> expandedForeground(const AiRasterInferenceResult& raster,
                                                     double toleranceM, const Cancel& cancel,
                                                     const float* scores = nullptr, double threshold = .25) {
    const int width = raster.width, height = raster.height;
    const double radiusX = std::ceil(toleranceM / raster.geoTransform[1]);
    const double radiusY = std::ceil(toleranceM / -raster.geoTransform[5]);
    if (!std::isfinite(radiusX) || !std::isfinite(radiusY) || radiusX > 100 || radiusY > 100)
        throw std::invalid_argument("Matching tolerance exceeds 100 pixels.");
    const int rx = int(radiusX), ry = int(radiusY);
    const auto* values = reinterpret_cast<const qint32*>(raster.data.constData());
    const auto present = [&](int index) { return scores ? scores[index] >= threshold : values[index] > 0; };
    std::vector<unsigned char> horizontal(size_t(width) * height), output(horizontal.size());
    for (int y = 0; y < height; ++y) {
        checkpoint(cancel);
        int count = 0;
        for (int x = 0; x < std::min(width, rx + 1); ++x) count += present(y * width + x);
        for (int x = 0; x < width; ++x) {
            horizontal[size_t(y) * width + x] = count > 0;
            if (x - rx >= 0) count -= present(y * width + x - rx);
            if (x + rx + 1 < width) count += present(y * width + x + rx + 1);
        }
    }
    for (int x = 0; x < width; ++x) {
        checkpoint(cancel);
        int count = 0;
        for (int y = 0; y < std::min(height, ry + 1); ++y) count += horizontal[size_t(y) * width + x];
        for (int y = 0; y < height; ++y) {
            output[size_t(y) * width + x] = count > 0;
            if (y - ry >= 0) count -= horizontal[size_t(y - ry) * width + x];
            if (y + ry + 1 < height) count += horizontal[size_t(y + ry + 1) * width + x];
        }
    }
    return output;
}

inline Matches compare(const AiRasterInferenceResult& before, const AiRasterInferenceResult& after,
                       const MatchOptions& options, const Cancel& cancel = {},
                       const AiRasterInferenceResult* beforeScores = nullptr,
                       const AiRasterInferenceResult* afterScores = nullptr) {
    validateGrid(before); validateGrid(after);
    if (before.width != after.width || before.height != after.height ||
        before.geoTransform != after.geoTransform || before.projectionWkt != after.projectionWkt)
        throw std::invalid_argument("Two-date instance grids must have identical CRS, extent and pixel grid.");
    if (!std::isfinite(options.minimumAreaM2) || options.minimumAreaM2 <= 0 ||
        !std::isfinite(options.maximumOverlap) || options.maximumOverlap < 0 || options.maximumOverlap > 1 ||
        !std::isfinite(options.toleranceM) || options.toleranceM < 0 || options.toleranceM > 5 ||
        !std::isfinite(options.minimumBuildingScore) || options.minimumBuildingScore < 0 || options.minimumBuildingScore > 1 ||
        !std::isfinite(options.maximumOtherScore) || options.maximumOtherScore < 0 || options.maximumOtherScore > 1 ||
        !std::isfinite(options.weakBuildingThreshold) || options.weakBuildingThreshold < 0 || options.weakBuildingThreshold > 1)
        throw std::invalid_argument("Invalid area, overlap or tolerance parameters.");
    if ((beforeScores == nullptr) != (afterScores == nullptr))
        throw std::invalid_argument("Both raw score grids are required together.");
    const auto validateScores = [&](const AiRasterInferenceResult* scores) -> const float* {
        if (!scores) return nullptr;
        const qint64 pixels = qint64(before.width) * before.height;
        if (scores->dataType != AiTensorDataType::Float32 || scores->bandCount != 1 ||
            scores->width != before.width || scores->height != before.height ||
            scores->geoTransform != before.geoTransform || scores->projectionWkt != before.projectionWkt ||
            scores->data.size() != pixels * qint64(sizeof(float)) || scores->cancelled)
            throw std::invalid_argument("Raw building score grids must match the instance grid.");
        const auto* data = reinterpret_cast<const float*>(scores->data.constData());
        for (qint64 i=0;i<pixels;++i) {
            if (i % before.width == 0) checkpoint(cancel);
            if (!std::isfinite(data[i]) || data[i] < 0 || data[i] > 1)
                throw std::invalid_argument("Invalid building score; expected finite values between 0 and 1.");
        }
        return data;
    };
    const auto* aScores = validateScores(beforeScores);
    const auto* bScores = validateScores(afterScores);
    const auto a = expandedForeground(before, options.toleranceM, cancel,aScores,options.weakBuildingThreshold);
    const auto b = expandedForeground(after, options.toleranceM, cancel,bScores,options.weakBuildingThreshold);
    Matches result; result.pixelAreaM2 = before.geoTransform[1] * -before.geoTransform[5];
    if (!std::isfinite(result.pixelAreaM2) || result.pixelAreaM2 <= 0)
        throw std::invalid_argument("Invalid source pixel area.");
    const auto collect = [&](const AiRasterInferenceResult& raster, const std::vector<unsigned char>& other,
                             QHash<qint32, InstanceMatch>& stats, const float* ownScores, const float* otherScores) {
        const auto* labels = reinterpret_cast<const qint32*>(raster.data.constData());
        for (int y = 0; y < raster.height; ++y) {
            checkpoint(cancel);
            for (int x = 0; x < raster.width; ++x) {
                const auto i = size_t(y) * raster.width + x;
                const qint32 id = labels[i];
                if (id < 0) throw std::invalid_argument("Instance labels must be nonnegative.");
                if (!id) continue;
                auto& stat = stats[id]; ++stat.pixels; stat.overlappingPixels += other[i] != 0;
                if (ownScores) { stat.hasScores = true; stat.buildingScoreSum += ownScores[i]; stat.otherScoreSum += otherScores[i]; }
                stat.touchesEdge |= x == 0 || y == 0 || x == raster.width - 1 || y == raster.height - 1;
            }
        }
        for (const auto& stat : stats) result.edgeInstances += stat.touchesEdge;
    };
    collect(before, b, result.before,aScores,bScores); collect(after, a, result.after,bScores,aScores);
    return result;
}

inline bool isCandidate(const InstanceMatch& instance, double pixelArea, const MatchOptions& options) {
    return !instance.touchesEdge && instance.pixels * pixelArea >= options.minimumAreaM2 &&
           instance.overlap() < options.maximumOverlap && (!instance.hasScores ||
               (instance.buildingScore() >= options.minimumBuildingScore && instance.otherScore() <= options.maximumOtherScore));
}
}
