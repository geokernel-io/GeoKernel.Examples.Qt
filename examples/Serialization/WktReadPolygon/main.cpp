#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

#include "CoordinateSystems/Defs/GeographicCoordinateSystem.h"
#include "CoordinateSystems/Defs/KnownCoordinateSystems.h"
#include "CoordinateSystems/Defs/ProjectedCoordinateSystem.h"
#include "CoordinateSystems/Transform/CoordinateTransformer.h"
#include "Layers/GisLayerStyle.h"
#include "Layers/GisLayerVector.h"
#include "Raster/Xyz/PredefinedXyzLayers.h"
#include "Serialization/Wkt/GisWktReader.h"
#include "Serialization/Wkt/GisWktWriter.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShapePoint.h"
#include "Shapes/GisShapePolygon.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::CoordinateSystems::Defs;
using namespace GeoKernel::Core::CoordinateSystems::Transform;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Serialization::Wkt;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Formats::Raster::Xyz;
using namespace GeoKernel::Viewer;

constexpr const char* PolygonWkt =
    "POLYGON((-123.25 37.15, -122.15 36.95, -121.55 37.65, -122.05 38.35, -123.05 38.15, -123.25 37.15))";

constexpr const char* MultiPolygonWkt =
    "MULTIPOLYGON(((-123.25 37.15, -122.25 36.95, -121.85 37.65, -122.45 38.20, -123.15 37.95, -123.25 37.15)),"
    "((-121.60 36.75, -120.70 36.70, -120.45 37.35, -121.25 37.65, -121.60 36.75)))";

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

