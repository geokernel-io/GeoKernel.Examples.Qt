#include <memory>

#include <QAbstractItemView>
#include <QAction>
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
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolBar>
#include <QVariant>

#include "Viewer/GisViewer.h"
#include "Shapes/GisExtent.h"
#include "Layers/Defs/GisAttributeDefinition.h"
#include "Layers/Defs/GisAttributeType.h"
#include "Layers/GisLayerStyle.h"
#include "Layers/GisLayerVector.h"

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Layers::Defs;
using namespace GeoKernel::Core::Shapes;

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
        QStringLiteral("DeleteFeature"),
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

GisLayerStyle pointStyle()
{
    GisLayerStyle style;
    style.setPointColor(QStringLiteral("#D95D39"));
    style.setLineColor(QStringLiteral("#8C321D"));
    style.setPointSize(11.0f);
    style.setLineWidth(1.3f);
    style.setSelectedLineColor(QStringLiteral("#F59E0B"));
    style.setSelectedLineWidth(4.0f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("Name"));
    style.setLabelFontSize(10.0f);
    style.setLabelColor(QStringLiteral("#263238"));
    style.setLabelHaloEnabled(true);
    style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
    style.setLabelHaloWidth(2.0f);
    style.setLabelOffsetY(-12.0f);
    style.setLabelAllowOverlap(true);
    return style;
}

std::unique_ptr<GisLayerVector> createPointLayer()
{
    auto layer = GisLayerVector::createInMemory(
        QStringLiteral("Editable Points"),
        GisShapeType::Point,
        GisExtent(-180.0, -90.0, 180.0, 90.0));

    layer->addAttributeDefinition({ QStringLiteral("Name"), GisAttributeType::String, 64, 0 });
    layer->addAttributeDefinition({ QStringLiteral("Group"), GisAttributeType::String, 32, 0 });
    layer->addAttributeDefinition({ QStringLiteral("Value"), GisAttributeType::Integer, 8, 0 });
    layer->style() = pointStyle();
    return layer;
}

