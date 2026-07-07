#include <memory>

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QFormLayout>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QSize>
#include <QSpinBox>
#include <QStatusBar>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolBar>
#include <QVariant>
#include <QWidget>

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
        QStringLiteral("SetAttributes"),
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
    style.setLineWidth(1.4f);
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
        QStringLiteral("Editable Attributes"),
        GisShapeType::Point,
        GisExtent(-180.0, -90.0, 180.0, 90.0));

    layer->addAttributeDefinition({ QStringLiteral("Name"), GisAttributeType::String, 64, 0 });
    layer->addAttributeDefinition({ QStringLiteral("Status"), GisAttributeType::String, 32, 0 });
    layer->addAttributeDefinition({ QStringLiteral("Priority"), GisAttributeType::Integer, 8, 0 });
    layer->style() = pointStyle();
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
        const int shapeId = rowIndex + 1;
        const QHash<QString, QVariant> attributes = layer.attributesForRow(rowIndex);
        const int row = table.rowCount();
        table.insertRow(row);
        table.setItem(row, 0, new QTableWidgetItem(QString::number(shapeId)));
        table.setItem(row, 1, new QTableWidgetItem(attributes.value(QStringLiteral("Name")).toString()));
        table.setItem(row, 2, new QTableWidgetItem(attributes.value(QStringLiteral("Status")).toString()));
        table.setItem(row, 3, new QTableWidgetItem(attributes.value(QStringLiteral("Priority")).toString()));
    }
}

void selectTableRow(QTableWidget& table, int shapeId)
{
    table.clearSelection();

    for (int row = 0; row < table.rowCount(); ++row)
    {
        const QTableWidgetItem* idItem = table.item(row, 0);
        if (idItem == nullptr || idItem->text().toInt() != shapeId)
            continue;

        table.selectRow(row);
        table.scrollToItem(idItem, QAbstractItemView::PositionAtCenter);
        return;
    }
}

int selectedShapeId(const GisViewer& viewer, const GisLayerVector* expectedLayer)
{
    const QVector<FeatureHitTestResult> selected = viewer.selectedFeatures();
    if (selected.isEmpty() || selected.first().layer != expectedLayer)
        return -1;

    return selected.first().featureId;
}

