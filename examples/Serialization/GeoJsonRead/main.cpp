#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

#include "CoordinateSystems/CoordinateSystemFactory.h"


#include "CoordinateSystems/CoordinateTransformer.h"
#include "Layers/GisLayerStyle.h"
#include "Layers/GisLayerVector.h"
#include "Raster/Xyz/PredefinedXyzLayers.h"
#include "Serialization/GeoJson/GisGeoJsonReader.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShape.h"
#include "Shapes/GisShapePoint.h"
#include "Shapes/GisShapePolygon.h"
#include "Shapes/GisShapePolyline.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::CoordinateSystems;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Serialization::GeoJson;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Formats::Raster::Xyz;
using namespace GeoKernel::Viewer;

constexpr const char* PointJson =
    "{\n"
    "  \"type\": \"Point\",\n"
    "  \"coordinates\": [-122.4194, 37.7749]\n"
    "}";

constexpr const char* LineStringJson =
    "{\n"
    "  \"type\": \"LineString\",\n"
    "  \"coordinates\": [\n"
    "    [-122.4194, 37.7749],\n"
    "    [-121.8863, 37.3382],\n"
    "    [-121.4944, 38.5816],\n"
    "    [-120.7401, 37.6391]\n"
    "  ]\n"
    "}";

constexpr const char* PolygonJson =
    "{\n"
    "  \"type\": \"Polygon\",\n"
    "  \"coordinates\": [[\n"
    "    [-123.25, 37.15],\n"
    "    [-122.15, 36.95],\n"
    "    [-121.55, 37.65],\n"
    "    [-122.05, 38.35],\n"
    "    [-123.05, 38.15],\n"
    "    [-123.25, 37.15]\n"
    "  ]]\n"
    "}";

constexpr const char* MultiPolygonJson =
    "{\n"
    "  \"type\": \"MultiPolygon\",\n"
    "  \"coordinates\": [\n"
    "    [[\n"
    "      [-123.25, 37.15],\n"
    "      [-122.25, 36.95],\n"
    "      [-121.85, 37.65],\n"
    "      [-122.45, 38.20],\n"
    "      [-123.15, 37.95],\n"
    "      [-123.25, 37.15]\n"
    "    ]],\n"
    "    [[\n"
    "      [-121.60, 36.75],\n"
    "      [-120.70, 36.70],\n"
    "      [-120.45, 37.35],\n"
    "      [-121.25, 37.65],\n"
    "      [-121.60, 36.75]\n"
    "    ]]\n"
    "  ]\n"
    "}";

GisLayerStyle pointStyle()
{
    GisLayerStyle style;
    style.setPointColor(QStringLiteral("#D95D39"));
    style.setLineColor(QStringLiteral("#8C321D"));
    style.setPointSize(14.0f);
    style.setLineWidth(1.5f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("name"));
    style.setLabelColor(QStringLiteral("#1F2D2D"));
    style.setLabelFontSize(11);
    style.setLabelOffsetX(10);
    style.setLabelOffsetY(-8);
    return style;
}

GisLayerStyle lineStyle()
{
    GisLayerStyle style;
    style.setLineColor(QStringLiteral("#E4572E"));
    style.setLineWidth(4.0f);
    style.setPointColor(QStringLiteral("#F3A712"));
    style.setPointSize(7.0f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("name"));
    style.setLabelColor(QStringLiteral("#1F2D2D"));
    style.setLabelFontSize(11);
    style.setLabelOffsetX(8);
    style.setLabelOffsetY(-8);
    return style;
}

GisLayerStyle polygonStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#88D18A80"));
    style.setLineColor(QStringLiteral("#1F7A4D"));
    style.setLineWidth(2.5f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("name"));
    style.setLabelColor(QStringLiteral("#183B2A"));
    style.setLabelFontSize(11);
    style.setLabelOffsetX(10);
    style.setLabelOffsetY(-8);
    return style;
}

std::unique_ptr<GisLayerVector> createMemoryLayer(
    const QString& name,
    GisShapeType shapeType,
    const GisLayerStyle& style)
{
    auto layer = GisLayerVector::createInMemory(
        name,
        shapeType,
        GisExtent(-180.0, -90.0, 180.0, 90.0));
    layer->setCoordinateSystem(CoordinateSystemFactory::fromEpsg(4326));
    layer->style() = style;
    layer->open();
    return layer;
}

void clearLayer(GisLayerVector& layer)
{
    if (!layer.isEditing())
        layer.beginEdit();

    while (layer.count() > 0)
    {
        const QVector<GisShape*> shapes = layer.items();
        if (shapes.isEmpty() || shapes.first() == nullptr)
            break;

        layer.deleteShapeEdit(shapes.first()->id());
    }

    layer.commitEdit();
    layer.clearEditHistory();
}

void clearLayers(GisLayerVector& pointLayer, GisLayerVector& lineLayer, GisLayerVector& polygonLayer)
{
    clearLayer(pointLayer);
    clearLayer(lineLayer);
    clearLayer(polygonLayer);
}

