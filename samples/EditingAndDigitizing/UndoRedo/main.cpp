#include <memory>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
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

#include "Viewer/GisViewer.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShapePoint.h"
#include "Layers/GisLayerStyle.h"
#include "Layers/GisLayerVector.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;

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
        QStringLiteral("UndoRedo"),
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
    style.setPointSize(13.0f);
    style.setLineWidth(1.4f);
    return style;
}

std::unique_ptr<GisLayerVector> createPointLayer()
{
    auto layer = GisLayerVector::createInMemory(
        QStringLiteral("Undo Redo Points"),
        GisShapeType::Point,
        GisExtent(-180.0, -90.0, 180.0, 90.0));
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

QString layerStateText(const GisViewer& viewer, int layerIndex, const GisLayerVector* layer, int clickStepCount)
{
    QStringList lines;
    lines << QStringLiteral("Interactive undo/redo:");
    lines << QStringLiteral("- Add Point tool is active by default.");
    lines << QStringLiteral("- Click the map 5 times to create 5 independent edit commands.");
    lines << QStringLiteral("- Undo calls undoEditLayer(index).");
    lines << QStringLiteral("- Redo calls redoEditLayer(index).");
    lines << QStringLiteral("- Undo 5 / Redo 5 call the same API repeatedly.");
    lines << QStringLiteral("");
    lines << QStringLiteral("Layer index: %1").arg(layerIndex);
    lines << QStringLiteral("Click steps created: %1").arg(clickStepCount);
    lines << QStringLiteral("Visible point count: %1").arg(layer != nullptr ? layer->count() : 0);
    lines << QStringLiteral("Can undo: %1").arg(viewer.canUndoEditLayer(layerIndex) ? QStringLiteral("true") : QStringLiteral("false"));
    lines << QStringLiteral("Can redo: %1").arg(viewer.canRedoEditLayer(layerIndex) ? QStringLiteral("true") : QStringLiteral("false"));

    if (layer != nullptr)
    {
        lines << QStringLiteral("");
        lines << QStringLiteral("Visible points:");
        for (int row = 0; row < layer->count(); ++row)
        {
            const int shapeId = row + 1;
            const auto* point = dynamic_cast<const GisShapePoint*>(layer->getShape(shapeId));
            if (point == nullptr)
                continue;

            lines << QStringLiteral("- Point %1: %2, %3")
                .arg(shapeId)
                .arg(point->x(), 0, 'f', 3)
                .arg(point->y(), 0, 'f', 3);
        }
    }

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
    window.setWindowTitle(QStringLiteral("UndoRedo"));
    window.statusBar()->showMessage(QStringLiteral("Click the map 5 times, then use Undo/Redo."));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::AddPoint);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QActionGroup toolGroup(&window);
    toolGroup.setExclusive(true);
    QAction* addPointAction = addToolAction(*toolbar, toolGroup, QStringLiteral("Add Point"), QStringLiteral("Point.svg"), *viewer, GisViewerTool::AddPoint);
    addToolAction(*toolbar, toolGroup, QStringLiteral("Pan"), QStringLiteral("Pan.svg"), *viewer, GisViewerTool::Pan);
    addPointAction->setChecked(true);

    toolbar->addSeparator();
    QAction* undoAction = toolbar->addAction(sampleIcon(QStringLiteral("PreviousExtent.svg")), QStringLiteral("Undo"));
    QAction* redoAction = toolbar->addAction(sampleIcon(QStringLiteral("NextExtent.svg")), QStringLiteral("Redo"));
    QAction* undoFiveAction = toolbar->addAction(sampleIcon(QStringLiteral("PreviousExtent.svg")), QStringLiteral("Undo 5"));
    QAction* redoFiveAction = toolbar->addAction(sampleIcon(QStringLiteral("NextExtent.svg")), QStringLiteral("Redo 5"));
    QAction* resetAction = toolbar->addAction(sampleIcon(QStringLiteral("Refresh.svg")), QStringLiteral("Reset"));
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));

    auto* statusLabel = new QLabel(&window);
    statusLabel->setContentsMargins(12, 0, 12, 0);
    toolbar->addWidget(statusLabel);

    auto* info = new QPlainTextEdit(&window);
    info->setReadOnly(true);
    info->setMinimumWidth(360);
    auto* infoDock = new QDockWidget(QStringLiteral("Undo / Redo state"), &window);
    infoDock->setWidget(info);
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

    int clickStepCount = 0;

    const auto pointLayerIndex = [&]() {
        return layerIndexOf(*viewer, pointLayerPtr);
    };

    const auto activatePointEditing = [&]() -> bool {
        const int index = pointLayerIndex();
        if (index < 0)
            return false;

        if (!viewer->isLayerEditing(index) && !viewer->beginEditLayer(index))
            return false;

        return viewer->setActiveEditLayerIndex(index);
    };

    const auto updateUi = [&] {
        const int index = pointLayerIndex();
        const bool canUndo = viewer->canUndoEditLayer(index);
        const bool canRedo = viewer->canRedoEditLayer(index);
        undoAction->setEnabled(canUndo);
        undoFiveAction->setEnabled(canUndo);
        redoAction->setEnabled(canRedo);
        redoFiveAction->setEnabled(canRedo);
        statusLabel->setText(QStringLiteral("Visible points: %1 | Click steps: %2 | Undo: %3 | Redo: %4")
            .arg(pointLayerPtr != nullptr ? pointLayerPtr->count() : 0)
            .arg(clickStepCount)
            .arg(canUndo ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(canRedo ? QStringLiteral("yes") : QStringLiteral("no")));
        info->setPlainText(layerStateText(*viewer, index, pointLayerPtr, clickStepCount));
    };

    const auto resetLayer = [&] {
        const int index = pointLayerIndex();
        if (index >= 0)
            viewer->rollbackEditLayer(index);

        clickStepCount = 0;
        activatePointEditing();
        viewer->setActiveTool(GisViewerTool::AddPoint);
        addPointAction->setChecked(true);
        refreshViewer(*viewer);
        updateUi();
        window.statusBar()->showMessage(QStringLiteral("Reset complete. Click the map to create undoable edit steps."));
    };

    activatePointEditing();

    QObject::connect(addPointAction, &QAction::triggered, viewer, [&] {
        if (!activatePointEditing())
        {
            window.statusBar()->showMessage(QStringLiteral("Undo Redo Points layer could not enter edit mode."));
            return;
        }

        viewer->setActiveTool(GisViewerTool::AddPoint);
        window.statusBar()->showMessage(QStringLiteral("Add Point active. Click the map to create an undoable step."));
    });

    QObject::connect(viewer, &GisViewer::mapClicked, viewer, [&](GisViewerTool tool, const QPointF&, const GisShapePoint& point, Qt::KeyboardModifiers) {
        if (tool != GisViewerTool::AddPoint)
            return;

        ++clickStepCount;
        refreshViewer(*viewer);
        updateUi();
        window.statusBar()->showMessage(QStringLiteral("Step %1 created at x=%2, y=%3")
            .arg(clickStepCount)
            .arg(point.x(), 0, 'f', 4)
            .arg(point.y(), 0, 'f', 4));
    });

    QObject::connect(undoAction, &QAction::triggered, viewer, [&] {
        const int index = pointLayerIndex();
        if (viewer->undoEditLayer(index))
            window.statusBar()->showMessage(QStringLiteral("undoEditLayer(%1) succeeded.").arg(index));
        else
            window.statusBar()->showMessage(QStringLiteral("Nothing to undo."));

        refreshViewer(*viewer);
        updateUi();
    });

    QObject::connect(redoAction, &QAction::triggered, viewer, [&] {
        const int index = pointLayerIndex();
        if (viewer->redoEditLayer(index))
            window.statusBar()->showMessage(QStringLiteral("redoEditLayer(%1) succeeded.").arg(index));
        else
            window.statusBar()->showMessage(QStringLiteral("Nothing to redo."));

        refreshViewer(*viewer);
        updateUi();
    });

    QObject::connect(undoFiveAction, &QAction::triggered, viewer, [&] {
        const int index = pointLayerIndex();
        int count = 0;
        for (int i = 0; i < 5; ++i)
        {
            if (!viewer->undoEditLayer(index))
                break;
            ++count;
        }

        refreshViewer(*viewer);
        updateUi();
        window.statusBar()->showMessage(QStringLiteral("undoEditLayer(%1) called successfully %2 time(s).").arg(index).arg(count));
    });

    QObject::connect(redoFiveAction, &QAction::triggered, viewer, [&] {
        const int index = pointLayerIndex();
        int count = 0;
        for (int i = 0; i < 5; ++i)
        {
            if (!viewer->redoEditLayer(index))
                break;
            ++count;
        }

        refreshViewer(*viewer);
        updateUi();
        window.statusBar()->showMessage(QStringLiteral("redoEditLayer(%1) called successfully %2 time(s).").arg(index).arg(count));
    });

    QObject::connect(resetAction, &QAction::triggered, viewer, resetLayer);
    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);
    QObject::connect(viewer, &GisViewer::layerEditStateChanged, viewer, [&](GisLayer* layer) {
        if (layer == pointLayerPtr)
            updateUi();
    });

    updateUi();
    window.show();

    viewer->setViewExtent(GisExtent(-132.0, 18.0, -60.0, 55.0));

    return app.exec();
}
