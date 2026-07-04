#include <memory>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSize>
#include <QSpinBox>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QToolBar>
#include <QVariant>
#include <QVector>

#include "Layers/Defs/GisAttributeDefinition.h"
#include "Layers/Defs/GisAttributeType.h"
#include "Layers/GisLayerStyle.h"
#include "Layers/GisLayerVector.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShape.h"
#include "Shapes/GisShapePoint.h"
#include "Shapes/GisShapePolygon.h"
#include "Shapes/GisShapePolyline.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Layers::Defs;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Viewer;

QIcon sampleIcon(const QString& fileName)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString path = QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/images/%1").arg(fileName)));
    QIcon icon;

    for (const auto mode : { QIcon::Normal, QIcon::Active, QIcon::Selected, QIcon::Disabled })
    {
        icon.addFile(path, QSize(), mode, QIcon::Off);
        icon.addFile(path, QSize(), mode, QIcon::On);
    }

    return icon;
}

QString sampleDataPath(const QString& relativePath)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/data/%1").arg(relativePath)));
}

bool loadLayer(GisViewer& viewer, const QString& path)
{
    QString errorMessage;
    if (viewer.addLayerFromPath(path, &errorMessage))
        return true;

    QMessageBox::critical(
        nullptr,
        QStringLiteral("DeleteVertex"),
        QStringLiteral("Layer could not be loaded:\n%1").arg(errorMessage.isEmpty() ? path : errorMessage));
    return false;
}

GisLayerStyle worldStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#D8E5E1"));
    style.setFillOpacity(210);
    style.setLineColor(QStringLiteral("#6F8883"));
    style.setLineWidth(0.7f);
    return style;
}

GisLayerStyle editStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#F2D27A"));
    style.setFillOpacity(150);
    style.setLineColor(QStringLiteral("#2B6F8E"));
    style.setLineWidth(3.0f);
    style.setSelectedLineColor(QStringLiteral("#F59E0B"));
    style.setSelectedLineWidth(5.0f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("Name"));
    style.setLabelFontSize(10.0f);
    style.setLabelHaloEnabled(true);
    style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
    style.setLabelHaloWidth(2.0f);
    style.setLabelOffsetY(-12.0f);
    style.setLabelAllowOverlap(true);
    return style;
}

