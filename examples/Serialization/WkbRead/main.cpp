#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QRegularExpression>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>

#include <memory>
#include <stdexcept>

#include "CoordinateSystems/Defs/GeographicCoordinateSystem.h"
#include "CoordinateSystems/Defs/KnownCoordinateSystems.h"
#include "CoordinateSystems/Defs/ProjectedCoordinateSystem.h"
#include "CoordinateSystems/Transform/CoordinateTransformer.h"
#include "Layers/GisLayerStyle.h"
#include "Layers/GisLayerVector.h"
#include "Raster/Xyz/PredefinedXyzLayers.h"
#include "Serialization/Wkb/GisWkbReader.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShape.h"
#include "Shapes/GisShapePoint.h"
#include "Shapes/GisShapePolygon.h"
#include "Shapes/GisShapePolyline.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::CoordinateSystems::Defs;
using namespace GeoKernel::Core::CoordinateSystems::Transform;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Serialization::Wkb;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Formats::Raster::Xyz;
using namespace GeoKernel::Viewer;

constexpr const char* PointWkb =
    "010100000050FC1873D79A5EC0D0D556EC2FE34240";

constexpr const char* LineStringWkb =
    "01020000000400000050FC1873D79A5EC0D0D556EC2FE34240789CA223B9785EC0ECC039234AAB4240"
    "1DC9E53FA45F5EC043AD69DE714A434041F163CC5D2F5EC0D26F5F07CED14240";

constexpr const char* PolygonWkb =
    "010300000001000000060000000000000000D05EC033333333339342409A99999999895EC09A999999"
    "997942403333333333635EC03333333333D342403333333333835EC0CDCCCCCCCC2C43403333333333"
    "C35EC033333333331343400000000000D05EC03333333333934240";

constexpr const char* MultiPolygonWkb =
    "010600000002000000010300000001000000060000000000000000D05EC033333333339342400000000"
    "000905EC09A999999997942406666666666765EC03333333333D34240CDCCCCCCCC9C5EC09A999999"
    "991943409A99999999C95EC09A99999999F942400000000000D05EC033333333339342400103000000"
    "01000000050000006666666666665EC00000000000604240CDCCCCCCCC2C5EC09A99999999594240CD"
    "CCCCCCCC1C5EC0CDCCCCCCCCAC42400000000000505EC03333333333D342406666666666665EC00000"
    "000000604240";

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
    layer->setCoordinateSystem(std::make_shared<GeographicCoordinateSystem>(KnownCoordinateSystems::wgs84()));
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
    const GeographicCoordinateSystem wgs84 = KnownCoordinateSystems::wgs84();
    const ProjectedCoordinateSystem webMercator = KnownCoordinateSystems::webMercator();
    return CoordinateTransformer(wgs84, webMercator).transform(lonLat);
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

QByteArray parseHexWkb(const QString& text)
{
    QString cleaned = text;
    cleaned.remove(QRegularExpression(QStringLiteral("\\s+")));

    if (cleaned.isEmpty())
        throw std::runtime_error("WKB hex input is empty.");

    if ((cleaned.size() % 2) != 0)
        throw std::runtime_error("WKB hex input must contain an even number of characters.");

    if (!QRegularExpression(QStringLiteral("^[0-9A-Fa-f]+$")).match(cleaned).hasMatch())
        throw std::runtime_error("WKB input must be hexadecimal.");

    return QByteArray::fromHex(cleaned.toLatin1());
}

