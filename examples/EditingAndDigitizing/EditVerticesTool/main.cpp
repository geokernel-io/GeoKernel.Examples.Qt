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
        QStringLiteral("EditVerticesTool"),
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

GisLayerStyle lineStyle()
{
    GisLayerStyle style;
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

GisLayerStyle polygonStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#F2D27A"));
    style.setFillOpacity(145);
    style.setLineColor(QStringLiteral("#D95D39"));
    style.setLineWidth(2.4f);
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

std::unique_ptr<GisLayerVector> createLineLayer()
{
    auto layer = GisLayerVector::createInMemory(
        QStringLiteral("Editable Lines"),
        GisShapeType::Polyline,
        GisExtent(-180.0, -90.0, 180.0, 90.0));

    layer->addAttributeDefinition({ QStringLiteral("Name"), GisAttributeType::String, 64, 0 });
    layer->style() = lineStyle();
    return layer;
}

std::unique_ptr<GisLayerVector> createPolygonLayer()
{
    auto layer = GisLayerVector::createInMemory(
        QStringLiteral("Editable Polygons"),
        GisShapeType::Polygon,
        GisExtent(-180.0, -90.0, 180.0, 90.0));

    layer->addAttributeDefinition({ QStringLiteral("Name"), GisAttributeType::String, 64, 0 });
    layer->style() = polygonStyle();
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

int partVertexCount(const GisShape& shape)
{
    int total = 0;
    if (const auto* polyline = dynamic_cast<const GisShapePolyline*>(&shape))
    {
        for (const QVector<GisShapePoint>& part : polyline->parts())
            total += part.size();
    }
    else if (const auto* polygon = dynamic_cast<const GisShapePolygon*>(&shape))
    {
        for (const QVector<GisShapePoint>& part : polygon->parts())
            total += part.size();
    }

    return total;
}

QString featureSummary(const GisViewer& viewer, const GisLayerVector& lineLayer, const GisLayerVector& polygonLayer)
{
    QStringList lines;
    lines << QStringLiteral("Tool usage:");
    lines << QStringLiteral("- Edit Vertices: click a feature or one of its vertices.");
    lines << QStringLiteral("- Drag an active vertex to move it.");
    lines << QStringLiteral("- Double-click a selected segment to insert a vertex.");
    lines << QStringLiteral("- Press Delete or click Delete Vertex to remove the active vertex.");
    lines << QStringLiteral("");
    lines << QStringLiteral("Feature vertex counts:");

    const auto appendLayer = [&](const GisLayerVector& layer) {
        for (int row = 0; row < layer.count(); ++row)
        {
            const int shapeId = row + 1;
            const GisShape* shape = layer.getShape(shapeId);
            const QHash<QString, QVariant> attributes = layer.attributesForRow(row);
            lines << QStringLiteral("- %1 / %2: %3 vertices")
                .arg(layer.name())
                .arg(attributes.value(QStringLiteral("Name")).toString())
                .arg(shape != nullptr ? partVertexCount(*shape) : 0);
        }
    };

    appendLayer(lineLayer);
    appendLayer(polygonLayer);

    const QVector<FeatureHitTestResult> selected = viewer.selectedFeatures();
    lines << QStringLiteral("");
    lines << QStringLiteral("Selected features: %1").arg(selected.size());
    for (const FeatureHitTestResult& feature : selected)
    {
        lines << QStringLiteral("- %1 / feature id %2")
            .arg(feature.layer != nullptr ? feature.layer->name() : QStringLiteral("Layer"))
            .arg(feature.featureId);
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
    window.setWindowTitle(QStringLiteral("EditVerticesTool"));
    window.statusBar()->showMessage(QStringLiteral("Edit Vertices: drag vertices, double-click segments to add, Delete to remove."));

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
    QAction* editVerticesAction = addToolAction(*toolbar, toolGroup, QStringLiteral("Edit Vertices"), QStringLiteral("Vertex.svg"), *viewer, GisViewerTool::EditVertices);
    editVerticesAction->setChecked(true);
    toolbar->addSeparator();
    QAction* deleteVertexAction = toolbar->addAction(sampleIcon(QStringLiteral("Delete.svg")), QStringLiteral("Delete Vertex"));
    deleteVertexAction->setShortcut(Qt::Key_Delete);
    QAction* resetAction = toolbar->addAction(sampleIcon(QStringLiteral("Refresh.svg")), QStringLiteral("Reset Shapes"));
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));

    auto* statusLabel = new QLabel(&window);
    statusLabel->setContentsMargins(12, 0, 12, 0);
    toolbar->addWidget(statusLabel);

    auto* info = new QPlainTextEdit(&window);
    info->setReadOnly(true);
    info->setMinimumWidth(330);
    auto* infoDock = new QDockWidget(QStringLiteral("Vertex editing"), &window);
    infoDock->setWidget(info);
    window.addDockWidget(Qt::RightDockWidgetArea, infoDock);

    if (!loadLayer(*viewer, sampleDataPath(QStringLiteral("shapefile/world_4326.shp"))))
        return 1;

    if (auto* worldLayer = viewer->mapLayerAt(0))
    {
        worldLayer->setName(QStringLiteral("World"));
        worldLayer->style() = worldStyle();
    }

    auto lineLayer = createLineLayer();
    auto* lineLayerPtr = lineLayer.get();
    viewer->addLayer(lineLayer);

    auto polygonLayer = createPolygonLayer();
    auto* polygonLayerPtr = polygonLayer.get();
    viewer->addLayer(polygonLayer);

    const auto updateInfo = [&] {
        statusLabel->setText(QStringLiteral("Lines: %1 | Polygons: %2 | Selected: %3")
            .arg(lineLayerPtr != nullptr ? lineLayerPtr->count() : 0)
            .arg(polygonLayerPtr != nullptr ? polygonLayerPtr->count() : 0)
            .arg(viewer->selectedFeatureCount()));

        if (lineLayerPtr != nullptr && polygonLayerPtr != nullptr)
            info->setPlainText(featureSummary(*viewer, *lineLayerPtr, *polygonLayerPtr));
    };

    const auto beginEditing = [&]() -> bool {
        const int lineIndex = layerIndexOf(*viewer, lineLayerPtr);
        const int polygonIndex = layerIndexOf(*viewer, polygonLayerPtr);
        if (lineIndex < 0 || polygonIndex < 0)
            return false;

        if (!viewer->isLayerEditing(lineIndex) && !viewer->beginEditLayer(lineIndex))
            return false;

        if (!viewer->isLayerEditing(polygonIndex) && !viewer->beginEditLayer(polygonIndex))
            return false;

        viewer->setActiveEditLayerIndex(polygonIndex);
        return true;
    };

    const auto populateShapes = [&] {
        const int lineIndex = layerIndexOf(*viewer, lineLayerPtr);
        const int polygonIndex = layerIndexOf(*viewer, polygonLayerPtr);
        if (lineIndex >= 0)
            viewer->rollbackEditLayer(lineIndex);
        if (polygonIndex >= 0)
            viewer->rollbackEditLayer(polygonIndex);

        if (!beginEditing())
        {
            window.statusBar()->showMessage(QStringLiteral("Edit layers could not enter edit mode."));
            return;
        }

        const int currentLineIndex = layerIndexOf(*viewer, lineLayerPtr);
        const int currentPolygonIndex = layerIndexOf(*viewer, polygonLayerPtr);

        QHash<QString, QVariant> lineAAttributes;
        lineAAttributes.insert(QStringLiteral("Name"), QStringLiteral("Pacific route"));
        viewer->addPolylineToEditLayer(currentLineIndex, {
            GisShapePoint(-127.0, 31.0),
            GisShapePoint(-118.0, 40.0),
            GisShapePoint(-107.0, 34.0),
            GisShapePoint(-96.0, 43.0),
            GisShapePoint(-86.0, 37.0)
        }, lineAAttributes);

        QHash<QString, QVariant> lineBAttributes;
        lineBAttributes.insert(QStringLiteral("Name"), QStringLiteral("Gulf route"));
        viewer->addPolylineToEditLayer(currentLineIndex, {
            GisShapePoint(-113.0, 24.0),
            GisShapePoint(-101.0, 29.0),
            GisShapePoint(-90.0, 27.0),
            GisShapePoint(-80.0, 33.0)
        }, lineBAttributes);

        QHash<QString, QVariant> polygonAAttributes;
        polygonAAttributes.insert(QStringLiteral("Name"), QStringLiteral("Edit polygon A"));
        viewer->addPolygonToEditLayer(currentPolygonIndex, {
            GisShapePoint(-118.0, 30.0),
            GisShapePoint(-109.0, 45.0),
            GisShapePoint(-91.0, 42.0),
            GisShapePoint(-94.0, 27.0),
            GisShapePoint(-111.0, 24.0),
            GisShapePoint(-118.0, 30.0)
        }, polygonAAttributes);

        QHash<QString, QVariant> polygonBAttributes;
        polygonBAttributes.insert(QStringLiteral("Name"), QStringLiteral("Edit polygon B"));
        viewer->addPolygonToEditLayer(currentPolygonIndex, {
            GisShapePoint(-83.0, 24.0),
            GisShapePoint(-73.0, 31.0),
            GisShapePoint(-65.0, 25.0),
            GisShapePoint(-72.0, 18.0),
            GisShapePoint(-83.0, 24.0)
        }, polygonBAttributes);

        viewer->clearSelectedFeatures();
        viewer->setActiveTool(GisViewerTool::EditVertices);
        refreshViewer(*viewer);
        updateInfo();
        window.statusBar()->showMessage(QStringLiteral("Shapes reset. Edit Vertices tool is active."));
    };

    populateShapes();

    QObject::connect(viewer, &GisViewer::activeToolChanged, viewer, [editVerticesAction](GisViewerTool tool) {
        editVerticesAction->setChecked(tool == GisViewerTool::EditVertices);
    });

    QObject::connect(deleteVertexAction, &QAction::triggered, viewer, [&] {
        if (viewer->deleteSelectedVertexFromEditLayer())
            window.statusBar()->showMessage(QStringLiteral("Selected vertex deleted."));
        else
            window.statusBar()->showMessage(QStringLiteral("No active vertex to delete. Click a vertex first."));

        refreshViewer(*viewer);
        updateInfo();
    });

    QObject::connect(resetAction, &QAction::triggered, viewer, populateShapes);
    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);

    QObject::connect(viewer, &GisViewer::selectionChanged, viewer, [&](int) {
        updateInfo();
    });

    QObject::connect(viewer, &GisViewer::layerEditStateChanged, viewer, [&](GisLayer* layer) {
        if (layer != lineLayerPtr && layer != polygonLayerPtr)
            return;

        updateInfo();
    });

    window.show();
    viewer->setViewExtent(GisExtent(-132.0, 15.0, -55.0, 55.0));

    return app.exec();
}