std::unique_ptr<GisLayerVector> createPolygonLayer()
{
    auto layer = GisLayerVector::createInMemory(
        QStringLiteral("WKT Polygon"),
        GisShapeType::Polygon,
        GisExtent(-180.0, -90.0, 180.0, 90.0));
    layer->setCoordinateSystem(std::make_shared<GeographicCoordinateSystem>(KnownCoordinateSystems::wgs84()));
    layer->style() = polygonStyle();
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

void replacePolygon(GisLayerVector& layer, const GisShapePolygon& polygon, const QString& label)
{
    clearLayer(layer);

    if (!layer.isEditing())
        layer.beginEdit();

    QHash<QString, QVariant> attributes;
    attributes.insert(QStringLiteral("name"), label);
    layer.addShapeEdit(std::make_unique<GisShapePolygon>(polygon), attributes);
    layer.commitEdit();
    layer.clearEditHistory();
}

GisShapePoint toWebMercator(const GisShapePoint& lonLat)
{
    const GeographicCoordinateSystem wgs84 = KnownCoordinateSystems::wgs84();
    const ProjectedCoordinateSystem webMercator = KnownCoordinateSystems::webMercator();
    return CoordinateTransformer(wgs84, webMercator).transform(lonLat);
}

GisExtent projectedExtent(const GisShapePolygon& polygon)
{
    bool hasPoint = false;
    GisExtent extent = GisExtent::empty();

    for (const QVector<GisShapePoint>& part : polygon.parts())
    {
        for (const GisShapePoint& point : part)
        {
            const GisShapePoint projected = toWebMercator(point);
            const GisExtent pointExtent(projected.x(), projected.y(), projected.x(), projected.y());
            extent = hasPoint ? extent.expand(pointExtent) : pointExtent;
            hasPoint = true;
        }
    }

    if (!hasPoint)
        return GisExtent(-20037508.342789244, -20037508.342789244, 20037508.342789244, 20037508.342789244);

    const double paddingX = qMax(300000.0, extent.width() * 0.35);
    const double paddingY = qMax(300000.0, extent.height() * 0.35);
    return extent.inflate(paddingX, paddingY);
}

int vertexCount(const GisShapePolygon& polygon)
{
    int count = 0;
    for (const QVector<GisShapePoint>& part : polygon.parts())
        count += part.size();
    return count;
}

QString detailsText(
    const QString& inputWkt,
    const QString& apiName,
    const GisShapePolygon& polygon,
    const GisExtent& webMercatorExtent)
{
    QStringList details;
    details << QStringLiteral("WktReadPolygon sample");
    details << QStringLiteral("");
    details << QStringLiteral("API");
    details << apiName;
    details << QStringLiteral("");
    details << QStringLiteral("Input WKT");
    details << inputWkt;
    details << QStringLiteral("");
    details << QStringLiteral("Parsed polygon");
    details << QStringLiteral("Parts/rings: %1").arg(polygon.parts().size());
    details << QStringLiteral("Vertices: %1").arg(vertexCount(polygon));
    details << QStringLiteral("Lon/lat extent: %1").arg(polygon.extent().toString());
    details << QStringLiteral("Centroid: %1, %2")
        .arg(polygon.centroid().x(), 0, 'f', 6)
        .arg(polygon.centroid().y(), 0, 'f', 6);
    details << QStringLiteral("");
    details << QStringLiteral("Displayed over OSM");
    details << QStringLiteral("Layer CRS: EPSG:4326");
    details << QStringLiteral("Viewer/OSM CRS: EPSG:3857");
    details << QStringLiteral("WebMercator view extent: %1").arg(webMercatorExtent.toString());
    details << QStringLiteral("");
    details << QStringLiteral("Round-trip WKT");
    details << GisWktWriter::writePolygon(polygon);
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
    window.resize(1120, 720);
    window.setWindowTitle(QStringLiteral("WktReadPolygon"));

    auto* root = new QWidget(&window);
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* inputBar = new QWidget(root);
    auto* inputLayout = new QHBoxLayout(inputBar);
    inputLayout->setContentsMargins(6, 4, 6, 4);
    inputLayout->setSpacing(8);

    auto* modeCombo = new QComboBox(inputBar);
    modeCombo->addItem(QStringLiteral("Polygon"));
    modeCombo->addItem(QStringLiteral("MultiPolygon"));

    auto* wktEdit = new QLineEdit(QString::fromLatin1(PolygonWkt), inputBar);
    auto* readButton = new QPushButton(QStringLiteral("Read Polygon"), inputBar);
    auto* resetButton = new QPushButton(QStringLiteral("Reset"), inputBar);

    inputLayout->addWidget(new QLabel(QStringLiteral("Mode:"), inputBar));
    inputLayout->addWidget(modeCombo);
    inputLayout->addWidget(new QLabel(QStringLiteral("WKT:"), inputBar));
    inputLayout->addWidget(wktEdit, 1);
    inputLayout->addWidget(readButton);
    inputLayout->addWidget(resetButton);

    auto* content = new QWidget(root);
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    auto* viewer = new GisViewer(content);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);

    auto* detailsView = new QTextEdit(content);
    detailsView->setReadOnly(true);
    detailsView->setMinimumWidth(400);

    contentLayout->addWidget(viewer, 1);
    contentLayout->addWidget(detailsView);

    rootLayout->addWidget(inputBar);
    rootLayout->addWidget(content, 1);
    window.setCentralWidget(root);

    auto osmLayer = std::make_unique<GisLayerXyzOpenStreetMap>();
    osmLayer->setLocalCacheEnabled(true);
    osmLayer->open();
    viewer->addLayer(osmLayer);

    auto polygonLayer = createPolygonLayer();
    GisLayerVector* polygonLayerPtr = polygonLayer.get();
    viewer->addLayer(polygonLayer);

    const auto parsePolygon = [&]
    {
        try
        {
            const QString inputWkt = wktEdit->text().trimmed();
            const bool multiPolygon = modeCombo->currentIndex() == 1;
            const QString apiName = multiPolygon
                ? QStringLiteral("GisWktReader::readMultiPolygon(wkt)")
                : QStringLiteral("GisWktReader::readPolygon(wkt)");
            const GisShapePolygon polygon = multiPolygon
                ? GisWktReader::readMultiPolygon(inputWkt)
                : GisWktReader::readPolygon(inputWkt);
            const GisExtent viewExtent = projectedExtent(polygon);

            replacePolygon(*polygonLayerPtr, polygon, multiPolygon ? QStringLiteral("MULTIPOLYGON") : QStringLiteral("POLYGON"));
            detailsView->setPlainText(detailsText(inputWkt, apiName, polygon, viewExtent));
            viewer->setViewExtent(viewExtent);
            refreshViewer(*viewer);

            window.statusBar()->showMessage(QStringLiteral("%1 parsed %2 rings and %3 vertices.")
                .arg(apiName)
                .arg(polygon.parts().size())
                .arg(vertexCount(polygon)));
        }
        catch (const std::exception& ex)
        {
            clearLayer(*polygonLayerPtr);
            refreshViewer(*viewer);
            detailsView->setPlainText(QStringLiteral("WKT parse failed:\n%1").arg(QString::fromUtf8(ex.what())));
            window.statusBar()->showMessage(QStringLiteral("WKT parse failed."));
        }
    };

    QObject::connect(readButton, &QPushButton::clicked, viewer, parsePolygon);
    QObject::connect(wktEdit, &QLineEdit::returnPressed, viewer, parsePolygon);
    QObject::connect(resetButton, &QPushButton::clicked, viewer, [=]
    {
        wktEdit->setText(modeCombo->currentIndex() == 1
            ? QString::fromLatin1(MultiPolygonWkt)
            : QString::fromLatin1(PolygonWkt));
        parsePolygon();
    });
    QObject::connect(modeCombo, &QComboBox::currentIndexChanged, viewer, [=](int index)
    {
        wktEdit->setText(index == 1
            ? QString::fromLatin1(MultiPolygonWkt)
            : QString::fromLatin1(PolygonWkt));
        readButton->setText(index == 1 ? QStringLiteral("Read MultiPolygon") : QStringLiteral("Read Polygon"));
        parsePolygon();
    });

    parsePolygon();
    window.show();

    return app.exec();
}
