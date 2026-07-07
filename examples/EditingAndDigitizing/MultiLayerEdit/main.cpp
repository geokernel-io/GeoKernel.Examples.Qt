#include <memory>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QSize>
#include <QStatusBar>
#include <QString>
#include <QToolBar>
#include <QVariant>

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
        QStringLiteral("MultiLayerEdit"),
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

GisLayerStyle pointStyle(const QString& pointColor, const QString& lineColor)
{
    GisLayerStyle style;
    style.setPointColor(pointColor);
    style.setLineColor(lineColor);
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

std::unique_ptr<GisLayerVector> createPointLayer(
    const QString& name,
    const QString& pointColor,
    const QString& lineColor)
{
    auto layer = GisLayerVector::createInMemory(
        name,
        GisShapeType::Point,
        GisExtent(-180.0, -90.0, 180.0, 90.0));

    layer->addAttributeDefinition({ QStringLiteral("Name"), GisAttributeType::String, 64, 0 });
    layer->addAttributeDefinition({ QStringLiteral("Layer"), GisAttributeType::String, 32, 0 });
    layer->style() = pointStyle(pointColor, lineColor);
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

GisShapePoint redPointAt(int index)
{
    static constexpr double xMin = -124.0;
    static constexpr double yMin = 31.0;
    static constexpr double xStep = 7.5;
    static constexpr double yStep = 5.0;
    static constexpr int columns = 7;
    return GisShapePoint(xMin + (index % columns) * xStep, yMin + (index / columns) * yStep);
}

GisShapePoint bluePointAt(int index)
{
    static constexpr double xMin = -121.5;
    static constexpr double yMin = 33.0;
    static constexpr double xStep = 7.5;
    static constexpr double yStep = 5.0;
    static constexpr int columns = 7;
    return GisShapePoint(xMin + (index % columns) * xStep, yMin + (index / columns) * yStep);
}

void refreshViewer(GisViewer& viewer)
{
    viewer.invalidateRenderCache(true, true);
    viewer.refreshLayers();
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("MultiLayerEdit"));
    window.statusBar()->showMessage(QStringLiteral("Switch active edit layer, then add points to the active layer."));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QActionGroup activeLayerGroup(&window);
    activeLayerGroup.setExclusive(true);

    QAction* redLayerAction = toolbar->addAction(sampleIcon(QStringLiteral("Point.svg")), QStringLiteral("Active: Red Points"));
    redLayerAction->setCheckable(true);
    activeLayerGroup.addAction(redLayerAction);

    QAction* blueLayerAction = toolbar->addAction(sampleIcon(QStringLiteral("Point.svg")), QStringLiteral("Active: Blue Points"));
    blueLayerAction->setCheckable(true);
    activeLayerGroup.addAction(blueLayerAction);

    toolbar->addSeparator();
    QAction* addAction = toolbar->addAction(sampleIcon(QStringLiteral("Add.svg")), QStringLiteral("Add To Active Layer"));
    QAction* commitAllAction = toolbar->addAction(sampleIcon(QStringLiteral("SaveProject.svg")), QStringLiteral("Commit Both"));
    QAction* rollbackAllAction = toolbar->addAction(sampleIcon(QStringLiteral("PreviousExtent.svg")), QStringLiteral("Rollback Both"));
    QAction* resetAction = toolbar->addAction(sampleIcon(QStringLiteral("Refresh.svg")), QStringLiteral("Reset"));
    toolbar->addSeparator();
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));

    auto* stateLabel = new QLabel(&window);
    stateLabel->setContentsMargins(12, 0, 12, 0);
    toolbar->addWidget(stateLabel);

    if (!loadLayer(*viewer, sampleDataPath(QStringLiteral("shapefile/world_4326.shp"))))
        return 1;

    if (auto* worldLayer = viewer->mapLayerAt(0))
    {
        worldLayer->setName(QStringLiteral("World"));
        worldLayer->style() = worldStyle();
    }

    auto redLayer = createPointLayer(
        QStringLiteral("Red Points"),
        QStringLiteral("#D95D39"),
        QStringLiteral("#8C321D"));
    auto* redLayerPtr = redLayer.get();
    viewer->addLayer(redLayer);

    auto blueLayer = createPointLayer(
        QStringLiteral("Blue Points"),
        QStringLiteral("#2563EB"),
        QStringLiteral("#1E3A8A"));
    auto* blueLayerPtr = blueLayer.get();
    viewer->addLayer(blueLayer);

    int redCursor = 0;
    int blueCursor = 0;

    const auto redLayerIndex = [&]() {
        return layerIndexOf(*viewer, redLayerPtr);
    };

    const auto blueLayerIndex = [&]() {
        return layerIndexOf(*viewer, blueLayerPtr);
    };

    const auto updateUi = [&]() {
        const int activeIndex = viewer->activeEditLayerIndex();
        const QString activeName = activeIndex >= 0 && viewer->mapLayerAt(activeIndex) != nullptr
            ? viewer->mapLayerAt(activeIndex)->name()
            : QStringLiteral("-");

        stateLabel->setText(QStringLiteral("Active edit layer: %1 (%2) | Red: %3 | Blue: %4")
            .arg(activeIndex)
            .arg(activeName)
            .arg(redLayerPtr != nullptr ? redLayerPtr->count() : 0)
            .arg(blueLayerPtr != nullptr ? blueLayerPtr->count() : 0));
    };

    const auto beginBothLayers = [&]() {
        const int redIndex = redLayerIndex();
        const int blueIndex = blueLayerIndex();

        if (redIndex >= 0 && !viewer->isLayerEditing(redIndex))
            viewer->beginEditLayer(redIndex);
        if (blueIndex >= 0 && !viewer->isLayerEditing(blueIndex))
            viewer->beginEditLayer(blueIndex);
    };

    const auto setActiveLayer = [&](GisLayerVector* layer) {
        beginBothLayers();

        const int index = layerIndexOf(*viewer, layer);
        if (index >= 0 && viewer->setActiveEditLayerIndex(index))
        {
            window.statusBar()->showMessage(QStringLiteral("setActiveEditLayerIndex(%1) -> %2")
                .arg(index)
                .arg(layer != nullptr ? layer->name() : QStringLiteral("-")));
        }

        updateUi();
    };

    const auto resetLayers = [&]() {
        const int redIndex = redLayerIndex();
        const int blueIndex = blueLayerIndex();

        if (redIndex >= 0 && viewer->isLayerEditing(redIndex))
            viewer->rollbackEditLayer(redIndex);
        if (blueIndex >= 0 && viewer->isLayerEditing(blueIndex))
            viewer->rollbackEditLayer(blueIndex);

        beginBothLayers();
        viewer->setActiveEditLayerIndex(redLayerIndex());
        redLayerAction->setChecked(true);
        redCursor = 0;
        blueCursor = 0;
        refreshViewer(*viewer);
        updateUi();
        window.statusBar()->showMessage(QStringLiteral("Both edit layers reset. Red Points is active."));
    };

    beginBothLayers();
    viewer->setActiveEditLayerIndex(redLayerIndex());
    redLayerAction->setChecked(true);
    updateUi();

    QObject::connect(redLayerAction, &QAction::triggered, viewer, [&] {
        setActiveLayer(redLayerPtr);
    });

    QObject::connect(blueLayerAction, &QAction::triggered, viewer, [&] {
        setActiveLayer(blueLayerPtr);
    });

    QObject::connect(addAction, &QAction::triggered, viewer, [&] {
        beginBothLayers();

        const int activeIndex = viewer->activeEditLayerIndex();
        GisLayer* activeLayer = viewer->mapLayerAt(activeIndex);
        if (activeLayer == nullptr)
        {
            window.statusBar()->showMessage(QStringLiteral("No active edit layer."));
            return;
        }

        const bool redActive = activeLayer == redLayerPtr;
        const GisShapePoint point = redActive ? redPointAt(redCursor) : bluePointAt(blueCursor);
        QHash<QString, QVariant> attributes;
        attributes.insert(QStringLiteral("Layer"), activeLayer->name());
        attributes.insert(QStringLiteral("Name"), QStringLiteral("%1 %2")
            .arg(activeLayer->name())
            .arg(redActive ? redCursor + 1 : blueCursor + 1));

        if (!viewer->addPointToActiveEditLayer(point, attributes))
        {
            window.statusBar()->showMessage(QStringLiteral("addPointToActiveEditLayer failed."));
            return;
        }

        if (redActive)
            ++redCursor;
        else
            ++blueCursor;

        refreshViewer(*viewer);
        updateUi();
        window.statusBar()->showMessage(QStringLiteral("Added point to active edit layer: %1").arg(activeLayer->name()));
    });

    QObject::connect(commitAllAction, &QAction::triggered, viewer, [&] {
        const int redIndex = redLayerIndex();
        const int blueIndex = blueLayerIndex();
        if (redIndex >= 0 && viewer->isLayerEditing(redIndex))
            viewer->commitEditLayer(redIndex);
        if (blueIndex >= 0 && viewer->isLayerEditing(blueIndex))
            viewer->commitEditLayer(blueIndex);

        beginBothLayers();
        viewer->setActiveEditLayerIndex(redLayerAction->isChecked() ? redLayerIndex() : blueLayerIndex());
        refreshViewer(*viewer);
        updateUi();
        window.statusBar()->showMessage(QStringLiteral("Both edit layers committed and reopened for editing."));
    });

    QObject::connect(rollbackAllAction, &QAction::triggered, viewer, resetLayers);
    QObject::connect(resetAction, &QAction::triggered, viewer, resetLayers);
    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);

    QObject::connect(viewer, &GisViewer::layerEditStateChanged, viewer, [&](GisLayer* layer) {
        if (layer != redLayerPtr && layer != blueLayerPtr)
            return;

        refreshViewer(*viewer);
        updateUi();
    });

    window.show();
    viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 55.0));

    return app.exec();
}
