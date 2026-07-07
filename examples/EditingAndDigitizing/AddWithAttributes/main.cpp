#include <memory>

#include <QAction>
#include <QAbstractItemView>
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
        QStringLiteral("AddWithAttributes"),
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
    style.setPointSize(9.5f);
    style.setLineWidth(1.2f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("Name"));
    style.setLabelFontSize(10.0f);
    style.setLabelColor(QStringLiteral("#263238"));
    style.setLabelHaloEnabled(true);
    style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
    style.setLabelHaloWidth(2.0f);
    style.setLabelOffsetY(-11.0f);
    style.setLabelAllowOverlap(true);
    return style;
}

std::unique_ptr<GisLayerVector> createPointLayer()
{
    auto layer = GisLayerVector::createInMemory(
        QStringLiteral("Points With Attributes"),
        GisShapeType::Point,
        GisExtent(-180.0, -90.0, 180.0, 90.0));

    layer->addAttributeDefinition({ QStringLiteral("Name"), GisAttributeType::String, 64, 0 });
    layer->addAttributeDefinition({ QStringLiteral("Category"), GisAttributeType::String, 32, 0 });
    layer->addAttributeDefinition({ QStringLiteral("Score"), GisAttributeType::Integer, 8, 0 });
    layer->addAttributeDefinition({ QStringLiteral("Source"), GisAttributeType::String, 32, 0 });
    layer->style() = pointStyle();
    return layer;
}

