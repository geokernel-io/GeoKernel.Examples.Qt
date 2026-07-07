#include <QApplication>
#include <QColor>
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
#include "Shapes/GisShapePolyline.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::CoordinateSystems::Defs;
using namespace GeoKernel::Core::CoordinateSystems::Transform;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Serialization::Wkt;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Formats::Raster::Xyz;
using namespace GeoKernel::Viewer;

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

std::unique_ptr<GisLayerVector> createPolylineLayer()
{
    auto layer = GisLayerVector::createInMemory(
        QStringLiteral("WKT LineString"),
        GisShapeType::Polyline,
        GisExtent(-180.0, -90.0, 180.0, 90.0));
    layer->setCoordinateSystem(std::make_shared<GeographicCoordinateSystem>(KnownCoordinateSystems::wgs84()));
    layer->style() = lineStyle();
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

void replacePolyline(GisLayerVector& layer, const GisShapePolyline& polyline)
{
    clearLayer(layer);

    if (!layer.isEditing())
        layer.beginEdit();

    QHash<QString, QVariant> attributes;
    attributes.insert(QStringLiteral("name"), QStringLiteral("LINESTRING"));
    layer.addShapeEdit(std::make_unique<GisShapePolyline>(polyline), attributes);
    layer.commitEdit();
    layer.clearEditHistory();
}

GisShapePoint toWebMercator(const GisShapePoint& lonLat)
{
    const GeographicCoordinateSystem wgs84 = KnownCoordinateSystems::wgs84();
    const ProjectedCoordinateSystem webMercator = KnownCoordinateSystems::webMercator();
    return CoordinateTransformer(wgs84, webMercator).transform(lonLat);
}

GisExtent projectedExtent(const GisShapePolyline& polyline)
{
    bool hasPoint = false;
    GisExtent extent = GisExtent::empty();

    for (const QVector<GisShapePoint>& part : polyline.parts())
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

    const double paddingX = qMax(250000.0, extent.width() * 0.25);
    const double paddingY = qMax(250000.0, extent.height() * 0.25);
    return extent.inflate(paddingX, paddingY);
}

int vertexCount(const GisShapePolyline& polyline)
{
    int count = 0;
    for (const QVector<GisShapePoint>& part : polyline.parts())
        count += part.size();
    return count;
}

QString detailsText(const QString& inputWkt, const GisShapePolyline& polyline, const GisExtent& webMercatorExtent)
{
    QStringList details;
    details << QStringLiteral("WktReadPolyline sample");
    details << QStringLiteral("");
    details << QStringLiteral("API");
    details << QStringLiteral("GisWktReader::readLineString(wkt)");
    details << QStringLiteral("");
    details << QStringLiteral("Input WKT");
    details << inputWkt;
    details << QStringLiteral("");
    details << QStringLiteral("Parsed line");
    details << QStringLiteral("Parts: %1").arg(polyline.parts().size());
    details << QStringLiteral("Vertices: %1").arg(vertexCount(polyline));
    details << QStringLiteral("Lon/lat extent: %1").arg(polyline.extent().toString());
    details << QStringLiteral("");
    details << QStringLiteral("Displayed over OSM");
    details << QStringLiteral("Layer CRS: EPSG:4326");
    details << QStringLiteral("Viewer/OSM CRS: EPSG:3857");
    details << QStringLiteral("WebMercator view extent: %1").arg(webMercatorExtent.toString());
    details << QStringLiteral("");
    details << QStringLiteral("Round-trip WKT");
    details << GisWktWriter::writePolyline(polyline);
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
    window.resize(1100, 720);
    window.setWindowTitle(QStringLiteral("WktReadPolyline"));

    auto* root = new QWidget(&window);
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* inputBar = new QWidget(root);
    auto* inputLayout = new QHBoxLayout(inputBar);
    inputLayout->setContentsMargins(6, 4, 6, 4);
    inputLayout->setSpacing(8);

    auto* wktEdit = new QLineEdit(
        QStringLiteral("LINESTRING(-122.4194 37.7749, -121.8863 37.3382, -121.4944 38.5816, -120.7401 37.6391)"),
        inputBar);
    auto* readButton = new QPushButton(QStringLiteral("Read LineString"), inputBar);
    auto* resetButton = new QPushButton(QStringLiteral("Reset"), inputBar);

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
    detailsView->setMinimumWidth(380);

    contentLayout->addWidget(viewer, 1);
    contentLayout->addWidget(detailsView);

    rootLayout->addWidget(inputBar);
    rootLayout->addWidget(content, 1);
    window.setCentralWidget(root);

    auto osmLayer = std::make_unique<GisLayerXyzOpenStreetMap>();
    osmLayer->setLocalCacheEnabled(true);
    osmLayer->open();
    viewer->addLayer(osmLayer);

    auto polylineLayer = createPolylineLayer();
    GisLayerVector* polylineLayerPtr = polylineLayer.get();
    viewer->addLayer(polylineLayer);

    const auto parseLineString = [&]
    {
        try
        {
            const QString inputWkt = wktEdit->text().trimmed();
            const GisShapePolyline polyline = GisWktReader::readLineString(inputWkt);
            const GisExtent viewExtent = projectedExtent(polyline);

            replacePolyline(*polylineLayerPtr, polyline);
            detailsView->setPlainText(detailsText(inputWkt, polyline, viewExtent));
            viewer->setViewExtent(viewExtent);
            refreshViewer(*viewer);

            window.statusBar()->showMessage(QStringLiteral("GisWktReader::readLineString parsed %1 vertices.")
                .arg(vertexCount(polyline)));
        }
        catch (const std::exception& ex)
        {
            clearLayer(*polylineLayerPtr);
            refreshViewer(*viewer);
            detailsView->setPlainText(QStringLiteral("WKT parse failed:\n%1").arg(QString::fromUtf8(ex.what())));
            window.statusBar()->showMessage(QStringLiteral("WKT parse failed."));
        }
    };

    QObject::connect(readButton, &QPushButton::clicked, viewer, parseLineString);
    QObject::connect(wktEdit, &QLineEdit::returnPressed, viewer, parseLineString);
    QObject::connect(resetButton, &QPushButton::clicked, viewer, [=]
    {
        wktEdit->setText(QStringLiteral("LINESTRING(-122.4194 37.7749, -121.8863 37.3382, -121.4944 38.5816, -120.7401 37.6391)"));
        parseLineString();
    });

    parseLineString();
    window.show();

    return app.exec();
}