GisShapePoint samplePointAt(int index)
{
    static constexpr double xMin = -122.0;
    static constexpr double yMin = 30.0;
    static constexpr double xStep = 7.0;
    static constexpr double yStep = 5.0;
    static constexpr int columns = 8;

    return GisShapePoint(
        xMin + (index % columns) * xStep,
        yMin + (index / columns) * yStep);
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

void refreshViewer(GisViewer& viewer)
{
    viewer.invalidateRenderCache(true, true);
    viewer.refreshLayers();
}

void rebuildTable(QTableWidget& table, const GisLayerVector& layer)
{
    table.setRowCount(0);

    for (int rowIndex = 0; rowIndex < layer.count(); ++rowIndex)
    {
        const QHash<QString, QVariant> attributes = layer.attributesForRow(rowIndex);
        const int row = table.rowCount();
        table.insertRow(row);
        table.setItem(row, 0, new QTableWidgetItem(QString::number(rowIndex + 1)));
        table.setItem(row, 1, new QTableWidgetItem(attributes.value(QStringLiteral("Name")).toString()));
        table.setItem(row, 2, new QTableWidgetItem(attributes.value(QStringLiteral("Group")).toString()));
        table.setItem(row, 3, new QTableWidgetItem(attributes.value(QStringLiteral("Value")).toString()));
    }
}

void selectTableRow(QTableWidget& table, int featureId)
{
    table.clearSelection();

    for (int row = 0; row < table.rowCount(); ++row)
    {
        const QTableWidgetItem* idItem = table.item(row, 0);
        if (idItem == nullptr || idItem->text().toInt() != featureId)
            continue;

        table.selectRow(row);
        table.scrollToItem(idItem, QAbstractItemView::PositionAtCenter);
        return;
    }
}

QString selectionText(const GisViewer& viewer)
{
    const QVector<FeatureHitTestResult> selected = viewer.selectedFeatures();
    if (selected.isEmpty())
        return QStringLiteral("No selected feature.");

    QStringList lines;
    lines << QStringLiteral("Selected feature count: %1").arg(selected.size());
    for (const FeatureHitTestResult& feature : selected)
    {
        const QHash<QString, QVariant> attributes = feature.attributes();
        lines << QStringLiteral("Feature %1: %2")
            .arg(feature.featureId)
            .arg(attributes.value(QStringLiteral("Name")).toString());
    }

    return lines.join(QStringLiteral("\n"));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("DeleteFeature"));
    window.statusBar()->showMessage(QStringLiteral("Use Select, click a point, then delete one feature or all selected features."));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QAction* selectAction = toolbar->addAction(sampleIcon(QStringLiteral("Select.svg")), QStringLiteral("Select"));
    selectAction->setCheckable(true);
    QAction* deleteOneAction = toolbar->addAction(sampleIcon(QStringLiteral("Delete.svg")), QStringLiteral("Delete Feature"));
    QAction* deleteSelectedAction = toolbar->addAction(sampleIcon(QStringLiteral("Delete.svg")), QStringLiteral("Delete Selected"));
    QAction* resetAction = toolbar->addAction(sampleIcon(QStringLiteral("Refresh.svg")), QStringLiteral("Reset Points"));
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));

    auto* countLabel = new QLabel(&window);
    countLabel->setContentsMargins(12, 0, 12, 0);
    toolbar->addWidget(countLabel);

    auto* table = new QTableWidget(&window);
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({
        QStringLiteral("Feature ID"),
        QStringLiteral("Name"),
        QStringLiteral("Group"),
        QStringLiteral("Value")
    });
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    auto* tableDock = new QDockWidget(QStringLiteral("Editable point features"), &window);
    tableDock->setWidget(table);
    window.addDockWidget(Qt::BottomDockWidgetArea, tableDock);

    auto* selectionInfo = new QPlainTextEdit(&window);
    selectionInfo->setReadOnly(true);
    selectionInfo->setPlainText(QStringLiteral("Select mode: click a point. Ctrl+click toggles multiple selection."));
    auto* infoDock = new QDockWidget(QStringLiteral("Selection"), &window);
    infoDock->setWidget(selectionInfo);
    window.addDockWidget(Qt::RightDockWidgetArea, infoDock);

    if (!loadLayer(*viewer, sampleDataPath(QStringLiteral("shapefile/world_4326.shp"))))
        return 1;

    if (auto* worldLayer = viewer->mapLayerAt(0))
    {
        worldLayer->setName(QStringLiteral("World"));
        worldLayer->style() = worldStyle();
    }

    auto pointLayer = createPointLayer();
    auto* pointLayerPtr = pointLayer.get();
    viewer->addLayer(pointLayer);

    const auto updateCount = [&] {
        countLabel->setText(QStringLiteral("Feature count: %1 | Selected: %2")
            .arg(pointLayerPtr != nullptr ? pointLayerPtr->count() : 0)
            .arg(viewer->selectedFeatureCount()));
    };

    const auto activateEditing = [&]() -> int {
        const int pointLayerIndex = layerIndexOf(*viewer, pointLayerPtr);
        if (pointLayerIndex < 0)
            return -1;

        if (!viewer->isLayerEditing(pointLayerIndex) && !viewer->beginEditLayer(pointLayerIndex))
            return -1;

        if (!viewer->setActiveEditLayerIndex(pointLayerIndex))
            return -1;

        return pointLayerIndex;
    };

    const auto populatePoints = [&] {
        const int pointLayerIndex = activateEditing();
        if (pointLayerIndex < 0)
            return;

        viewer->rollbackEditLayer(pointLayerIndex);
        activateEditing();

        for (int i = 0; i < 16; ++i)
        {
            QHash<QString, QVariant> attributes;
            attributes.insert(QStringLiteral("Name"), QStringLiteral("Point %1").arg(i + 1));
            attributes.insert(QStringLiteral("Group"), i % 2 == 0 ? QStringLiteral("A") : QStringLiteral("B"));
            attributes.insert(QStringLiteral("Value"), (i + 1) * 5);
            viewer->addPointToEditLayer(pointLayerIndex, samplePointAt(i), attributes);
        }

        viewer->clearSelectedFeatures();
        rebuildTable(*table, *pointLayerPtr);
        selectionInfo->setPlainText(QStringLiteral("Select mode: click a point. Ctrl+click toggles multiple selection."));
        refreshViewer(*viewer);
        updateCount();
    };

    populatePoints();

    QObject::connect(selectAction, &QAction::triggered, viewer, [&, selectAction](bool checked) {
        viewer->setActiveTool(checked ? GisViewerTool::Info : GisViewerTool::Pan);
        window.statusBar()->showMessage(checked
            ? QStringLiteral("Select mode: click a point. Ctrl+click toggles multiple selection.")
            : QStringLiteral("Pan mode."));
    });

    QObject::connect(viewer, &GisViewer::activeToolChanged, viewer, [selectAction](GisViewerTool tool) {
        selectAction->setChecked(tool == GisViewerTool::Info);
    });

    QObject::connect(viewer, &GisViewer::mapClicked, viewer, [&](
        GisViewerTool tool,
        const QPointF& screenPoint,
        const GisShapePoint&,
        Qt::KeyboardModifiers modifiers) {
        if (tool != GisViewerTool::Info)
            return;

        const FeatureHitTestResult result = viewer->hitTestTopFeatureAt(screenPoint, 8);
        if (!result.isValid() || result.layer != pointLayerPtr)
        {
            viewer->clearSelectedFeatures();
            table->clearSelection();
            selectionInfo->setPlainText(QStringLiteral("No editable point feature found."));
            updateCount();
            return;
        }

        if (modifiers.testFlag(Qt::ControlModifier))
            viewer->toggleSelectedFeature(result);
        else
            viewer->setSelectedFeature(result);

        selectTableRow(*table, result.featureId);
        selectionInfo->setPlainText(selectionText(*viewer));
        updateCount();
    });

    QObject::connect(deleteOneAction, &QAction::triggered, viewer, [&] {
        const QVector<FeatureHitTestResult> selected = viewer->selectedFeatures();
        if (selected.isEmpty())
        {
            window.statusBar()->showMessage(QStringLiteral("Select a feature first."));
            return;
        }

        const int pointLayerIndex = activateEditing();
        const int featureId = selected.first().featureId;
        if (pointLayerIndex < 0 || !viewer->deleteShapeFromEditLayer(pointLayerIndex, featureId))
        {
            window.statusBar()->showMessage(QStringLiteral("deleteShapeFromEditLayer failed."));
            return;
        }

        viewer->clearSelectedFeatures();
        rebuildTable(*table, *pointLayerPtr);
        table->clearSelection();
        selectionInfo->setPlainText(QStringLiteral("Deleted feature %1 with deleteShapeFromEditLayer(index, shapeId).").arg(featureId));
        refreshViewer(*viewer);
        updateCount();
    });

    QObject::connect(deleteSelectedAction, &QAction::triggered, viewer, [&] {
        if (viewer->selectedFeatureCount() <= 0)
        {
            window.statusBar()->showMessage(QStringLiteral("Select one or more features first."));
            return;
        }

        const int deletedCount = viewer->selectedFeatureCount();
        if (!viewer->deleteSelectedFeaturesFromEditLayer())
        {
            window.statusBar()->showMessage(QStringLiteral("deleteSelectedFeaturesFromEditLayer failed."));
            return;
        }

        rebuildTable(*table, *pointLayerPtr);
        table->clearSelection();
        selectionInfo->setPlainText(QStringLiteral("Deleted %1 selected feature(s) with deleteSelectedFeaturesFromEditLayer().").arg(deletedCount));
        refreshViewer(*viewer);
        updateCount();
    });

    QObject::connect(resetAction, &QAction::triggered, viewer, populatePoints);
    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);
    QObject::connect(viewer, &GisViewer::selectionChanged, viewer, [&](int) {
        updateCount();
    });

    window.show();
    viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 55.0));

    return app.exec();
}
