#include <cmath>
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

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Layers::Defs;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Viewer;

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
        QStringLiteral("InsertVertex"),
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
    style.setFillOpacity(140);
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
            std::abs(part.first().x() - part.last().x()) < 1e-12 &&
            std::abs(part.first().y() - part.last().y()) < 1e-12)
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

GisShapePoint insertionPointForSegment(const GisShape& shape, int partIndex, int insertIndex)
{
    const QVector<GisShapePoint> part = editablePart(shape, partIndex);
    if (part.size() < 2 || insertIndex <= 0 || insertIndex > part.size())
        return {};

    const GisShapePoint& a = part[insertIndex - 1];
    const GisShapePoint& b = part[insertIndex == part.size() ? 0 : insertIndex];
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    const double length = std::sqrt(dx * dx + dy * dy);
    const double offset = length > 0.0 ? length * 0.22 : 1.0;

    return GisShapePoint(
        (a.x() + b.x()) * 0.5 - (dy / (length > 0.0 ? length : 1.0)) * offset,
        (a.y() + b.y()) * 0.5 + (dx / (length > 0.0 ? length : 1.0)) * offset);
}

QString selectedFeatureText(const GisViewer& viewer, const GisLayerVector* expectedLayer, int partIndex, int insertIndex)
{
    const QVector<FeatureHitTestResult> selected = viewer.selectedFeatures();
    QStringList lines;
    lines << QStringLiteral("Usage:");
    lines << QStringLiteral("- Select: click a polygon.");
    lines << QStringLiteral("- Part is usually 0 for this sample.");
    lines << QStringLiteral("- Insert index means insert before that vertex index.");
    lines << QStringLiteral("- The sample computes a visible point near the selected segment.");
    lines << QStringLiteral("- Click Insert Vertex to call insertFeatureVertexInEditLayer(feature, part, index, point).");
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
    const GisShapePoint insertPoint = insertionPointForSegment(*feature.shape, partIndex, insertIndex);

    lines << QStringLiteral("Selected feature id: %1").arg(feature.featureId);
    lines << QStringLiteral("Selected layer: %1").arg(feature.layer != nullptr ? feature.layer->name() : QStringLiteral("-"));
    lines << QStringLiteral("Vertex count: %1").arg(vertexCount(*feature.shape));
    lines << QStringLiteral("Part %1 vertex count: %2").arg(partIndex).arg(part.size());
    lines << QStringLiteral("Insert index: %1").arg(insertIndex);
    lines << QStringLiteral("Calculated point: %1, %2")
        .arg(insertPoint.x(), 0, 'f', 3)
        .arg(insertPoint.y(), 0, 'f', 3);

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
    window.setWindowTitle(QStringLiteral("InsertVertex"));
    window.statusBar()->showMessage(QStringLiteral("Select a polygon, then click Insert Vertex."));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Info);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QActionGroup toolGroup(&window);
    toolGroup.setExclusive(true);
    addToolAction(*toolbar, toolGroup, QStringLiteral("Pan"), QStringLiteral("Pan.svg"), *viewer, GisViewerTool::Pan);
    QAction* selectAction = addToolAction(*toolbar, toolGroup, QStringLiteral("Select"), QStringLiteral("Select.svg"), *viewer, GisViewerTool::Info);
    selectAction->setChecked(true);
    toolbar->addSeparator();

    toolbar->addWidget(new QLabel(QStringLiteral("Part"), &window));
    auto* partSpin = new QSpinBox(&window);
    partSpin->setRange(0, 0);
    partSpin->setValue(0);
    toolbar->addWidget(partSpin);

    toolbar->addWidget(new QLabel(QStringLiteral("Insert index"), &window));
    auto* insertIndexSpin = new QSpinBox(&window);
    insertIndexSpin->setRange(1, 12);
    insertIndexSpin->setValue(2);
    toolbar->addWidget(insertIndexSpin);

    QAction* insertAction = toolbar->addAction(sampleIcon(QStringLiteral("Add.svg")), QStringLiteral("Insert Vertex"));
    QAction* resetAction = toolbar->addAction(sampleIcon(QStringLiteral("Refresh.svg")), QStringLiteral("Reset Shape"));
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));

    auto* info = new QPlainTextEdit(&window);
    info->setReadOnly(true);
    info->setMinimumWidth(360);
    auto* infoDock = new QDockWidget(QStringLiteral("insertFeatureVertexInEditLayer"), &window);
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
        info->setPlainText(selectedFeatureText(*viewer, polygonLayerPtr, partSpin->value(), insertIndexSpin->value()));
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
        attributes.insert(QStringLiteral("Name"), QStringLiteral("Insert target"));
        viewer->addPolygonToEditLayer(currentIndex, {
            GisShapePoint(-119.0, 28.0),
            GisShapePoint(-109.0, 45.0),
            GisShapePoint(-91.0, 42.0),
            GisShapePoint(-83.0, 30.0),
            GisShapePoint(-99.0, 22.0),
            GisShapePoint(-115.0, 23.5),
            GisShapePoint(-119.0, 28.0)
        }, attributes);

        viewer->setActiveTool(GisViewerTool::Info);
        insertIndexSpin->setRange(1, 6);
        insertIndexSpin->setValue(2);
        refreshViewer(*viewer);
        updateInfo();
        window.statusBar()->showMessage(QStringLiteral("Shape reset. Select the polygon, then insert a vertex."));
    };

    populateShape();

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
            updateInfo();
            return;
        }

        viewer->setSelectedFeature(result);
        const QVector<GisShapePoint> part = editablePart(*result.shape, partSpin->value());
        insertIndexSpin->setRange(1, qMax(1, part.size()));
        insertIndexSpin->setValue(qMin(insertIndexSpin->value(), part.size()));
        window.statusBar()->showMessage(QStringLiteral("Selected feature %1.").arg(result.featureId));
        updateInfo();
    });

    QObject::connect(insertAction, &QAction::triggered, viewer, [&] {
        const QVector<FeatureHitTestResult> selected = viewer->selectedFeatures();
        if (selected.isEmpty() || selected.first().shape == nullptr)
        {
            window.statusBar()->showMessage(QStringLiteral("Select a polygon first."));
            return;
        }

        const FeatureHitTestResult feature = selected.first();
        const int partIndex = partSpin->value();
        const int insertIndex = insertIndexSpin->value();
        const GisShapePoint point = insertionPointForSegment(*feature.shape, partIndex, insertIndex);
        if (point.isEmpty())
        {
            window.statusBar()->showMessage(QStringLiteral("Invalid part/index for selected feature."));
            return;
        }

        if (!viewer->insertFeatureVertexInEditLayer(feature, partIndex, insertIndex, point))
        {
            window.statusBar()->showMessage(QStringLiteral("insertFeatureVertexInEditLayer failed."));
            return;
        }

        const QVector<FeatureHitTestResult> updated = viewer->selectedFeatures();
        if (!updated.isEmpty() && updated.first().shape != nullptr)
        {
            const QVector<GisShapePoint> part = editablePart(*updated.first().shape, partIndex);
            insertIndexSpin->setRange(1, qMax(1, part.size()));
            insertIndexSpin->setValue(qMin(insertIndex + 1, part.size()));
        }

        refreshViewer(*viewer);
        updateInfo();
        window.statusBar()->showMessage(QStringLiteral("insertFeatureVertexInEditLayer(feature, %1, %2, point) succeeded.")
            .arg(partIndex)
            .arg(insertIndex));
    });

    QObject::connect(resetAction, &QAction::triggered, viewer, populateShape);
    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);
    QObject::connect(partSpin, qOverload<int>(&QSpinBox::valueChanged), viewer, [&](int) { updateInfo(); });
    QObject::connect(insertIndexSpin, qOverload<int>(&QSpinBox::valueChanged), viewer, [&](int) { updateInfo(); });
    QObject::connect(viewer, &GisViewer::selectionChanged, viewer, [&](int) { updateInfo(); });
    QObject::connect(viewer, &GisViewer::layerEditStateChanged, viewer, [&](GisLayer* layer) {
        if (layer == polygonLayerPtr)
            updateInfo();
    });

    window.show();
    viewer->setViewExtent(GisExtent(-132.0, 15.0, -55.0, 55.0));

    return app.exec();
}
