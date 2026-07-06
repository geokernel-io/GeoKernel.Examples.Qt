#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
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
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Serialization::Wkt;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Core::CoordinateSystems::Defs;
using namespace GeoKernel::Core::CoordinateSystems::Transform;
using namespace GeoKernel::Formats::Raster::Xyz;
using namespace GeoKernel::Viewer;

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

std::unique_ptr<GisLayerVector> createPointLayer()
{
    auto layer = GisLayerVector::createInMemory(
        QStringLiteral("WKT Point"),
        GisShapeType::Point,
        GisExtent(-180.0, -90.0, 180.0, 90.0));
    layer->setCoordinateSystem(std::make_shared<GeographicCoordinateSystem>(KnownCoordinateSystems::wgs84()));
    layer->style() = pointStyle();
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

void replacePoint(GisLayerVector& layer, const GisShapePoint& point)
{
    clearLayer(layer);

    if (!layer.isEditing())
        layer.beginEdit();

    QHash<QString, QVariant> attributes;
    attributes.insert(QStringLiteral("name"), QStringLiteral("POINT"));
    layer.addShapeEdit(std::make_unique<GisShapePoint>(point), attributes);
    layer.commitEdit();
    layer.clearEditHistory();
}

GisShapePoint toWebMercator(const GisShapePoint& lonLat)
{
    const GeographicCoordinateSystem wgs84 = KnownCoordinateSystems::wgs84();
    const ProjectedCoordinateSystem webMercator = KnownCoordinateSystems::webMercator();
    return CoordinateTransformer(wgs84, webMercator).transform(lonLat);
}

QString detailsText(const QString& inputWkt, const GisShapePoint& point, const GisShapePoint& projectedPoint)
{
    QStringList details;
    details << QStringLiteral("WktReadPoint sample");
    details << QStringLiteral("");
    details << QStringLiteral("API");
    details << QStringLiteral("GisWktReader::readPoint(wkt)");
    details << QStringLiteral("");
    details << QStringLiteral("Input WKT");
    details << inputWkt;
    details << QStringLiteral("");
    details << QStringLiteral("Parsed lon/lat point");
    details << QStringLiteral("X: %1").arg(point.x(), 0, 'f', 6);
    details << QStringLiteral("Y: %1").arg(point.y(), 0, 'f', 6);
    details << QStringLiteral("");
    details << QStringLiteral("Displayed over OSM");
    details << QStringLiteral("Layer CRS: EPSG:4326");
    details << QStringLiteral("Viewer/OSM CRS: EPSG:3857");
    details << QStringLiteral("WebMercator X: %1").arg(projectedPoint.x(), 0, 'f', 3);
    details << QStringLiteral("WebMercator Y: %1").arg(projectedPoint.y(), 0, 'f', 3);
    details << QStringLiteral("");
    details << QStringLiteral("Round-trip WKT");
    details << GisWktWriter::writePoint(point);
    return details.join(QStringLiteral("\n"));
}

GisExtent pointViewExtent(const GisShapePoint& projectedPoint)
{
    return GisExtent(
        projectedPoint.x() - 2500000.0,
        projectedPoint.y() - 1800000.0,
        projectedPoint.x() + 2500000.0,
        projectedPoint.y() + 1800000.0);
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
    window.resize(1000, 700);
    window.setWindowTitle(QStringLiteral("WktReadPoint"));

    auto* root = new QWidget(&window);
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* inputBar = new QWidget(root);
    auto* inputLayout = new QHBoxLayout(inputBar);
    inputLayout->setContentsMargins(6, 4, 6, 4);
    inputLayout->setSpacing(8);

    auto* wktEdit = new QLineEdit(QStringLiteral("POINT(-122.4194 37.7749)"), inputBar);
    auto* readButton = new QPushButton(QStringLiteral("Read Point"), inputBar);
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
    detailsView->setMinimumWidth(340);

    contentLayout->addWidget(viewer, 1);
    contentLayout->addWidget(detailsView);

    rootLayout->addWidget(inputBar);
    rootLayout->addWidget(content, 1);
    window.setCentralWidget(root);

    auto osmLayer = std::make_unique<GisLayerXyzOpenStreetMap>();
    osmLayer->setLocalCacheEnabled(true);
    osmLayer->open();
    viewer->addLayer(osmLayer);

    auto pointLayer = createPointLayer();
    GisLayerVector* pointLayerPtr = pointLayer.get();
    viewer->addLayer(pointLayer);

    const auto parsePoint = [&]
    {
        try
        {
            const QString inputWkt = wktEdit->text().trimmed();
            const GisShapePoint point = GisWktReader::readPoint(inputWkt);
            const GisShapePoint projectedPoint = toWebMercator(point);
            replacePoint(*pointLayerPtr, point);
            detailsView->setPlainText(detailsText(inputWkt, point, projectedPoint));
            viewer->setViewExtent(pointViewExtent(projectedPoint));
            refreshViewer(*viewer);
            window.statusBar()->showMessage(QStringLiteral("GisWktReader::readPoint parsed lon/lat POINT(%1 %2) over OSM.")
                .arg(point.x(), 0, 'f', 6)
                .arg(point.y(), 0, 'f', 6));
        }
        catch (const std::exception& ex)
        {
            clearLayer(*pointLayerPtr);
            refreshViewer(*viewer);
            detailsView->setPlainText(QStringLiteral("WKT parse failed:\n%1").arg(QString::fromUtf8(ex.what())));
            window.statusBar()->showMessage(QStringLiteral("WKT parse failed."));
        }
    };

    QObject::connect(readButton, &QPushButton::clicked, viewer, parsePoint);
    QObject::connect(wktEdit, &QLineEdit::returnPressed, viewer, parsePoint);
    QObject::connect(resetButton, &QPushButton::clicked, viewer, [=]
    {
        wktEdit->setText(QStringLiteral("POINT(-122.4194 37.7749)"));
        parsePoint();
    });

    parsePoint();
    window.show();

    return app.exec();
}