GisShapePoint samplePointAt(int index)
{
    static constexpr double xMin = -123.0;
    static constexpr double yMin = 29.0;
    static constexpr double xStep = 5.0;
    static constexpr double yStep = 4.0;
    static constexpr int columns = 12;

    const int column = index % columns;
    const int row = index / columns;
    return GisShapePoint(xMin + column * xStep, yMin + row * yStep);
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

void appendAttributesRow(QTableWidget& table, int featureNo, const QHash<QString, QVariant>& attributes)
{
    const int row = table.rowCount();
    table.insertRow(row);

    table.setItem(row, 0, new QTableWidgetItem(QString::number(featureNo)));
    table.setItem(row, 1, new QTableWidgetItem(attributes.value(QStringLiteral("Name")).toString()));
    table.setItem(row, 2, new QTableWidgetItem(attributes.value(QStringLiteral("Category")).toString()));
    table.setItem(row, 3, new QTableWidgetItem(attributes.value(QStringLiteral("Score")).toString()));
    table.setItem(row, 4, new QTableWidgetItem(attributes.value(QStringLiteral("Source")).toString()));
    table.scrollToBottom();
}

void selectAttributesRow(QTableWidget& table, int featureId)
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

QString formatFeatureAttributes(const FeatureHitTestResult& result)
{
    if (!result.isValid())
        return QStringLiteral("No feature found.");

    QStringList lines;
    lines << QStringLiteral("Layer: %1").arg(result.layer != nullptr ? result.layer->name() : QStringLiteral("-"));
    lines << QStringLiteral("Feature ID: %1").arg(result.featureId);
    lines << QString();

    const QHash<QString, QVariant> attributes = result.attributes();
    QStringList keys = attributes.keys();
    keys.sort(Qt::CaseInsensitive);

    for (const QString& key : keys)
        lines << QStringLiteral("%1 = %2").arg(key, attributes.value(key).toString());

    return lines.join(QStringLiteral("\n"));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("AddWithAttributes"));
    window.statusBar()->showMessage(QStringLiteral("Click Add Point to call addPointToEditLayer(index, worldPoint, attributes)."));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QAction* addPointAction = toolbar->addAction(sampleIcon(QStringLiteral("Point.svg")), QStringLiteral("Add Point With Attributes"));
    QAction* infoAction = toolbar->addAction(sampleIcon(QStringLiteral("Identify.svg")), QStringLiteral("Info"));
    infoAction->setCheckable(true);
    QAction* clearAction = toolbar->addAction(sampleIcon(QStringLiteral("Delete.svg")), QStringLiteral("Clear Points"));
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));

    auto* countLabel = new QLabel(&window);
    countLabel->setContentsMargins(12, 0, 12, 0);
    toolbar->addWidget(countLabel);

    auto* table = new QTableWidget(&window);
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels({
        QStringLiteral("#"),
        QStringLiteral("Name"),
        QStringLiteral("Category"),
        QStringLiteral("Score"),
        QStringLiteral("Source")
    });
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setMaximumHeight(155);
    auto* dock = new QDockWidget(QStringLiteral("Added point attributes"), &window);
    dock->setWidget(table);
    window.addDockWidget(Qt::BottomDockWidgetArea, dock);

    auto* infoText = new QPlainTextEdit(&window);
    infoText->setReadOnly(true);
    infoText->setPlainText(QStringLiteral("Click Info, then click an added point to read its QHash attributes from the feature."));
    auto* infoDock = new QDockWidget(QStringLiteral("Info result"), &window);
    infoDock->setWidget(infoText);
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

    int pointCursor = 0;

    const auto updateCount = [&] {
        countLabel->setText(QStringLiteral("Feature count: %1").arg(pointLayerPtr != nullptr ? pointLayerPtr->count() : 0));
    };

    const auto activatePointEditing = [&]() -> int {
        const int pointLayerIndex = layerIndexOf(*viewer, pointLayerPtr);
        if (pointLayerIndex < 0)
        {
            window.statusBar()->showMessage(QStringLiteral("Points With Attributes layer is not in the viewer."));
            return -1;
        }

        if (!viewer->isLayerEditing(pointLayerIndex) && !viewer->beginEditLayer(pointLayerIndex))
        {
            window.statusBar()->showMessage(QStringLiteral("Points With Attributes layer could not enter edit mode."));
            return -1;
        }

        if (!viewer->setActiveEditLayerIndex(pointLayerIndex))
        {
            window.statusBar()->showMessage(QStringLiteral("Points With Attributes layer could not be activated for editing."));
            return -1;
        }

        return pointLayerIndex;
    };

    activatePointEditing();
    updateCount();

    QObject::connect(addPointAction, &QAction::triggered, viewer, [&] {
        const int pointLayerIndex = activatePointEditing();
        if (pointLayerIndex < 0)
            return;

        const int featureNo = pointCursor + 1;
        const GisShapePoint worldPoint = samplePointAt(pointCursor);

        QHash<QString, QVariant> attributes;
        attributes.insert(QStringLiteral("Name"), QStringLiteral("Site %1").arg(featureNo));
        attributes.insert(QStringLiteral("Category"), featureNo % 2 == 0 ? QStringLiteral("Even") : QStringLiteral("Odd"));
        attributes.insert(QStringLiteral("Score"), featureNo * 10);
        attributes.insert(QStringLiteral("Source"), QStringLiteral("QHash"));

        if (!viewer->addPointToEditLayer(pointLayerIndex, worldPoint, attributes))
        {
            window.statusBar()->showMessage(QStringLiteral("Point could not be added."));
            return;
        }

        ++pointCursor;
        appendAttributesRow(*table, featureNo, attributes);
        refreshViewer(*viewer);
        updateCount();
        infoAction->setChecked(false);
        viewer->setActiveTool(GisViewerTool::Pan);
        window.statusBar()->showMessage(QStringLiteral("addPointToEditLayer(%1, [%2, %3], QHash attributes)")
            .arg(pointLayerIndex)
            .arg(worldPoint.x(), 0, 'f', 4)
            .arg(worldPoint.y(), 0, 'f', 4));
    });

    QObject::connect(clearAction, &QAction::triggered, viewer, [&] {
        const int pointLayerIndex = layerIndexOf(*viewer, pointLayerPtr);
        viewer->rollbackEditLayer(pointLayerIndex);
        activatePointEditing();
        pointCursor = 0;
        table->setRowCount(0);
        infoText->setPlainText(QStringLiteral("Click Info, then click an added point to read its QHash attributes from the feature."));
        refreshViewer(*viewer);
        updateCount();
        window.statusBar()->showMessage(QStringLiteral("Points with attributes cleared."));
    });

    QObject::connect(infoAction, &QAction::triggered, viewer, [&, infoAction](bool checked) {
        viewer->setActiveTool(checked ? GisViewerTool::Info : GisViewerTool::Pan);
        window.statusBar()->showMessage(checked
            ? QStringLiteral("Info mode: click an added point to read its attributes.")
            : QStringLiteral("Pan mode."));
    });

    QObject::connect(viewer, &GisViewer::activeToolChanged, viewer, [infoAction](GisViewerTool tool) {
        infoAction->setChecked(tool == GisViewerTool::Info);
    });

    QObject::connect(viewer, &GisViewer::mapClicked, viewer, [&, infoText](
        GisViewerTool tool,
        const QPointF& screenPoint,
        const GisShapePoint&,
        Qt::KeyboardModifiers) {
        if (tool != GisViewerTool::Info)
            return;

        const FeatureHitTestResult result = viewer->hitTestTopFeatureAt(screenPoint, 8);
        infoText->setPlainText(formatFeatureAttributes(result));

        if (result.isValid())
        {
            selectAttributesRow(*table, result.featureId);
            viewer->setSelectedFeature(result);
            window.statusBar()->showMessage(QStringLiteral("Attributes read from feature %1.").arg(result.featureId));
        }
        else
        {
            table->clearSelection();
            window.statusBar()->showMessage(QStringLiteral("No feature found under cursor."));
        }
    });

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);

    QObject::connect(viewer, &GisViewer::layerEditStateChanged, viewer, [&](GisLayer* layer) {
        if (layer != pointLayerPtr)
            return;

        refreshViewer(*viewer);
        updateCount();
    });

    window.show();
    viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 55.0));

    return app.exec();
}