GisShapePoint toWebMercator(const GisShapePoint& lonLat)
{
    const auto wgs84 = CoordinateSystemFactory::fromEpsg(4326);
    const auto webMercator = CoordinateSystemFactory::fromEpsg(3857);
    return CoordinateTransformer(*wgs84, *webMercator).transform(lonLat);
}

QVector<GisShapePoint> pointsOf(const GisShape& shape)
{
    if (const auto* point = dynamic_cast<const GisShapePoint*>(&shape))
        return { *point };

    QVector<GisShapePoint> points;
    if (const auto* polyline = dynamic_cast<const GisShapePolyline*>(&shape))
    {
        for (const QVector<GisShapePoint>& part : polyline->parts())
            points += part;
    }
    else if (const auto* polygon = dynamic_cast<const GisShapePolygon*>(&shape))
    {
        for (const QVector<GisShapePoint>& part : polygon->parts())
            points += part;
    }

    return points;
}

GisExtent projectedExtent(const GisShape& shape)
{
    const QVector<GisShapePoint> points = pointsOf(shape);
    if (points.isEmpty())
        return GisExtent(-20037508.342789244, -20037508.342789244, 20037508.342789244, 20037508.342789244);

    bool hasPoint = false;
    GisExtent extent = GisExtent::empty();
    for (const GisShapePoint& point : points)
    {
        const GisShapePoint projected = toWebMercator(point);
        const GisExtent pointExtent(projected.x(), projected.y(), projected.x(), projected.y());
        extent = hasPoint ? extent.expand(pointExtent) : pointExtent;
        hasPoint = true;
    }

    const double paddingX = qMax(250000.0, extent.width() * 0.35);
    const double paddingY = qMax(250000.0, extent.height() * 0.35);
    return extent.inflate(paddingX, paddingY);
}

QString shapeTypeName(const GisShape& shape)
{
    if (dynamic_cast<const GisShapePoint*>(&shape) != nullptr)
        return QStringLiteral("Point");
    if (dynamic_cast<const GisShapePolyline*>(&shape) != nullptr)
        return QStringLiteral("Polyline");
    if (dynamic_cast<const GisShapePolygon*>(&shape) != nullptr)
        return QStringLiteral("Polygon");
    return QStringLiteral("Unknown");
}

int partCount(const GisShape& shape)
{
    if (dynamic_cast<const GisShapePoint*>(&shape) != nullptr)
        return 1;
    if (const auto* polyline = dynamic_cast<const GisShapePolyline*>(&shape))
        return polyline->parts().size();
    if (const auto* polygon = dynamic_cast<const GisShapePolygon*>(&shape))
        return polygon->parts().size();
    return 0;
}

QString presetJson(int index)
{
    switch (index)
    {
        case 0:
            return QString::fromLatin1(PointJson);
        case 1:
            return QString::fromLatin1(LineStringJson);
        case 2:
            return QString::fromLatin1(PolygonJson);
        case 3:
            return QString::fromLatin1(MultiPolygonJson);
        default:
            return QString::fromLatin1(PointJson);
    }
}

QString detailsText(const QString& inputJson, const GisShape& shape, const GisExtent& viewExtent)
{
    QStringList details;
    details << QStringLiteral("GeoJsonRead sample");
    details << QStringLiteral("");
    details << QStringLiteral("API");
    details << QStringLiteral("GisGeoJsonReader::read(jsonString)");
    details << QStringLiteral("");
    details << QStringLiteral("Input GeoJSON geometry");
    details << inputJson;
    details << QStringLiteral("");
    details << QStringLiteral("Parsed shape");
    details << QStringLiteral("Shape class: %1").arg(shapeTypeName(shape));
    details << QStringLiteral("Parts: %1").arg(partCount(shape));
    details << QStringLiteral("Vertices: %1").arg(pointsOf(shape).size());
    details << QStringLiteral("Lon/lat extent: %1").arg(shape.extent().toString());
    details << QStringLiteral("");
    details << QStringLiteral("Displayed over OSM");
    details << QStringLiteral("Layer CRS: EPSG:4326");
    details << QStringLiteral("Viewer/OSM CRS: EPSG:3857");
    details << QStringLiteral("WebMercator view extent: %1").arg(viewExtent.toString());
    return details.join(QStringLiteral("\n"));
}

