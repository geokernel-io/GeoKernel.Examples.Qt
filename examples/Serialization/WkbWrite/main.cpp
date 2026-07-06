#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPushButton>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>
#include <memory>

#include "CoordinateSystems/Defs/GeographicCoordinateSystem.h"
#include "CoordinateSystems/Defs/KnownCoordinateSystems.h"
#include "CoordinateSystems/Defs/ProjectedCoordinateSystem.h"
#include "CoordinateSystems/Transform/CoordinateTransformer.h"
#include "Layers/GisLayerStyle.h"
#include "Layers/GisLayerVector.h"
#include "Raster/Xyz/PredefinedXyzLayers.h"
#include "Serialization/Wkb/GisWkbWriter.h"
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

enum class GeometryMode
{
    Point = 0,
    Polyline = 1,
    Polygon = 2
};

class MousePressFilter final : public QObject
{
public:
    explicit MousePressFilter(std::function<void()> handler, QObject* parent = nullptr) :
        QObject(parent),
        m_handler(std::move(handler))
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        Q_UNUSED(watched);

        if (event->type() == QEvent::MouseButtonPress)
        {
            const auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton && m_handler)
                m_handler();
        }

        return false;
    }

private:
    std::function<void()> m_handler;
};

GisLayerStyle pointStyle()
{
    GisLayerStyle style;
    style.setPointColor(QStringLiteral("#D95D39"));
    style.setLineColor(QStringLiteral("#8C321D"));
    style.setPointSize(13.0f);
    style.setLineWidth(1.4f);
    return style;
}

GisLayerStyle lineStyle()
{
    GisLayerStyle style;
    style.setLineColor(QStringLiteral("#E4572E"));
    style.setLineWidth(3.4f);
    style.setPointColor(QStringLiteral("#F3A712"));
    style.setPointSize(7.0f);
    return style;
}

GisLayerStyle polygonStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#88D18A80"));
    style.setLineColor(QStringLiteral("#1F7A4D"));
    style.setLineWidth(2.4f);
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

int layerIndexOf(const GisViewer& viewer, const GisLayer* layer)
{
    if (layer == nullptr)
        return -1;

    for (int i = 0; i < viewer.layerCount(); ++i)
    {
        if (viewer.mapLayerAt(i) == layer)
            return i;
    }

    return -1;
}

bool activateLayer(GisViewer& viewer, GisLayerVector* layer, GisViewerTool tool, QStatusBar& statusBar)
{
    const int index = layerIndexOf(viewer, layer);
    if (index < 0)
    {
        statusBar.showMessage(QStringLiteral("Editable layer is not in the viewer."));
        return false;
    }

    if (!viewer.isLayerEditing(index) && !viewer.beginEditLayer(index))
    {
        statusBar.showMessage(QStringLiteral("Editable layer could not enter edit mode."));
        return false;
    }

    if (!viewer.setActiveEditLayerIndex(index))
    {
        statusBar.showMessage(QStringLiteral("Editable layer could not be activated."));
        return false;
    }

    viewer.setActiveTool(tool);
    return true;
}

void clearLayer(GisViewer& viewer, GisLayerVector* layer)
{
    const int index = layerIndexOf(viewer, layer);
    if (index < 0)
        return;

    viewer.rollbackEditLayer(index);
    viewer.beginEditLayer(index);
}

void clearAllLayers(GisViewer& viewer, GisLayerVector* pointLayer, GisLayerVector* lineLayer, GisLayerVector* polygonLayer)
{
    clearLayer(viewer, pointLayer);
    clearLayer(viewer, lineLayer);
    clearLayer(viewer, polygonLayer);
    viewer.invalidateRenderCache(true, true);
    viewer.refreshLayers();
}

const GisShape* lastShape(const GisLayerVector* layer)
{
    if (layer == nullptr || layer->items().isEmpty())
        return nullptr;

    const QVector<GisShape*> shapes = layer->items();
    for (int i = shapes.size() - 1; i >= 0; --i)
    {
        if (shapes[i] != nullptr)
            return shapes[i];
    }

    return nullptr;
}

void keepOnlyLastShape(GisLayerVector& layer)
{
    QVector<GisShape*> shapes = layer.items();
    if (shapes.size() <= 1)
        return;

    while (shapes.size() > 1)
    {
        GisShape* shape = shapes.takeFirst();
        if (shape != nullptr)
            layer.deleteShapeEdit(shape->id());
    }
}

GisShapePoint toWebMercator(const GisShapePoint& lonLat)
{
    const GeographicCoordinateSystem wgs84 = KnownCoordinateSystems::wgs84();
    const ProjectedCoordinateSystem webMercator = KnownCoordinateSystems::webMercator();
    return CoordinateTransformer(wgs84, webMercator).transform(lonLat);
}

