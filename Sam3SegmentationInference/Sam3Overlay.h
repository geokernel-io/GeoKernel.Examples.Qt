#pragma once
#include <QtCore>
#include "Serialization/Wkb/GisWkbReader.h"
#include "CoordinateSystems/CoordinateSystemFactory.h"
#include "Layers/GisLayerVector.h"
#include <memory>
#include <stdexcept>

// Materialize the small prediction layer for immediate drawing. GeoPackage
// remains the authoritative GIS export; display does not depend on a lazy
// provider retaining the worker thread's dataset/connection.
class Sam3Overlay
{
    QLibrary library;
    template <class T> T resolve(const char *name)
    {
        auto address = library.resolve(name);
        if (!address)
            throw std::runtime_error("Missing GDAL overlay function.");
        return reinterpret_cast<T>(address);
    }

  public:
    Sam3Overlay()
    {
#ifdef Q_OS_WIN
        library.setFileName(QDir(QCoreApplication::applicationDirPath()).filePath("gdal.dll"));
#else
        library.setFileName("gdal");
#endif
        library.setLoadHints(QLibrary::PreventUnloadHint);
        if (!library.load())
            throw std::runtime_error(library.errorString().toStdString());
    }
    std::unique_ptr<GeoKernel::Core::Layers::GisLayerVector> loadOverlay(const QString &path)
    {
        using namespace GeoKernel::Core;
        auto open = resolve<void *(*)(const char *, unsigned int, const char *const *, const char *const *,
                                      const char *const *)>("GDALOpenEx");
        auto close = resolve<void (*)(void *)>("GDALClose");
        auto getLayer = resolve<void *(*)(void *, int)>("GDALDatasetGetLayer");
        auto next = resolve<void *(*)(void *)>("OGR_L_GetNextFeature");
        auto destroyFeature = resolve<void (*)(void *)>("OGR_F_Destroy");
        auto getGeometry = resolve<void *(*)(void *)>("OGR_F_GetGeometryRef");
        auto getId = resolve<qint64 (*)(void *, int)>("OGR_F_GetFieldAsInteger64");
        auto wkbSize = resolve<int (*)(void *)>("OGR_G_WkbSize");
        auto exportWkb = resolve<int (*)(void *, int, unsigned char *)>("OGR_G_ExportToWkb");
        using Handle = std::unique_ptr<void, void (*)(void *)>;
        Handle dataset(open(path.toUtf8().constData(), 4, nullptr, nullptr, nullptr), close);
        if (!dataset)
            throw std::runtime_error("Could not open footprint output for display.");
        void *source = getLayer(dataset.get(), 0);
        if (!source)
            throw std::runtime_error("Footprint output has no layer.");
        auto layer =
            Layers::GisLayerVector::createInMemory("SAM3 prediction polygons", Shapes::GisShapeType::Polygon);
        // Read the authoritative SRS from the output layer. GisLayerGPKG in
        // SDK 1.5.36 retains its constructor's EPSG:4326 fallback after open().
        auto getSrs = resolve<void *(*)(void *)>("OGR_L_GetSpatialRef");
        auto exportSrs = resolve<int (*)(void *, char **)>("OSRExportToWkt");
        auto freeText = resolve<void (*)(void *)>("VSIFree");
        char *wkt = nullptr;
        if (!getSrs(source) || exportSrs(getSrs(source), &wkt) != 0 || !wkt)
            throw std::runtime_error("SAM3 output layer has no readable CRS.");
        const QString definition = QString::fromUtf8(wkt);
        freeText(wkt);
        auto crs = CoordinateSystems::CoordinateSystemFactory::fromUserInput(definition);
        if (!crs)
            throw std::runtime_error("Cannot resolve SAM3 output CRS.");
        layer->setCoordinateSystem(crs);
        layer->addAttributeDefinition({"instance_id", Layers::Defs::GisAttributeType::Integer, 0, 0});
        while (void *raw = next(source))
        {
            Handle feature(raw, destroyFeature);
            void *geometry = getGeometry(raw);
            if (!geometry)
                continue;
            QByteArray wkb(wkbSize(geometry), Qt::Uninitialized);
            if (exportWkb(geometry, 1, reinterpret_cast<unsigned char *>(wkb.data())) != 0)
                throw std::runtime_error("Could not read footprint geometry for display.");
            auto shape = Serialization::Wkb::GisWkbReader::read(wkb);
            if (!shape)
                throw std::runtime_error("Footprint geometry was not decoded.");
            shape->attributes().insert("instance_id", getId(raw, 0));
            if (!layer->addShape(std::move(shape)))
                throw std::runtime_error("Could not add footprint geometry to overlay.");
        }
        return layer;
    }
};
