#include <memory>

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QString>
#include <QToolBar>

#include "Layers/GisLayerStyle.h"
#include "Layers/GisLayerVector.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShapePoint.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Viewer;

constexpr int WorldLayerIndex = 0;

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
        QStringLiteral("EditDirtyState"),
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
    return style;
}

std::unique_ptr<GisLayerVector> createEditLayer()
{
    auto layer = GisLayerVector::createInMemory(
        QStringLiteral("Dirty State Points"),
        GisShapeType::Point,
        GisExtent(-180.0, -90.0, 180.0, 90.0));
    layer->style() = pointStyle();
    layer->addPoint(GisShapePoint(-122.4194, 37.7749));
    layer->addPoint(GisShapePoint(-118.2437, 34.0522));
    layer->addPoint(GisShapePoint(-112.0740, 33.4484));
    layer->clearEditHistory();
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

GisShapePoint generatedEditPoint(int index)
{
    const int column = index % 8;
    const int row = (index / 8) % 4;
    return GisShapePoint(-124.0 + column * 7.5, 25.0 + row * 5.2);
}

void refreshViewer(GisViewer& viewer)
{
    viewer.invalidateRenderCache(true, true);
    viewer.refreshLayers();
}

void appendLog(QPlainTextEdit& log, const QString& text)
{
    log.appendPlainText(QStringLiteral("%1 | %2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")), text));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("EditDirtyState"));
    window.statusBar()->showMessage(QStringLiteral("Use Add Feature to turn isLayerDirty on, then commit or rollback."));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    QAction* beginEditAction = toolbar->addAction(QStringLiteral("Begin Edit"));
    QAction* addFeatureAction = toolbar->addAction(QStringLiteral("Add Feature"));
    toolbar->addSeparator();
    QAction* commitEditAction = toolbar->addAction(QStringLiteral("Commit Edit"));
    QAction* rollbackEditAction = toolbar->addAction(QStringLiteral("Rollback Edit"));
    toolbar->addSeparator();
    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));

    auto* stateLabel = new QLabel(&window);
    stateLabel->setContentsMargins(12, 0, 12, 0);
    toolbar->addWidget(stateLabel);

    auto* log = new QPlainTextEdit(&window);
    log->setReadOnly(true);
    log->setMaximumBlockCount(200);
    log->setMinimumHeight(120);
    auto* dock = new QDockWidget(QStringLiteral("layerEditStateChanged log"), &window);
    dock->setWidget(log);
    window.addDockWidget(Qt::BottomDockWidgetArea, dock);

    if (!loadLayer(*viewer, sampleDataPath(QStringLiteral("shapefile/world_4326.shp"))))
        return 1;

    if (auto* worldLayer = viewer->mapLayerAt(WorldLayerIndex))
    {
        worldLayer->setName(QStringLiteral("World"));
        worldLayer->style() = worldStyle();
    }

    auto editLayer = createEditLayer();
    auto* editLayerPtr = editLayer.get();
    viewer->addLayer(editLayer);

    int editPointCursor = 0;
    int editStateSignalCount = 0;

    const auto editLayerIndex = [&]() {
        return layerIndexOf(*viewer, editLayerPtr);
    };

    const auto updateUi = [&]() {
        const int index = editLayerIndex();
        const bool hasLayer = index >= 0;
        const bool editing = hasLayer && viewer->isLayerEditing(index);
        const bool dirty = hasLayer && viewer->isLayerDirty(index);

        beginEditAction->setEnabled(hasLayer && !editing);
        addFeatureAction->setEnabled(editing);
        commitEditAction->setEnabled(editing);
        rollbackEditAction->setEnabled(editing);
        stateLabel->setText(QStringLiteral("Layer index: %1 | Editing: %2 | Dirty: %3 | Signals: %4 | Feature count: %5")
            .arg(index)
            .arg(editing ? QStringLiteral("ON") : QStringLiteral("OFF"))
            .arg(dirty ? QStringLiteral("YES") : QStringLiteral("NO"))
            .arg(editStateSignalCount)
            .arg(editLayerPtr != nullptr ? editLayerPtr->count() : 0));
    };

    QObject::connect(beginEditAction, &QAction::triggered, viewer, [&] {
        const int index = editLayerIndex();
        if (viewer->beginEditLayer(index))
        {
            appendLog(*log, QStringLiteral("beginEditLayer(%1); isLayerDirty=%2")
                .arg(index)
                .arg(viewer->isLayerDirty(index) ? QStringLiteral("true") : QStringLiteral("false")));
            window.statusBar()->showMessage(QStringLiteral("Edit session started. Dirty is still false until a change is made."));
            updateUi();
        }
    });

    QObject::connect(addFeatureAction, &QAction::triggered, viewer, [&] {
        const int index = editLayerIndex();
        const GisShapePoint point = generatedEditPoint(editPointCursor++);
        if (viewer->addPointToEditLayer(index, point))
        {
            appendLog(*log, QStringLiteral("addPointToEditLayer(%1); isLayerDirty=%2")
                .arg(index)
                .arg(viewer->isLayerDirty(index) ? QStringLiteral("true") : QStringLiteral("false")));
            refreshViewer(*viewer);
            window.statusBar()->showMessage(QStringLiteral("Feature added. isLayerDirty(index) is now true."));
            updateUi();
        }
    });

    QObject::connect(commitEditAction, &QAction::triggered, viewer, [&] {
        const int index = editLayerIndex();
        if (viewer->commitEditLayer(index))
        {
            appendLog(*log, QStringLiteral("commitEditLayer(%1); isLayerDirty=%2")
                .arg(index)
                .arg(viewer->isLayerDirty(index) ? QStringLiteral("true") : QStringLiteral("false")));
            refreshViewer(*viewer);
            window.statusBar()->showMessage(QStringLiteral("Edit committed. isLayerDirty(index) returned to false."));
            updateUi();
        }
    });

    QObject::connect(rollbackEditAction, &QAction::triggered, viewer, [&] {
        const int index = editLayerIndex();
        if (viewer->rollbackEditLayer(index))
        {
            appendLog(*log, QStringLiteral("rollbackEditLayer(%1); isLayerDirty=%2")
                .arg(index)
                .arg(viewer->isLayerDirty(index) ? QStringLiteral("true") : QStringLiteral("false")));
            refreshViewer(*viewer);
            window.statusBar()->showMessage(QStringLiteral("Edit rolled back. isLayerDirty(index) returned to false."));
            updateUi();
        }
    });

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);

    QObject::connect(viewer, &GisViewer::layerEditStateChanged, viewer, [&](GisLayer* layer) {
        if (layer != editLayerPtr)
            return;

        ++editStateSignalCount;
        const int index = editLayerIndex();
        appendLog(*log, QStringLiteral("signal layerEditStateChanged(%1); isLayerDirty=%2; editing=%3")
            .arg(layer != nullptr ? layer->name() : QStringLiteral("-"))
            .arg(viewer->isLayerDirty(index) ? QStringLiteral("true") : QStringLiteral("false"))
            .arg(viewer->isLayerEditing(index) ? QStringLiteral("true") : QStringLiteral("false")));
        updateUi();
    });

    updateUi();
    appendLog(*log, QStringLiteral("Ready. Initial isLayerDirty=%1")
        .arg(viewer->isLayerDirty(editLayerIndex()) ? QStringLiteral("true") : QStringLiteral("false")));

    window.show();
    viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));

    return app.exec();
}
