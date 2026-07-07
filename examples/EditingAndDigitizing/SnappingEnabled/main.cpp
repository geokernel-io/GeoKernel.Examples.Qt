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
        QStringLiteral("SnappingEnabled"),
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

std::unique_ptr<GisLayerVector> createPolylineLayer()
{
    auto layer = GisLayerVector::createInMemory(
        QStringLiteral("Snapping Lines"),
        GisShapeType::Polyline,
        GisExtent(-180.0, -90.0, 180.0, 90.0));

    layer->addAttributeDefinition({ QStringLiteral("Name"), GisAttributeType::String, 64, 0 });
    layer->addAttributeDefinition({ QStringLiteral("Kind"), GisAttributeType::String, 32, 0 });
    layer->style() = lineStyle();
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

QString stateText(const GisViewer& viewer, const GisLayerVector* layer)
{
    QStringList lines;
    lines << QStringLiteral("Snapping APIs:");
    lines << QStringLiteral("- setEditSnappingEnabled(bool)");
    lines << QStringLiteral("- setEditSnappingTolerancePixels(double)");
    lines << QStringLiteral("");
    lines << QStringLiteral("How to test:");
    lines << QStringLiteral("- Add Polyline is active.");
    lines << QStringLiteral("- Draw near the existing guide line vertices/segments.");
    lines << QStringLiteral("- Toggle Snapping off and draw again.");
    lines << QStringLiteral("- Increase/decrease tolerance and compare.");
    lines << QStringLiteral("- Finish a line with Enter or double-click.");
    lines << QStringLiteral("");
    lines << QStringLiteral("Snapping enabled: %1").arg(viewer.editSnappingEnabled() ? QStringLiteral("true") : QStringLiteral("false"));
    lines << QStringLiteral("Tolerance: %1 px").arg(viewer.editSnappingTolerancePixels(), 0, 'f', 0);
    lines << QStringLiteral("Line feature count: %1").arg(layer != nullptr ? layer->count() : 0);
    lines << QStringLiteral("User-drawn lines: %1").arg(layer != nullptr ? qMax(0, layer->count() - 1) : 0);
    return lines.join(QStringLiteral("\n"));
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
    window.setWindowTitle(QStringLiteral("SnappingEnabled"));
    window.statusBar()->showMessage(QStringLiteral("Draw near the guide line. Toggle snapping and tolerance to compare."));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::AddPolyline);
    viewer->setEditSnappingEnabled(true);
    viewer->setEditSnappingTolerancePixels(14.0);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QActionGroup toolGroup(&window);
    toolGroup.setExclusive(true);
    QAction* addLineAction = addToolAction(*toolbar, toolGroup, QStringLiteral("Add Polyline"), QStringLiteral("Polyline.svg"), *viewer, GisViewerTool::AddPolyline);
    addToolAction(*toolbar, toolGroup, QStringLiteral("Pan"), QStringLiteral("Pan.svg"), *viewer, GisViewerTool::Pan);
    addLineAction->setChecked(true);

    toolbar->addSeparator();
    QAction* snappingAction = toolbar->addAction(sampleIcon(QStringLiteral("SnapEnableDisable.svg")), QStringLiteral("Snapping"));
    snappingAction->setCheckable(true);
    snappingAction->setChecked(true);

    toolbar->addWidget(new QLabel(QStringLiteral("Tolerance px"), &window));
    auto* toleranceSpin = new QSpinBox(&window);
    toleranceSpin->setRange(1, 64);
    toleranceSpin->setValue(14);
    toolbar->addWidget(toleranceSpin);

    QAction* resetAction = toolbar->addAction(sampleIcon(QStringLiteral("Refresh.svg")), QStringLiteral("Reset Guide"));
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));

    auto* statusLabel = new QLabel(&window);
    statusLabel->setContentsMargins(12, 0, 12, 0);
    toolbar->addWidget(statusLabel);

    auto* info = new QPlainTextEdit(&window);
    info->setReadOnly(true);
    info->setMinimumWidth(360);
    auto* infoDock = new QDockWidget(QStringLiteral("Snapping state"), &window);
    infoDock->setWidget(info);
    window.addDockWidget(Qt::RightDockWidgetArea, infoDock);

    if (!loadLayer(*viewer, sampleDataPath(QStringLiteral("shapefile/world_4326.shp"))))
        return 1;

    if (auto* worldLayer = viewer->mapLayerAt(0))
    {
        worldLayer->setName(QStringLiteral("World"));
        worldLayer->style() = worldStyle();
    }

    auto lineLayer = createPolylineLayer();
    auto* lineLayerPtr = lineLayer.get();
    viewer->addLayer(lineLayer);

    const auto lineLayerIndex = [&]() {
        return layerIndexOf(*viewer, lineLayerPtr);
    };

    const auto activateEditing = [&]() -> bool {
        const int index = lineLayerIndex();
        if (index < 0)
            return false;

        if (!viewer->isLayerEditing(index) && !viewer->beginEditLayer(index))
            return false;

        return viewer->setActiveEditLayerIndex(index);
    };

    const auto updateUi = [&] {
        statusLabel->setText(QStringLiteral("Snapping: %1 | Tolerance: %2 px | Drawn lines: %3")
            .arg(viewer->editSnappingEnabled() ? QStringLiteral("ON") : QStringLiteral("OFF"))
            .arg(viewer->editSnappingTolerancePixels(), 0, 'f', 0)
            .arg(lineLayerPtr != nullptr ? qMax(0, lineLayerPtr->count() - 1) : 0));
        info->setPlainText(stateText(*viewer, lineLayerPtr));
    };

    const auto populateGuide = [&] {
        const int index = lineLayerIndex();
        if (index >= 0)
            viewer->rollbackEditLayer(index);

        if (!activateEditing())
        {
            window.statusBar()->showMessage(QStringLiteral("Snapping Lines layer could not enter edit mode."));
            return;
        }

        QHash<QString, QVariant> attributes;
        attributes.insert(QStringLiteral("Name"), QStringLiteral("Guide line"));
        attributes.insert(QStringLiteral("Kind"), QStringLiteral("Snap target"));
        viewer->addPolylineToEditLayer(lineLayerIndex(), {
            GisShapePoint(-123.0, 31.0),
            GisShapePoint(-116.0, 42.0),
            GisShapePoint(-106.0, 34.0),
            GisShapePoint(-96.0, 43.0),
            GisShapePoint(-86.0, 35.0),
            GisShapePoint(-76.0, 41.0)
        }, attributes);

        viewer->setActiveTool(GisViewerTool::AddPolyline);
        addLineAction->setChecked(true);
        refreshViewer(*viewer);
        updateUi();
        window.statusBar()->showMessage(QStringLiteral("Guide line reset. Draw near it to test snapping."));
    };

    populateGuide();

    QObject::connect(addLineAction, &QAction::triggered, viewer, [&] {
        if (!activateEditing())
            return;

        viewer->setActiveTool(GisViewerTool::AddPolyline);
        window.statusBar()->showMessage(QStringLiteral("Add Polyline active. Click vertices, then Enter or double-click."));
    });

    QObject::connect(snappingAction, &QAction::toggled, viewer, [&](bool enabled) {
        viewer->setEditSnappingEnabled(enabled);
        updateUi();
        window.statusBar()->showMessage(enabled ? QStringLiteral("Snapping enabled.") : QStringLiteral("Snapping disabled."));
    });

    QObject::connect(toleranceSpin, qOverload<int>(&QSpinBox::valueChanged), viewer, [&](int value) {
        viewer->setEditSnappingTolerancePixels(static_cast<double>(value));
        updateUi();
        window.statusBar()->showMessage(QStringLiteral("editSnappingTolerancePixels = %1").arg(value));
    });

    QObject::connect(resetAction, &QAction::triggered, viewer, populateGuide);
    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);

    QObject::connect(viewer, &GisViewer::layerEditStateChanged, viewer, [&](GisLayer* layer) {
        if (layer != lineLayerPtr)
            return;

        refreshViewer(*viewer);
        updateUi();
    });

    QObject::connect(viewer, &GisViewer::mouseCoordinatesChanged, viewer, [&](const QPointF&, const QPointF& worldPoint) {
        if (viewer->activeTool() != GisViewerTool::AddPolyline)
            return;

        window.statusBar()->showMessage(QStringLiteral("Add Polyline. x=%1, y=%2 | snapping=%3 | tolerance=%4px")
            .arg(worldPoint.x(), 0, 'f', 4)
            .arg(worldPoint.y(), 0, 'f', 4)
            .arg(viewer->editSnappingEnabled() ? QStringLiteral("on") : QStringLiteral("off"))
            .arg(viewer->editSnappingTolerancePixels(), 0, 'f', 0));
    });

    window.show();
    viewer->setViewExtent(GisExtent(-132.0, 18.0, -60.0, 55.0));

    return app.exec();
}
