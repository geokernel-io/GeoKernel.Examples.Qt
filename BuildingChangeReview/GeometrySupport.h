#pragma once
#include <QLibrary>
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>
#include <cmath>
#include <stdexcept>
#include "Shapes/GisShapePolygon.h"
#include "CoordinateSystems/CoordinateTransformer.h"
#include "CoordinateSystems/CoordinateSystemFactory.h"

// Resolve the stable GDAL C ABI from the runtime shipped with the SDK, as in the
// existing footprint examples. No separate GDAL headers or installation.
class GeometrySupport {
    QLibrary library;
    template<class T> T get(const char* name) {
        const auto address = library.resolve(name);
        if (!address) throw std::runtime_error(QString("Missing runtime function: %1").arg(name).toStdString());
        return reinterpret_cast<T>(address);
    }
public:
    GeometrySupport() {
#ifdef Q_OS_WIN
        library.setFileName(QDir(QCoreApplication::applicationDirPath()).filePath("gdal.dll"));
#else
        library.setFileName("gdal");
#endif
        library.setLoadHints(QLibrary::PreventUnloadHint);
        if (!library.load()) throw std::runtime_error(library.errorString().toStdString());
    }
    double area(const GeoKernel::Core::Shapes::GisShape& shape) {
        const auto bytes = shape.toWkb();
        void* raw = nullptr;
        const auto create = get<int(*)(const void*,void*,void**,int)>("OGR_G_CreateFromWkb");
        const auto destroy = get<void(*)(void*)>("OGR_G_DestroyGeometry");
        if (create(bytes.constData(), nullptr, &raw, bytes.size()) != 0 || !raw)
            throw std::runtime_error("Could not decode candidate polygon.");
        std::unique_ptr<void, void(*)(void*)> geometry(raw, destroy);
        if (!get<int(*)(void*)>("OGR_G_IsValid")(raw))
            throw std::runtime_error("Candidate polygon is invalid. Correct its vertices before approval.");
        const double value = get<double(*)(void*)>("OGR_G_Area")(raw);
        if (!std::isfinite(value) || value <= 0) throw std::runtime_error("Candidate polygon has no valid area.");
        return value;
    }
    QJsonObject geographicGeometry(const GeoKernel::Core::Shapes::GisShape& shape) {
        using namespace GeoKernel::Core;
        const auto* polygon = dynamic_cast<const Shapes::GisShapePolygon*>(&shape);
        if (!polygon) throw std::runtime_error("A polygon is required for export.");
        const auto source = CoordinateSystems::CoordinateSystemFactory::fromEpsg(25831);
        const auto target = CoordinateSystems::CoordinateSystemFactory::fromEpsg(4326);
        CoordinateSystems::CoordinateTransformer transformer(*source, *target);
        Shapes::GisShapePolygon transformed;
        for (const auto& part : polygon->parts()) {
            QVector<Shapes::GisShapePoint> ring;
            for (const auto& point : part) ring.push_back(transformer.transform(point));
            transformed.parts().push_back(std::move(ring));
        }
        const auto json = QJsonDocument::fromJson(transformed.toGeoJson().toUtf8());
        if (!json.isObject()) throw std::runtime_error("Could not encode exported geometry.");
        return json.object();
    }
};