std::unique_ptr<GisLayerVector> createPolygonLayer()
{
    auto layer = GisLayerVector::createInMemory(
        QStringLiteral("Editable Polygons"),
        GisShapeType::Polygon,
        GisExtent(-180.0, -90.0, 180.0, 90.0));
    layer->addAttributeDefinition({ QStringLiteral("Name"), GisAttributeType::String, 64, 0 });
    layer->style() = editStyle();
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

QVector<GisShapePoint> editablePart(const GisShape& shape, int partIndex)
{
    if (const auto* polyline = dynamic_cast<const GisShapePolyline*>(&shape))
    {
        const auto& parts = polyline->parts();
        return partIndex >= 0 && partIndex < parts.size() ? parts[partIndex] : QVector<GisShapePoint>();
    }

    if (const auto* polygon = dynamic_cast<const GisShapePolygon*>(&shape))
    {
        const auto& parts = polygon->parts();
        if (partIndex < 0 || partIndex >= parts.size())
            return {};

        QVector<GisShapePoint> part = parts[partIndex];
        if (part.size() >= 2 &&
            qAbs(part.first().x() - part.last().x()) < 1e-12 &&
            qAbs(part.first().y() - part.last().y()) < 1e-12)
        {
            part.removeLast();
        }

        return part;
    }

    return {};
}

int vertexCount(const GisShape& shape)
{
    int total = 0;
    if (const auto* polygon = dynamic_cast<const GisShapePolygon*>(&shape))
    {
        for (int i = 0; i < polygon->parts().size(); ++i)
            total += editablePart(shape, i).size();
    }
    else if (const auto* polyline = dynamic_cast<const GisShapePolyline*>(&shape))
    {
        for (const QVector<GisShapePoint>& part : polyline->parts())
            total += part.size();
    }

    return total;
}

QString selectedFeatureText(const GisViewer& viewer, const GisLayerVector* expectedLayer, int partIndex, int vertexIndex)
{
    const QVector<FeatureHitTestResult> selected = viewer.selectedFeatures();
    QStringList lines;
    lines << QStringLiteral("Usage:");
    lines << QStringLiteral("- Edit Vertices: click a vertex to make it the active vertex.");
    lines << QStringLiteral("- Delete Selected Vertex calls deleteSelectedVertexFromEditLayer().");
    lines << QStringLiteral("- Select: click polygon, choose part/index, then Delete By Index.");
    lines << QStringLiteral("- Delete By Index calls deleteFeatureVertexInEditLayer(feature, part, index).");
    lines << QStringLiteral("");

    if (selected.isEmpty() ||
        selected.first().layer != expectedLayer ||
        selected.first().shape == nullptr)
    {
        lines << QStringLiteral("Selected feature: none");
        return lines.join(QStringLiteral("\n"));
    }

    const FeatureHitTestResult& feature = selected.first();
    const QVector<GisShapePoint> part = editablePart(*feature.shape, partIndex);
    lines << QStringLiteral("Selected feature id: %1").arg(feature.featureId);
    lines << QStringLiteral("Vertex count: %1").arg(vertexCount(*feature.shape));
    lines << QStringLiteral("Part %1 vertex count: %2").arg(partIndex).arg(part.size());
    lines << QStringLiteral("Delete index: %1").arg(vertexIndex);

    if (vertexIndex >= 0 && vertexIndex < part.size())
    {
        const GisShapePoint& point = part[vertexIndex];
        lines << QStringLiteral("Vertex point: %1, %2")
            .arg(point.x(), 0, 'f', 3)
            .arg(point.y(), 0, 'f', 3);
    }
    else
    {
        lines << QStringLiteral("Vertex point: -");
    }

    return lines.join(QStringLiteral("\n"));
}

void refreshViewer(GisViewer& viewer)
{
    viewer.invalidateRenderCache(true, true);
    viewer.refreshLayers();
}

QAction* addToolAction(
    QToolBar& toolbar,
    QActionGroup& group,
    const QString& text,
    const QString& iconName,
    GisViewer& viewer,
    GisViewerTool tool)
{
    QAction* action = toolbar.addAction(sampleIcon(iconName), text);
    action->setCheckable(true);
    group.addAction(action);

    QObject::connect(action, &QAction::triggered, &viewer, [&viewer, tool] {
        viewer.setActiveTool(tool);
    });

    return action;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("DeleteVertex"));
    window.statusBar()->showMessage(QStringLiteral("Use Edit Vertices for active vertex delete, or Select + index for direct API delete."));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::EditVertices);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QActionGroup toolGroup(&window);
    toolGroup.setExclusive(true);
    addToolAction(*toolbar, toolGroup, QStringLiteral("Pan"), QStringLiteral("Pan.svg"), *viewer, GisViewerTool::Pan);
    QAction* selectAction = addToolAction(*toolbar, toolGroup, QStringLiteral("Select"), QStringLiteral("Select.svg"), *viewer, GisViewerTool::Info);
    QAction* editVerticesAction = addToolAction(*toolbar, toolGroup, QStringLiteral("Edit Vertices"), QStringLiteral("Vertex.svg"), *viewer, GisViewerTool::EditVertices);
    editVerticesAction->setChecked(true);
    toolbar->addSeparator();

    QAction* deleteSelectedVertexAction = toolbar->addAction(sampleIcon(QStringLiteral("Delete.svg")), QStringLiteral("Delete Selected Vertex"));
    deleteSelectedVertexAction->setShortcut(Qt::Key_Delete);

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Part"), &window));
    auto* partSpin = new QSpinBox(&window);
    partSpin->setRange(0, 0);
    partSpin->setValue(0);
    toolbar->addWidget(partSpin);

    toolbar->addWidget(new QLabel(QStringLiteral("Vertex index"), &window));
    auto* vertexIndexSpin = new QSpinBox(&window);
    vertexIndexSpin->setRange(0, 5);
    vertexIndexSpin->setValue(2);
    toolbar->addWidget(vertexIndexSpin);

    QAction* deleteByIndexAction = toolbar->addAction(sampleIcon(QStringLiteral("Delete.svg")), QStringLiteral("Delete By Index"));
    QAction* resetAction = toolbar->addAction(sampleIcon(QStringLiteral("Refresh.svg")), QStringLiteral("Reset Shape"));
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));

    auto* info = new QPlainTextEdit(&window);
    info->setReadOnly(true);
    info->setMinimumWidth(380);
    auto* infoDock = new QDockWidget(QStringLiteral("Delete vertex APIs"), &window);
    infoDock->setWidget(info);
    window.addDockWidget(Qt::RightDockWidgetArea, infoDock);

    if (!loadLayer(*viewer, sampleDataPath(QStringLiteral("shapefile/world_4326.shp"))))
        return 1;

    if (auto* worldLayer = viewer->mapLayerAt(0))
    {
        worldLayer->setName(QStringLiteral("World"));
        worldLayer->style() = worldStyle();
    }

    auto polygonLayer = createPolygonLayer();
    auto* polygonLayerPtr = polygonLayer.get();
    viewer->addLayer(polygonLayer);

    const auto updateInfo = [&] {
        info->setPlainText(selectedFeatureText(*viewer, polygonLayerPtr, partSpin->value(), vertexIndexSpin->value()));
    };

    const auto beginEditing = [&]() -> int {
        const int polygonIndex = layerIndexOf(*viewer, polygonLayerPtr);
        if (polygonIndex < 0)
            return -1;

        if (!viewer->isLayerEditing(polygonIndex) && !viewer->beginEditLayer(polygonIndex))
            return -1;

        viewer->setActiveEditLayerIndex(polygonIndex);
        return polygonIndex;
    };

    const auto syncVertexIndexRange = [&] {
        const QVector<FeatureHitTestResult> selected = viewer->selectedFeatures();
        if (selected.isEmpty() || selected.first().layer != polygonLayerPtr || selected.first().shape == nullptr)
        {
            vertexIndexSpin->setRange(0, 5);
            vertexIndexSpin->setValue(qMin(vertexIndexSpin->value(), 5));
            return;
        }

        const QVector<GisShapePoint> part = editablePart(*selected.first().shape, partSpin->value());
        vertexIndexSpin->setRange(0, qMax(0, part.size() - 1));
        vertexIndexSpin->setValue(qMin(vertexIndexSpin->value(), qMax(0, part.size() - 1)));
    };

    const auto populateShape = [&] {
        viewer->clearSelectedFeatures();

        const int polygonIndex = layerIndexOf(*viewer, polygonLayerPtr);
        if (polygonIndex >= 0)
            viewer->rollbackEditLayer(polygonIndex);

        const int currentIndex = beginEditing();
        if (currentIndex < 0)
        {
            window.statusBar()->showMessage(QStringLiteral("Editable polygon layer could not enter edit mode."));
            return;
        }

        QHash<QString, QVariant> attributes;
        attributes.insert(QStringLiteral("Name"), QStringLiteral("Delete target"));
        viewer->addPolygonToEditLayer(currentIndex, {
            GisShapePoint(-119.0, 28.0),
            GisShapePoint(-109.0, 45.0),
            GisShapePoint(-91.0, 42.0),
            GisShapePoint(-83.0, 30.0),
            GisShapePoint(-99.0, 22.0),
            GisShapePoint(-115.0, 23.5),
            GisShapePoint(-119.0, 28.0)
        }, attributes);

        viewer->setActiveTool(GisViewerTool::EditVertices);
        editVerticesAction->setChecked(true);
        vertexIndexSpin->setRange(0, 5);
        vertexIndexSpin->setValue(2);
        refreshViewer(*viewer);
        updateInfo();
        window.statusBar()->showMessage(QStringLiteral("Shape reset. Click a vertex or select the polygon."));
    };

    populateShape();

    QObject::connect(viewer, &GisViewer::activeToolChanged, viewer, [selectAction, editVerticesAction](GisViewerTool tool) {
        selectAction->setChecked(tool == GisViewerTool::Info);
        editVerticesAction->setChecked(tool == GisViewerTool::EditVertices);
    });

    QObject::connect(viewer, &GisViewer::mapClicked, viewer, [&](
        GisViewerTool tool,
        const QPointF& screenPoint,
        const GisShapePoint&,
        Qt::KeyboardModifiers) {
        if (tool != GisViewerTool::Info)
            return;

        const FeatureHitTestResult result = viewer->hitTestTopFeatureAt(screenPoint, 8);
        if (!result.isValid() || result.layer != polygonLayerPtr)
        {
            viewer->clearSelectedFeatures();
            window.statusBar()->showMessage(QStringLiteral("No editable polygon selected."));
            syncVertexIndexRange();
            updateInfo();
            return;
        }

        viewer->setSelectedFeature(result);
        syncVertexIndexRange();
        window.statusBar()->showMessage(QStringLiteral("Selected feature %1.").arg(result.featureId));
        updateInfo();
    });

    QObject::connect(deleteSelectedVertexAction, &QAction::triggered, viewer, [&] {
        if (viewer->deleteSelectedVertexFromEditLayer())
            window.statusBar()->showMessage(QStringLiteral("deleteSelectedVertexFromEditLayer() succeeded."));
        else
            window.statusBar()->showMessage(QStringLiteral("No active vertex. Use Edit Vertices and click a vertex first."));

        syncVertexIndexRange();
        refreshViewer(*viewer);
        updateInfo();
    });

    QObject::connect(deleteByIndexAction, &QAction::triggered, viewer, [&] {
        const QVector<FeatureHitTestResult> selected = viewer->selectedFeatures();
        if (selected.isEmpty() || selected.first().shape == nullptr || selected.first().layer != polygonLayerPtr)
        {
            window.statusBar()->showMessage(QStringLiteral("Select the polygon first."));
            return;
        }

        const int partIndex = partSpin->value();
        const int vertexIndex = vertexIndexSpin->value();
        if (!viewer->deleteFeatureVertexInEditLayer(selected.first(), partIndex, vertexIndex))
        {
            window.statusBar()->showMessage(QStringLiteral("deleteFeatureVertexInEditLayer failed."));
            return;
        }

        syncVertexIndexRange();
        refreshViewer(*viewer);
        updateInfo();
        window.statusBar()->showMessage(QStringLiteral("deleteFeatureVertexInEditLayer(feature, %1, %2) succeeded.")
            .arg(partIndex)
            .arg(vertexIndex));
    });

    QObject::connect(resetAction, &QAction::triggered, viewer, populateShape);
    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);
    QObject::connect(partSpin, qOverload<int>(&QSpinBox::valueChanged), viewer, [&](int) {
        syncVertexIndexRange();
        updateInfo();
    });
    QObject::connect(vertexIndexSpin, qOverload<int>(&QSpinBox::valueChanged), viewer, [&](int) {
        updateInfo();
    });
    QObject::connect(viewer, &GisViewer::selectionChanged, viewer, [&](int) {
        syncVertexIndexRange();
        updateInfo();
    });
    QObject::connect(viewer, &GisViewer::layerEditStateChanged, viewer, [&](GisLayer* layer) {
        if (layer == polygonLayerPtr)
        {
            syncVertexIndexRange();
            updateInfo();
        }
    });

    window.show();
    viewer->setViewExtent(GisExtent(-132.0, 15.0, -55.0, 55.0));

    return app.exec();
}