GisExtent initialViewExtent()
{
    const GisShapePoint minPoint = toWebMercator(GisShapePoint(-124.8, 32.0));
    const GisShapePoint maxPoint = toWebMercator(GisShapePoint(-114.0, 42.5));
    return GisExtent(minPoint.x(), minPoint.y(), maxPoint.x(), maxPoint.y());
}

QByteArray writeShapeWkb(GeometryMode mode, const GisShape* shape)
{
    if (shape == nullptr)
        return QByteArray();

    switch (mode)
    {
        case GeometryMode::Point:
        {
            const auto* point = dynamic_cast<const GisShapePoint*>(shape);
            return point != nullptr ? GisWkbWriter::writePoint(*point) : QByteArray();
        }
        case GeometryMode::Polyline:
        {
            const auto* polyline = dynamic_cast<const GisShapePolyline*>(shape);
            return polyline != nullptr ? GisWkbWriter::writePolyline(*polyline) : QByteArray();
        }
        case GeometryMode::Polygon:
        {
            const auto* polygon = dynamic_cast<const GisShapePolygon*>(shape);
            return polygon != nullptr ? GisWkbWriter::writePolygon(*polygon) : QByteArray();
        }
    }

    return QByteArray();
}

QString apiName(GeometryMode mode)
{
    switch (mode)
    {
        case GeometryMode::Point:
            return QStringLiteral("GisWkbWriter::writePoint(shape)");
        case GeometryMode::Polyline:
            return QStringLiteral("GisWkbWriter::writePolyline(shape)");
        case GeometryMode::Polygon:
            return QStringLiteral("GisWkbWriter::writePolygon(shape)");
    }

    return QString();
}

