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

#include "FeatureSources/FeatureHitTestResult.h"
#include "Layers/Defs/GisAttributeDefinition.h"
#include "Layers/Defs/GisAttributeType.h"
#include "Layers/GisLayerStyle.h"
#include "Layers/GisLayerVector.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShapePoint.h"
#include "Viewer/GisViewer.h"

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Layers::Defs;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Viewer;
using namespace GeoKernel::Viewer::FeatureSources;

namespace
{
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
            QStringLiteral("CanEditCheck"),
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
        style.setPointSize(12.0f);
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
        style.setLabelOffsetY(-13.0f);
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
        layer->style() = pointStyle();
        return layer;
    }

    GisShapePoint samplePointAt(int index)
    {
        static constexpr double xMin = -121.0;
        static constexpr double yMin = 31.0;
        static constexpr double xStep = 8.0;
        static constexpr double yStep = 5.5;
        static constexpr int columns = 7;

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

    QString yesNo(bool value)
    {
        return value ? QStringLiteral("true") : QStringLiteral("false");
    }

    void setStatusRow(QTableWidget& table, int row, const QString& api, bool value, const QString& note)
    {
        table.setItem(row, 0, new QTableWidgetItem(api));
        table.setItem(row, 1, new QTableWidgetItem(yesNo(value)));
        table.setItem(row, 2, new QTableWidgetItem(note));
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

    void rebuildFeatureTable(QTableWidget& table, const GisLayerVector& layer)
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
        }
    }

    QString selectionText(const GisViewer& viewer)
    {
        const QVector<FeatureHitTestResult> selected = viewer.selectedFeatures();
        if (selected.isEmpty())
            return QStringLiteral("No selected feature.\n\ncanEditSelectedFeatures and canMoveSelectedFeatures require at least one selected feature.");

        QStringList lines;
        lines << QStringLiteral("Selected feature count: %1").arg(selected.size());
        for (const FeatureHitTestResult& feature : selected)
        {
            const QHash<QString, QVariant> attributes = feature.attributes();
            lines << QStringLiteral("Feature %1: %2")
                .arg(feature.featureId)
                .arg(attributes.value(QStringLiteral("Name")).toString());
        }
        lines << QString();
        lines << QStringLiteral("Ctrl+click toggles multiple selection.");
        return lines.join(QStringLiteral("\n"));
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("CanEditCheck"));
    window.statusBar()->showMessage(QStringLiteral("Use Begin Edit and Select to see canEdit* capability checks change."));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QAction* beginEditAction = toolbar->addAction(sampleIcon(QStringLiteral("Edit.svg")), QStringLiteral("Begin Edit"));
    QAction* commitAction = toolbar->addAction(sampleIcon(QStringLiteral("SaveProject.svg")), QStringLiteral("Commit Edit"));
    QAction* rollbackAction = toolbar->addAction(sampleIcon(QStringLiteral("PreviousExtent.svg")), QStringLiteral("Rollback Edit"));
    toolbar->addSeparator();
    QAction* selectAction = toolbar->addAction(sampleIcon(QStringLiteral("Select.svg")), QStringLiteral("Select"));
    selectAction->setCheckable(true);
    QAction* clearSelectionAction = toolbar->addAction(sampleIcon(QStringLiteral("Delete.svg")), QStringLiteral("Clear Selection"));
    toolbar->addSeparator();
    QAction* resetAction = toolbar->addAction(sampleIcon(QStringLiteral("Refresh.svg")), QStringLiteral("Reset Points"));
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));

    auto* stateLabel = new QLabel(&window);
    stateLabel->setContentsMargins(12, 0, 12, 0);
    toolbar->addWidget(stateLabel);

    auto* statusTable = new QTableWidget(&window);
    statusTable->setColumnCount(3);
    statusTable->setRowCount(3);
    statusTable->setHorizontalHeaderLabels({
        QStringLiteral("API"),
        QStringLiteral("Result"),
        QStringLiteral("Why")
    });
    statusTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    statusTable->setSelectionMode(QAbstractItemView::NoSelection);

    auto* statusDock = new QDockWidget(QStringLiteral("Can edit checks"), &window);
    statusDock->setWidget(statusTable);
    window.addDockWidget(Qt::RightDockWidgetArea, statusDock);

    auto* featureTable = new QTableWidget(&window);
    featureTable->setColumnCount(3);
    featureTable->setHorizontalHeaderLabels({
        QStringLiteral("Feature ID"),
        QStringLiteral("Name"),
        QStringLiteral("Group")
    });
    featureTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    featureTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    featureTable->setSelectionMode(QAbstractItemView::SingleSelection);

    auto* featureDock = new QDockWidget(QStringLiteral("Editable features"), &window);
    featureDock->setWidget(featureTable);
    window.addDockWidget(Qt::LeftDockWidgetArea, featureDock);

    auto* selectionInfo = new QPlainTextEdit(&window);
    selectionInfo->setReadOnly(true);
    selectionInfo->setMinimumHeight(120);
    auto* infoDock = new QDockWidget(QStringLiteral("Selection"), &window);
    infoDock->setWidget(selectionInfo);
    window.addDockWidget(Qt::BottomDockWidgetArea, infoDock);

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

    const auto pointLayerIndex = [&]() {
        return layerIndexOf(*viewer, pointLayerPtr);
    };

    const auto updateUi = [&]() {
        const int index = pointLayerIndex();
        const bool canEditLayer = viewer->canEditLayer(index);
        const bool isEditing = viewer->isLayerEditing(index);
        const int selectedCount = viewer->selectedFeatureCount();
        const bool canEditSelection = viewer->canEditSelectedFeatures();
        const bool canMoveSelection = viewer->canMoveSelectedFeatures();

        beginEditAction->setEnabled(canEditLayer && !isEditing);
        commitAction->setEnabled(isEditing);
        rollbackAction->setEnabled(isEditing);
        clearSelectionAction->setEnabled(selectedCount > 0);

        setStatusRow(*statusTable, 0, QStringLiteral("canEditLayer(index)"), canEditLayer,
            QStringLiteral("Layer must exist and support editing."));
        setStatusRow(*statusTable, 1, QStringLiteral("canEditSelectedFeatures()"), canEditSelection,
            QStringLiteral("Requires selected features from an editing layer."));
        setStatusRow(*statusTable, 2, QStringLiteral("canMoveSelectedFeatures()"), canMoveSelection,
            QStringLiteral("Requires selected editable features with valid geometry."));

        stateLabel->setText(QStringLiteral("Layer index: %1 | Editing: %2 | Selected: %3")
            .arg(index)
            .arg(isEditing ? QStringLiteral("ON") : QStringLiteral("OFF"))
            .arg(selectedCount));
    };

    const auto activateEditing = [&]() -> int {
        const int index = pointLayerIndex();
        if (index < 0)
            return -1;

        if (!viewer->isLayerEditing(index) && !viewer->beginEditLayer(index))
            return -1;

        if (!viewer->setActiveEditLayerIndex(index))
            return -1;

        return index;
    };

    const auto populatePoints = [&] {
        const int index = activateEditing();
        if (index < 0)
            return;

        viewer->rollbackEditLayer(index);
        activateEditing();

        for (int i = 0; i < 14; ++i)
        {
            QHash<QString, QVariant> attributes;
            attributes.insert(QStringLiteral("Name"), QStringLiteral("Point %1").arg(i + 1));
            attributes.insert(QStringLiteral("Group"), i % 2 == 0 ? QStringLiteral("North") : QStringLiteral("South"));
            viewer->addPointToEditLayer(index, samplePointAt(i), attributes);
        }

        viewer->commitEditLayer(index);
        viewer->clearSelectedFeatures();
        rebuildFeatureTable(*featureTable, *pointLayerPtr);
        selectionInfo->setPlainText(selectionText(*viewer));
        refreshViewer(*viewer);
        updateUi();
    };

    populatePoints();

    QObject::connect(beginEditAction, &QAction::triggered, viewer, [&] {
        if (activateEditing() >= 0)
        {
            window.statusBar()->showMessage(QStringLiteral("Edit session started. Select a point to enable selected-feature checks."));
            updateUi();
        }
    });

    QObject::connect(commitAction, &QAction::triggered, viewer, [&] {
        const int index = pointLayerIndex();
        if (viewer->commitEditLayer(index))
        {
            window.statusBar()->showMessage(QStringLiteral("Edit session committed. Selection checks are false until editing starts again."));
            updateUi();
        }
    });

    QObject::connect(rollbackAction, &QAction::triggered, viewer, [&] {
        const int index = pointLayerIndex();
        if (viewer->rollbackEditLayer(index))
        {
            viewer->clearSelectedFeatures();
            selectionInfo->setPlainText(selectionText(*viewer));
            window.statusBar()->showMessage(QStringLiteral("Edit session rolled back."));
            refreshViewer(*viewer);
            updateUi();
        }
    });

    QObject::connect(selectAction, &QAction::triggered, viewer, [&, selectAction](bool checked) {
        viewer->setActiveTool(checked ? GisViewerTool::Info : GisViewerTool::Pan);
        window.statusBar()->showMessage(checked
            ? QStringLiteral("Select mode: click a point. Ctrl+click toggles multiple selection.")
            : QStringLiteral("Pan mode."));
    });

    QObject::connect(viewer, &GisViewer::activeToolChanged, viewer, [selectAction](GisViewerTool tool) {
        selectAction->setChecked(tool == GisViewerTool::Info);
    });

    QObject::connect(clearSelectionAction, &QAction::triggered, viewer, [&] {
        viewer->clearSelectedFeatures();
        featureTable->clearSelection();
        selectionInfo->setPlainText(selectionText(*viewer));
        window.statusBar()->showMessage(QStringLiteral("Selection cleared."));
        updateUi();
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
            featureTable->clearSelection();
            selectionInfo->setPlainText(selectionText(*viewer));
            updateUi();
            return;
        }

        if (modifiers.testFlag(Qt::ControlModifier))
            viewer->toggleSelectedFeature(result);
        else
            viewer->setSelectedFeature(result);

        selectTableRow(*featureTable, result.featureId);
        selectionInfo->setPlainText(selectionText(*viewer));
        updateUi();
    });

    QObject::connect(viewer, &GisViewer::layerEditStateChanged, viewer, [&](GisLayer* layer) {
        if (layer != pointLayerPtr)
            return;

        rebuildFeatureTable(*featureTable, *pointLayerPtr);
        selectionInfo->setPlainText(selectionText(*viewer));
        refreshViewer(*viewer);
        updateUi();
    });

    QObject::connect(viewer, &GisViewer::selectionChanged, viewer, [&](int) {
        selectionInfo->setPlainText(selectionText(*viewer));
        updateUi();
    });

    QObject::connect(resetAction, &QAction::triggered, viewer, populatePoints);
    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);

    window.show();
    viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 55.0));

    return app.exec();
}