void loadFormFromAttributes(
    const QHash<QString, QVariant>& attributes,
    QLineEdit& nameEdit,
    QComboBox& statusCombo,
    QSpinBox& prioritySpin)
{
    nameEdit.setText(attributes.value(QStringLiteral("Name")).toString());

    const QString status = attributes.value(QStringLiteral("Status")).toString();
    const int statusIndex = statusCombo.findText(status);
    statusCombo.setCurrentIndex(statusIndex >= 0 ? statusIndex : 0);

    prioritySpin.setValue(attributes.value(QStringLiteral("Priority")).toInt());
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("SetAttributes"));
    window.statusBar()->showMessage(QStringLiteral("Select a point, edit form values, then Apply Attributes."));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Info);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QAction* selectAction = toolbar->addAction(sampleIcon(QStringLiteral("Select.svg")), QStringLiteral("Select"));
    selectAction->setCheckable(true);
    selectAction->setChecked(true);
    QAction* applyAction = toolbar->addAction(sampleIcon(QStringLiteral("Edit.svg")), QStringLiteral("Apply Attributes"));
    QAction* undoAction = toolbar->addAction(sampleIcon(QStringLiteral("PreviousExtent.svg")), QStringLiteral("Undo"));
    QAction* redoAction = toolbar->addAction(sampleIcon(QStringLiteral("NextExtent.svg")), QStringLiteral("Redo"));
    QAction* resetAction = toolbar->addAction(sampleIcon(QStringLiteral("Refresh.svg")), QStringLiteral("Reset"));
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));

    auto* countLabel = new QLabel(&window);
    countLabel->setContentsMargins(12, 0, 12, 0);
    toolbar->addWidget(countLabel);

    auto* formWidget = new QWidget(&window);
    auto* formLayout = new QFormLayout(formWidget);
    auto* selectedLabel = new QLabel(QStringLiteral("none"), formWidget);
    auto* nameEdit = new QLineEdit(formWidget);
    auto* statusCombo = new QComboBox(formWidget);
    statusCombo->addItems({ QStringLiteral("Planned"), QStringLiteral("Active"), QStringLiteral("Done") });
    auto* prioritySpin = new QSpinBox(formWidget);
    prioritySpin->setRange(1, 10);
    formLayout->addRow(QStringLiteral("Selected shape"), selectedLabel);
    formLayout->addRow(QStringLiteral("Name"), nameEdit);
    formLayout->addRow(QStringLiteral("Status"), statusCombo);
    formLayout->addRow(QStringLiteral("Priority"), prioritySpin);
    auto* formDock = new QDockWidget(QStringLiteral("Attribute editor"), &window);
    formDock->setWidget(formWidget);
    window.addDockWidget(Qt::RightDockWidgetArea, formDock);

    auto* table = new QTableWidget(&window);
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({
        QStringLiteral("Shape ID"),
        QStringLiteral("Name"),
        QStringLiteral("Status"),
        QStringLiteral("Priority")
    });
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setMaximumHeight(170);
    auto* tableDock = new QDockWidget(QStringLiteral("Editable Attributes table"), &window);
    tableDock->setWidget(table);
    window.addDockWidget(Qt::BottomDockWidgetArea, tableDock);

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

    const auto activateEditing = [&]() -> bool {
        const int index = pointLayerIndex();
        if (index < 0)
            return false;

        if (!viewer->isLayerEditing(index) && !viewer->beginEditLayer(index))
            return false;

        return viewer->setActiveEditLayerIndex(index);
    };

    const auto updateUi = [&] {
        const int index = pointLayerIndex();
        const int shapeId = selectedShapeId(*viewer, pointLayerPtr);
        countLabel->setText(QStringLiteral("Feature count: %1 | Selected: %2")
            .arg(pointLayerPtr != nullptr ? pointLayerPtr->count() : 0)
            .arg(shapeId > 0 ? QString::number(shapeId) : QStringLiteral("none")));
        selectedLabel->setText(shapeId > 0 ? QString::number(shapeId) : QStringLiteral("none"));
        applyAction->setEnabled(shapeId > 0);
        undoAction->setEnabled(viewer->canUndoEditLayer(index));
        redoAction->setEnabled(viewer->canRedoEditLayer(index));

        if (pointLayerPtr != nullptr)
            rebuildTable(*table, *pointLayerPtr);

        if (shapeId > 0)
            selectTableRow(*table, shapeId);
    };

    const auto populatePoints = [&] {
        viewer->clearSelectedFeatures();

        const int index = pointLayerIndex();
        if (index >= 0)
            viewer->rollbackEditLayer(index);

        if (!activateEditing())
        {
            window.statusBar()->showMessage(QStringLiteral("Editable Attributes layer could not enter edit mode."));
            return;
        }

        const QVector<GisShapePoint> points = {
            GisShapePoint(-122.0, 36.0),
            GisShapePoint(-111.0, 42.0),
            GisShapePoint(-101.0, 34.5),
            GisShapePoint(-91.0, 41.0),
            GisShapePoint(-80.0, 33.0)
        };

        for (int i = 0; i < points.size(); ++i)
        {
            QHash<QString, QVariant> attributes;
            attributes.insert(QStringLiteral("Name"), QStringLiteral("Site %1").arg(i + 1));
            attributes.insert(QStringLiteral("Status"), i % 2 == 0 ? QStringLiteral("Planned") : QStringLiteral("Active"));
            attributes.insert(QStringLiteral("Priority"), i + 1);
            viewer->addPointToEditLayer(pointLayerIndex(), points[i], attributes);
        }

        nameEdit->clear();
        statusCombo->setCurrentIndex(0);
        prioritySpin->setValue(1);
        viewer->setActiveTool(GisViewerTool::Info);
        selectAction->setChecked(true);
        refreshViewer(*viewer);
        updateUi();
        window.statusBar()->showMessage(QStringLiteral("Select a point, edit attributes, then Apply Attributes."));
    };

    populatePoints();

    QObject::connect(selectAction, &QAction::triggered, viewer, [&, selectAction](bool checked) {
        viewer->setActiveTool(checked ? GisViewerTool::Info : GisViewerTool::Pan);
    });

    QObject::connect(viewer, &GisViewer::activeToolChanged, viewer, [selectAction](GisViewerTool tool) {
        selectAction->setChecked(tool == GisViewerTool::Info);
    });

    QObject::connect(viewer, &GisViewer::mapClicked, viewer, [&](
        GisViewerTool tool,
        const QPointF& screenPoint,
        const GisShapePoint&,
        Qt::KeyboardModifiers) {
        if (tool != GisViewerTool::Info)
            return;

        const FeatureHitTestResult result = viewer->hitTestTopFeatureAt(screenPoint, 8);
        if (!result.isValid() || result.layer != pointLayerPtr)
        {
            viewer->clearSelectedFeatures();
            window.statusBar()->showMessage(QStringLiteral("No editable feature found."));
            updateUi();
            return;
        }

        viewer->setSelectedFeature(result);
        loadFormFromAttributes(result.attributes(), *nameEdit, *statusCombo, *prioritySpin);
        window.statusBar()->showMessage(QStringLiteral("Selected shape %1.").arg(result.featureId));
        updateUi();
    });

    QObject::connect(table, &QTableWidget::cellClicked, viewer, [&](int row, int) {
        const QTableWidgetItem* idItem = table->item(row, 0);
        if (idItem == nullptr)
            return;

        const int shapeId = idItem->text().toInt();
        const int rowIndex = shapeId - 1;
        if (pointLayerPtr == nullptr || rowIndex < 0 || rowIndex >= pointLayerPtr->count())
            return;

        QHash<QString, QVariant> attributes = pointLayerPtr->attributesForRow(rowIndex);
        loadFormFromAttributes(attributes, *nameEdit, *statusCombo, *prioritySpin);

        FeatureHitTestResult result;
        result.layer = pointLayerPtr;
        result.shape = pointLayerPtr->getShape(shapeId);
        result.featureId = shapeId;
        result.layerIndex = pointLayerIndex();
        viewer->setSelectedFeature(result);
        updateUi();
    });

    QObject::connect(applyAction, &QAction::triggered, viewer, [&] {
        const int index = pointLayerIndex();
        const int shapeId = selectedShapeId(*viewer, pointLayerPtr);
        if (index < 0 || shapeId <= 0)
        {
            window.statusBar()->showMessage(QStringLiteral("Select a feature first."));
            return;
        }

        QHash<QString, QVariant> attributes;
        attributes.insert(QStringLiteral("Name"), nameEdit->text());
        attributes.insert(QStringLiteral("Status"), statusCombo->currentText());
        attributes.insert(QStringLiteral("Priority"), prioritySpin->value());

        if (!viewer->setShapeAttributesInEditLayer(index, shapeId, attributes))
        {
            window.statusBar()->showMessage(QStringLiteral("setShapeAttributesInEditLayer failed."));
            return;
        }

        refreshViewer(*viewer);
        updateUi();
        window.statusBar()->showMessage(QStringLiteral("setShapeAttributesInEditLayer(%1, %2, attributes) succeeded.")
            .arg(index)
            .arg(shapeId));
    });

    QObject::connect(undoAction, &QAction::triggered, viewer, [&] {
        const int index = pointLayerIndex();
        viewer->undoEditLayer(index);
        refreshViewer(*viewer);
        updateUi();
    });

    QObject::connect(redoAction, &QAction::triggered, viewer, [&] {
        const int index = pointLayerIndex();
        viewer->redoEditLayer(index);
        refreshViewer(*viewer);
        updateUi();
    });

    QObject::connect(resetAction, &QAction::triggered, viewer, populatePoints);
    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);
    QObject::connect(viewer, &GisViewer::layerEditStateChanged, viewer, [&](GisLayer* layer) {
        if (layer == pointLayerPtr)
            updateUi();
    });

    window.show();
    viewer->setViewExtent(GisExtent(-132.0, 18.0, -60.0, 55.0));

    return app.exec();
}