QString helpText(GeometryMode mode)
{
    switch (mode)
    {
        case GeometryMode::Point:
            return QStringLiteral("Click on the map to draw a point. WKB is written automatically.");
        case GeometryMode::Polyline:
            return QStringLiteral("Click line vertices, then press Enter or double-click to finish. WKB is written automatically.");
        case GeometryMode::Polygon:
            return QStringLiteral("Click polygon vertices, then press Enter or double-click to finish. WKB is written automatically.");
    }

    return QString();
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1100, 720);
    window.setWindowTitle(QStringLiteral("WkbWrite"));

    auto* root = new QWidget(&window);
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* inputBar = new QWidget(root);
    auto* inputLayout = new QHBoxLayout(inputBar);
    inputLayout->setContentsMargins(6, 4, 6, 4);
    inputLayout->setSpacing(8);

    auto* modeCombo = new QComboBox(inputBar);
    modeCombo->addItem(QStringLiteral("Point"));
    modeCombo->addItem(QStringLiteral("Polyline"));
    modeCombo->addItem(QStringLiteral("Polygon"));

    auto* clearButton = new QPushButton(QStringLiteral("Clear"), inputBar);
    auto* hintLabel = new QLabel(inputBar);

    inputLayout->addWidget(new QLabel(QStringLiteral("Geometry:"), inputBar));
    inputLayout->addWidget(modeCombo);
    inputLayout->addWidget(clearButton);
    inputLayout->addWidget(hintLabel, 1);

    auto* content = new QWidget(root);
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    auto* viewer = new GisViewer(content);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));

    auto* detailsView = new QTextEdit(content);
    detailsView->setReadOnly(true);
    detailsView->setMinimumWidth(450);

    contentLayout->addWidget(viewer, 1);
    contentLayout->addWidget(detailsView);

    rootLayout->addWidget(inputBar);
    rootLayout->addWidget(content, 1);
    window.setCentralWidget(root);

    auto osmLayer = std::make_unique<GisLayerXyzOpenStreetMap>();
    osmLayer->setLocalCacheEnabled(true);
    osmLayer->open();
    viewer->addLayer(osmLayer);

    auto pointLayer = createMemoryLayer(QStringLiteral("Drawn Point"), GisShapeType::Point, pointStyle());
    auto lineLayer = createMemoryLayer(QStringLiteral("Drawn Polyline"), GisShapeType::Polyline, lineStyle());
    auto polygonLayer = createMemoryLayer(QStringLiteral("Drawn Polygon"), GisShapeType::Polygon, polygonStyle());

    GisLayerVector* pointLayerPtr = pointLayer.get();
    GisLayerVector* lineLayerPtr = lineLayer.get();
    GisLayerVector* polygonLayerPtr = polygonLayer.get();

    viewer->addLayer(pointLayer);
    viewer->addLayer(lineLayer);
    viewer->addLayer(polygonLayer);

    bool enforcingSingleShape = false;
    bool drawingSketch = false;

    const auto activeLayer = [&]() -> GisLayerVector*
    {
        const auto mode = static_cast<GeometryMode>(modeCombo->currentIndex());
        if (mode == GeometryMode::Point)
            return pointLayerPtr;
        if (mode == GeometryMode::Polyline)
            return lineLayerPtr;
        return polygonLayerPtr;
    };

    const auto activeTool = [&]() -> GisViewerTool
    {
        const auto mode = static_cast<GeometryMode>(modeCombo->currentIndex());
        if (mode == GeometryMode::Point)
            return GisViewerTool::AddPoint;
        if (mode == GeometryMode::Polyline)
            return GisViewerTool::AddPolyline;
        return GisViewerTool::AddPolygon;
    };

    const auto activateSelectedMode = [&]
    {
        const auto mode = static_cast<GeometryMode>(modeCombo->currentIndex());
        hintLabel->setText(helpText(mode));
        activateLayer(*viewer, activeLayer(), activeTool(), *window.statusBar());
        window.statusBar()->showMessage(helpText(mode));
    };

    const auto writeWkb = [&]
    {
        const auto mode = static_cast<GeometryMode>(modeCombo->currentIndex());
        const GisShape* shape = lastShape(activeLayer());
        const QByteArray wkb = writeShapeWkb(mode, shape);

        if (wkb.isEmpty())
        {
            detailsView->setPlainText(QStringLiteral("%1\n\nDraw a geometry first. WKB will be written automatically.")
                .arg(apiName(mode)));
            window.statusBar()->showMessage(QStringLiteral("No drawn geometry is available for the selected type."));
            return;
        }

        const QString hex = QString::fromLatin1(wkb.toHex(' ').toUpper());

        QStringList details;
        details << QStringLiteral("WkbWrite sample");
        details << QStringLiteral("");
        details << QStringLiteral("API");
        details << apiName(mode);
        details << QStringLiteral("");
        details << QStringLiteral("Selected geometry");
        details << modeCombo->currentText();
        details << QStringLiteral("Layer feature count: %1").arg(activeLayer() != nullptr ? activeLayer()->count() : 0);
        details << QStringLiteral("");
        details << QStringLiteral("Output WKB");
        details << QStringLiteral("Byte count: %1").arg(wkb.size());
        details << QStringLiteral("Endian: little endian");
        details << QStringLiteral("");
        details << QStringLiteral("Hex view");
        details << hex;
        details << QStringLiteral("");
        details << QStringLiteral("Workflow");
        details << QStringLiteral("1. Choose geometry type.");
        details << QStringLiteral("2. Draw geometry on map.");
        details << QStringLiteral("3. WKB binary is written automatically when drawing finishes.");

        detailsView->setPlainText(details.join(QStringLiteral("\n")));
        window.statusBar()->showMessage(QStringLiteral("%1 wrote %2 WKB bytes.")
            .arg(apiName(mode))
            .arg(wkb.size()));
    };

    QObject::connect(modeCombo, &QComboBox::currentIndexChanged, viewer, [&](int)
    {
        drawingSketch = false;
        clearAllLayers(*viewer, pointLayerPtr, lineLayerPtr, polygonLayerPtr);
        detailsView->clear();
        activateSelectedMode();
    });

    QObject::connect(clearButton, &QPushButton::clicked, viewer, [&]
    {
        drawingSketch = false;
        clearAllLayers(*viewer, pointLayerPtr, lineLayerPtr, polygonLayerPtr);
        detailsView->clear();
        activateSelectedMode();
        window.statusBar()->showMessage(QStringLiteral("Drawn geometries cleared."));
    });

    auto* mousePressFilter = new MousePressFilter([&]
    {
        if (viewer->activeTool() != activeTool())
            return;

        GisLayerVector* layer = activeLayer();
        if (layer == nullptr)
            return;

        const auto mode = static_cast<GeometryMode>(modeCombo->currentIndex());
        const bool startsNewGeometry =
            mode == GeometryMode::Point ||
            !drawingSketch;

        if (!startsNewGeometry)
            return;

        if (layer->count() > 0)
        {
            clearLayer(*viewer, layer);
            activateLayer(*viewer, layer, activeTool(), *window.statusBar());
            detailsView->clear();
            viewer->invalidateRenderCache(true, true);
            viewer->refreshLayers();
        }

        if (mode != GeometryMode::Point)
            drawingSketch = true;
    }, viewer);
    viewer->installEventFilter(mousePressFilter);

    QObject::connect(viewer, &GisViewer::layerEditStateChanged, viewer, [&](GisLayer* layer)
    {
        if (layer != activeLayer())
            return;

        drawingSketch = false;

        if (!enforcingSingleShape && activeLayer() != nullptr)
        {
            enforcingSingleShape = true;
            keepOnlyLastShape(*activeLayer());
            enforcingSingleShape = false;
        }

        viewer->invalidateRenderCache(true, true);
        viewer->refreshLayers();
        writeWkb();
    });

    activateSelectedMode();
    window.show();
    viewer->setViewExtent(initialViewExtent());

    return app.exec();
}