void refreshViewer(GisViewer& viewer)
{
    viewer.invalidateRenderCache(true, true);
    viewer.refreshLayers();
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1120, 760);
    window.setWindowTitle(QStringLiteral("GeoJsonRead"));

    auto* root = new QWidget(&window);
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* inputBar = new QWidget(root);
    auto* inputLayout = new QHBoxLayout(inputBar);
    inputLayout->setContentsMargins(6, 4, 6, 4);
    inputLayout->setSpacing(8);

    auto* presetCombo = new QComboBox(inputBar);
    presetCombo->addItem(QStringLiteral("Point"));
    presetCombo->addItem(QStringLiteral("LineString"));
    presetCombo->addItem(QStringLiteral("Polygon"));
    presetCombo->addItem(QStringLiteral("MultiPolygon"));

    auto* readButton = new QPushButton(QStringLiteral("Read GeoJSON"), inputBar);
    inputLayout->addWidget(new QLabel(QStringLiteral("Preset:"), inputBar));
    inputLayout->addWidget(presetCombo);
    inputLayout->addWidget(readButton);
    inputLayout->addStretch(1);

    auto* body = new QWidget(root);
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    auto* leftPanel = new QWidget(body);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    auto* jsonEdit = new QTextEdit(leftPanel);
    jsonEdit->setMinimumHeight(180);
    jsonEdit->setPlainText(QString::fromLatin1(PointJson));

    auto* viewer = new GisViewer(leftPanel);
    viewer->setActiveTool(GisViewerTool::Pan);

    leftLayout->addWidget(jsonEdit);
    leftLayout->addWidget(viewer, 1);

    auto* detailsView = new QTextEdit(body);
    detailsView->setReadOnly(true);
    detailsView->setMinimumWidth(400);

    bodyLayout->addWidget(leftPanel, 1);
    bodyLayout->addWidget(detailsView);

    rootLayout->addWidget(inputBar);
    rootLayout->addWidget(body, 1);
    window.setCentralWidget(root);

    auto osmLayer = std::make_unique<GisLayerXyzOpenStreetMap>();
    osmLayer->setLocalCacheEnabled(true);
    osmLayer->open();
    viewer->addLayer(osmLayer);

    auto pointLayer = createMemoryLayer(QStringLiteral("GeoJSON Point"), GisShapeType::Point, pointStyle());
    auto lineLayer = createMemoryLayer(QStringLiteral("GeoJSON LineString"), GisShapeType::Polyline, lineStyle());
    auto polygonLayer = createMemoryLayer(QStringLiteral("GeoJSON Polygon"), GisShapeType::Polygon, polygonStyle());

    GisLayerVector* pointLayerPtr = pointLayer.get();
    GisLayerVector* lineLayerPtr = lineLayer.get();
    GisLayerVector* polygonLayerPtr = polygonLayer.get();

    viewer->addLayer(pointLayer);
    viewer->addLayer(lineLayer);
    viewer->addLayer(polygonLayer);

    const auto targetLayer = [&](const GisShape& shape) -> GisLayerVector*
    {
        if (dynamic_cast<const GisShapePoint*>(&shape) != nullptr)
            return pointLayerPtr;
        if (dynamic_cast<const GisShapePolyline*>(&shape) != nullptr)
            return lineLayerPtr;
        if (dynamic_cast<const GisShapePolygon*>(&shape) != nullptr)
            return polygonLayerPtr;
        return nullptr;
    };

    const auto parseGeoJson = [&]
    {
        try
        {
            const QString inputJson = jsonEdit->toPlainText().trimmed();
            std::unique_ptr<GisShape> shape = GisGeoJsonReader::read(inputJson);
            if (!shape)
                throw std::runtime_error("GeoJSON reader returned an empty shape.");

            GisLayerVector* layer = targetLayer(*shape);
            if (layer == nullptr)
                throw std::runtime_error("Parsed shape type is not supported by this sample.");

            clearLayers(*pointLayerPtr, *lineLayerPtr, *polygonLayerPtr);

            if (!layer->isEditing())
                layer->beginEdit();

            QHash<QString, QVariant> attributes;
            attributes.insert(QStringLiteral("name"), shapeTypeName(*shape));
            layer->addShapeEdit(shape->clone(), attributes);
            layer->commitEdit();
            layer->clearEditHistory();

            const GisExtent viewExtent = projectedExtent(*shape);
            detailsView->setPlainText(detailsText(inputJson, *shape, viewExtent));
            viewer->setViewExtent(viewExtent);
            refreshViewer(*viewer);
            window.statusBar()->showMessage(QStringLiteral("GisGeoJsonReader::read parsed %1 with %2 vertices.")
                .arg(shapeTypeName(*shape))
                .arg(pointsOf(*shape).size()));
        }
        catch (const std::exception& ex)
        {
            clearLayers(*pointLayerPtr, *lineLayerPtr, *polygonLayerPtr);
            refreshViewer(*viewer);
            detailsView->setPlainText(QStringLiteral("GeoJSON parse failed:\n%1").arg(QString::fromUtf8(ex.what())));
            window.statusBar()->showMessage(QStringLiteral("GeoJSON parse failed."));
        }
    };

    QObject::connect(readButton, &QPushButton::clicked, viewer, parseGeoJson);
    QObject::connect(presetCombo, &QComboBox::currentIndexChanged, viewer, [=](int index)
    {
        jsonEdit->setPlainText(presetJson(index));
        parseGeoJson();
    });

    parseGeoJson();
    window.show();

    return app.exec();
}
