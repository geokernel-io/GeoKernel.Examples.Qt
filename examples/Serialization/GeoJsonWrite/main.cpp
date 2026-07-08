#include <QApplication>
#include <QColor>
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
#include "Serialization/GeoJson/GisGeoJsonWriter.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShape.h"
#include "Shapes/GisShapePoint.h"
#include "Shapes/GisShapePolygon.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::CoordinateSystems::Defs;
using namespace GeoKernel::Core::CoordinateSystems::Transform;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Serialization::GeoJson;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Formats::Raster::Xyz;
using namespace GeoKernel::Viewer;


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

GisLayerStyle polygonStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#88D18A80"));
    style.setLineColor(QStringLiteral("#1F7A4D"));
    style.setLineWidth(2.4f);
    return style;
}

std::unique_ptr<GisLayerVector> createPolygonLayer()
{
    auto layer = GisLayerVector::createInMemory(
        QStringLiteral("Drawn Polygon"),
        GisShapeType::Polygon,
        GisExtent(-180.0, -90.0, 180.0, 90.0));
    layer->setCoordinateSystem(std::make_shared<GeographicCoordinateSystem>(KnownCoordinateSystems::wgs84()));
    layer->style() = polygonStyle();
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

bool activatePolygonTool(GisViewer& viewer, GisLayerVector* layer, QStatusBar& statusBar)
{
    const int index = layerIndexOf(viewer, layer);
    if (index < 0)
    {
        statusBar.showMessage(QStringLiteral("Editable polygon layer is not in the viewer."));
        return false;
    }

    if (!viewer.isLayerEditing(index) && !viewer.beginEditLayer(index))
    {
        statusBar.showMessage(QStringLiteral("Polygon layer could not enter edit mode."));
        return false;
    }

    if (!viewer.setActiveEditLayerIndex(index))
    {
        statusBar.showMessage(QStringLiteral("Polygon layer could not be activated."));
        return false;
    }

    viewer.setActiveTool(GisViewerTool::AddPolygon);
    return true;
}

void clearLayer(GisViewer& viewer, GisLayerVector* layer)
{
    const int index = layerIndexOf(viewer, layer);
    if (index < 0)
        return;

    viewer.rollbackEditLayer(index);
    viewer.beginEditLayer(index);
    viewer.invalidateRenderCache(true, true);
    viewer.refreshLayers();
}

const GisShapePolygon* lastPolygon(const GisLayerVector* layer)
{
    if (layer == nullptr || layer->items().isEmpty())
        return nullptr;

    const QVector<GisShape*> shapes = layer->items();
    for (int i = shapes.size() - 1; i >= 0; --i)
    {
        if (const auto* polygon = dynamic_cast<const GisShapePolygon*>(shapes[i]))
            return polygon;
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

int vertexCount(const GisShapePolygon& polygon)
{
    int count = 0;
    for (const QVector<GisShapePoint>& part : polygon.parts())
        count += part.size();
    return count;
}

QString detailsText(const GisShapePolygon& polygon, const QString& geoJson)
{
    QStringList details;
    details << QStringLiteral("GeoJsonWrite sample");
    details << QStringLiteral("");
    details << QStringLiteral("API");
    details << QStringLiteral("GisGeoJsonWriter::writePolygonString(shape)");
    details << QStringLiteral("");
    details << QStringLiteral("Drawn polygon");
    details << QStringLiteral("Rings: %1").arg(polygon.parts().size());
    details << QStringLiteral("Vertices: %1").arg(vertexCount(polygon));
    details << QStringLiteral("Lon/lat extent: %1").arg(polygon.extent().toString());
    details << QStringLiteral("");
    details << QStringLiteral("Output GeoJSON");
    details << geoJson;
    details << QStringLiteral("");
    details << QStringLiteral("Workflow");
    details << QStringLiteral("1. Click polygon vertices on the map.");
    details << QStringLiteral("2. Press Enter or double-click to finish.");
    details << QStringLiteral("3. GeoJSON is written automatically.");
    return details.join(QStringLiteral("\n"));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1100, 720);
    window.setWindowTitle(QStringLiteral("GeoJsonWrite"));

    auto* root = new QWidget(&window);
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* inputBar = new QWidget(root);
    auto* inputLayout = new QHBoxLayout(inputBar);
    inputLayout->setContentsMargins(6, 4, 6, 4);
    inputLayout->setSpacing(8);

    auto* clearButton = new QPushButton(QStringLiteral("Clear"), inputBar);
    auto* hintLabel = new QLabel(
        QStringLiteral("Click polygon vertices, then press Enter or double-click to finish. GeoJSON is written automatically."),
        inputBar);

    inputLayout->addWidget(clearButton);
    inputLayout->addWidget(hintLabel, 1);

    auto* content = new QWidget(root);
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    auto* viewer = new GisViewer(content);

    auto* detailsView = new QTextEdit(content);
    detailsView->setReadOnly(true);
    detailsView->setMinimumWidth(430);
    detailsView->setPlainText(QStringLiteral(
        "GisGeoJsonWriter::writePolygonString(shape)\n\n"
        "Draw a polygon on the map. The GeoJSON string will appear here."));

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

    bool enforcingSingleShape = false;
    bool drawingSketch = false;

    const auto activate = [&]
    {
        activatePolygonTool(*viewer, polygonLayerPtr, *window.statusBar());
        window.statusBar()->showMessage(QStringLiteral("Add Polygon active. Finish with Enter or double-click."));
    };

    const auto writeGeoJson = [&]
    {
        const GisShapePolygon* polygon = lastPolygon(polygonLayerPtr);
        if (polygon == nullptr)
        {
            detailsView->setPlainText(QStringLiteral(
                "GisGeoJsonWriter::writePolygonString(shape)\n\n"
                "Draw a polygon first. GeoJSON will be written automatically."));
            return;
        }

        const QString geoJson = GisGeoJsonWriter::writePolygonString(*polygon);
        detailsView->setPlainText(detailsText(*polygon, geoJson));
        window.statusBar()->showMessage(QStringLiteral("GisGeoJsonWriter::writePolygonString wrote polygon GeoJSON."));
    };

    QObject::connect(clearButton, &QPushButton::clicked, viewer, [&]
    {
        drawingSketch = false;
        clearLayer(*viewer, polygonLayerPtr);
        detailsView->setPlainText(QStringLiteral(
            "GisGeoJsonWriter::writePolygonString(shape)\n\n"
            "Draw a polygon on the map. The GeoJSON string will appear here."));
        activate();
        window.statusBar()->showMessage(QStringLiteral("Polygon cleared."));
    });

    auto* mousePressFilter = new MousePressFilter([&]
    {
        if (viewer->activeTool() != GisViewerTool::AddPolygon)
            return;

        if (polygonLayerPtr == nullptr)
            return;

        if (drawingSketch)
            return;

        if (polygonLayerPtr->count() > 0)
        {
            clearLayer(*viewer, polygonLayerPtr);
            activate();
            detailsView->clear();
        }

        drawingSketch = true;
    }, viewer);
    viewer->installEventFilter(mousePressFilter);

    QObject::connect(viewer, &GisViewer::layerEditStateChanged, viewer, [&](GisLayer* layer)
    {
        if (layer != polygonLayerPtr)
            return;

        drawingSketch = false;

        if (!enforcingSingleShape && polygonLayerPtr != nullptr)
        {
            enforcingSingleShape = true;
            keepOnlyLastShape(*polygonLayerPtr);
            enforcingSingleShape = false;
        }

        viewer->invalidateRenderCache(true, true);
        viewer->refreshLayers();
        writeGeoJson();
    });

    activate();
    window.show();
    viewer->setViewExtent(initialViewExtent());

    return app.exec();
}