QString detailsText(
    const QString& inputHex,
    const QByteArray& bytes,
    const GisShape& shape,
    const GisExtent& viewExtent)
{
    QStringList details;
    details << QStringLiteral("WkbRead sample");
    details << QStringLiteral("");
    details << QStringLiteral("API");
    details << QStringLiteral("GisWkbReader::read(byteArray)");
    details << QStringLiteral("");
    details << QStringLiteral("Input WKB");
    details << QStringLiteral("Hex characters: %1").arg(inputHex.trimmed().size());
    details << QStringLiteral("Byte count: %1").arg(bytes.size());
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
    details << QStringLiteral("");
    details << QStringLiteral("Input hex");
    details << inputHex.trimmed();
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
    window.setWindowTitle(QStringLiteral("WkbRead"));

    auto* root = new QWidget(&window);
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* inputBar = new QWidget(root);
    auto* inputLayout = new QHBoxLayout(inputBar);
    inputLayout->setContentsMargins(6, 4, 6, 4);
    inputLayout->setSpacing(8);

    auto* readButton = new QPushButton(QStringLiteral("Read WKB"), inputBar);
    auto* pointButton = new QPushButton(QStringLiteral("Point"), inputBar);
    auto* lineButton = new QPushButton(QStringLiteral("LineString"), inputBar);
    auto* polygonButton = new QPushButton(QStringLiteral("Polygon"), inputBar);
    auto* multiPolygonButton = new QPushButton(QStringLiteral("MultiPolygon"), inputBar);

    inputLayout->addWidget(new QLabel(QStringLiteral("Preset:"), inputBar));
    inputLayout->addWidget(pointButton);
    inputLayout->addWidget(lineButton);
    inputLayout->addWidget(polygonButton);
    inputLayout->addWidget(multiPolygonButton);
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

    auto* wkbEdit = new QTextEdit(leftPanel);
    wkbEdit->setMinimumHeight(150);
    wkbEdit->setPlainText(QString::fromLatin1(PointWkb));

    auto* viewer = new GisViewer(leftPanel);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);

    leftLayout->addWidget(wkbEdit);
    leftLayout->addWidget(viewer, 1);

    auto* detailsView = new QTextEdit(body);
    detailsView->setReadOnly(true);
    detailsView->setMinimumWidth(420);

    bodyLayout->addWidget(leftPanel, 1);
    bodyLayout->addWidget(detailsView);

    rootLayout->addWidget(inputBar);
    rootLayout->addWidget(body, 1);
    window.setCentralWidget(root);

    auto osmLayer = std::make_unique<GisLayerXyzOpenStreetMap>();
    osmLayer->setLocalCacheEnabled(true);
    osmLayer->open();
    viewer->addLayer(osmLayer);

    auto pointLayer = createMemoryLayer(QStringLiteral("WKB Point"), GisShapeType::Point, pointStyle());
    auto lineLayer = createMemoryLayer(QStringLiteral("WKB LineString"), GisShapeType::Polyline, lineStyle());
    auto polygonLayer = createMemoryLayer(QStringLiteral("WKB Polygon"), GisShapeType::Polygon, polygonStyle());

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

    const auto parseWkb = [&]
    {
        try
        {
            const QString inputHex = wkbEdit->toPlainText().trimmed();
            const QByteArray bytes = parseHexWkb(inputHex);
            std::unique_ptr<GisShape> shape = GisWkbReader::read(bytes);
            if (!shape)
                throw std::runtime_error("WKB reader returned an empty shape.");

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
            detailsView->setPlainText(detailsText(inputHex, bytes, *shape, viewExtent));
            viewer->setViewExtent(viewExtent);
            refreshViewer(*viewer);
            window.statusBar()->showMessage(QStringLiteral("GisWkbReader::read parsed %1 from %2 bytes.")
                .arg(shapeTypeName(*shape))
                .arg(bytes.size()));
        }
        catch (const std::exception& ex)
        {
            clearLayers(*pointLayerPtr, *lineLayerPtr, *polygonLayerPtr);
            refreshViewer(*viewer);
            detailsView->setPlainText(QStringLiteral("WKB parse failed:\n%1").arg(QString::fromUtf8(ex.what())));
            window.statusBar()->showMessage(QStringLiteral("WKB parse failed."));
        }
    };

    QObject::connect(readButton, &QPushButton::clicked, viewer, parseWkb);
    QObject::connect(pointButton, &QPushButton::clicked, viewer, [=]
    {
        wkbEdit->setPlainText(QString::fromLatin1(PointWkb));
        parseWkb();
    });
    QObject::connect(lineButton, &QPushButton::clicked, viewer, [=]
    {
        wkbEdit->setPlainText(QString::fromLatin1(LineStringWkb));
        parseWkb();
    });
    QObject::connect(polygonButton, &QPushButton::clicked, viewer, [=]
    {
        wkbEdit->setPlainText(QString::fromLatin1(PolygonWkb));
        parseWkb();
    });
    QObject::connect(multiPolygonButton, &QPushButton::clicked, viewer, [=]
    {
        wkbEdit->setPlainText(QString::fromLatin1(MultiPolygonWkb));
        parseWkb();
    });

    parseWkb();
    window.show();

    return app.exec();
}
