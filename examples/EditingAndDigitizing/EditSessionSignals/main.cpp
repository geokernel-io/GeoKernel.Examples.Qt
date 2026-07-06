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
        QStringLiteral("EditSessionSignals"),
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
        QStringLiteral("Session Signal Points"),
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
    window.setWindowTitle(QStringLiteral("EditSessionSignals"));
    window.statusBar()->showMessage(QStringLiteral("Begin an edit session, add a feature, then commit or rollback to see session signals."));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(247, 248, 250));
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
    log->setMinimumHeight(130);
    auto* dock = new QDockWidget(QStringLiteral("Edit session signal log"), &window);
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
    int startedSignalCount = 0;
    int committedSignalCount = 0;
    int rolledBackSignalCount = 0;

    const auto editLayerIndex = [&]() {
        return layerIndexOf(*viewer, editLayerPtr);
    };

    const auto updateUi = [&]() {
        const int index = editLayerIndex();
        const bool hasLayer = index >= 0;
        const bool editing = hasLayer && viewer->isLayerEditing(index);

        beginEditAction->setEnabled(hasLayer && !editing);
        addFeatureAction->setEnabled(editing);
        commitEditAction->setEnabled(editing);
        rollbackEditAction->setEnabled(editing);
        stateLabel->setText(QStringLiteral("Editing: %1 | Started: %2 | Committed: %3 | Rolled back: %4 | Feature count: %5")
            .arg(editing ? QStringLiteral("ON") : QStringLiteral("OFF"))
            .arg(startedSignalCount)
            .arg(committedSignalCount)
            .arg(rolledBackSignalCount)
            .arg(editLayerPtr != nullptr ? editLayerPtr->count() : 0));
    };

    QObject::connect(beginEditAction, &QAction::triggered, viewer, [&] {
        const int index = editLayerIndex();
        if (viewer->beginEditLayer(index))
        {
            appendLog(*log, QStringLiteral("call beginEditLayer(%1)").arg(index));
            window.statusBar()->showMessage(QStringLiteral("Edit session started."));
            updateUi();
        }
    });

    QObject::connect(addFeatureAction, &QAction::triggered, viewer, [&] {
        const int index = editLayerIndex();
        const GisShapePoint point = generatedEditPoint(editPointCursor++);
        if (viewer->addPointToEditLayer(index, point))
        {
            appendLog(*log, QStringLiteral("call addPointToEditLayer(%1)").arg(index));
            refreshViewer(*viewer);
            window.statusBar()->showMessage(QStringLiteral("Feature added inside the active edit session."));
            updateUi();
        }
    });

    QObject::connect(commitEditAction, &QAction::triggered, viewer, [&] {
        const int index = editLayerIndex();
        if (viewer->commitEditLayer(index))
        {
            appendLog(*log, QStringLiteral("call commitEditLayer(%1)").arg(index));
            refreshViewer(*viewer);
            window.statusBar()->showMessage(QStringLiteral("Edit session committed."));
            updateUi();
        }
    });

    QObject::connect(rollbackEditAction, &QAction::triggered, viewer, [&] {
        const int index = editLayerIndex();
        if (viewer->rollbackEditLayer(index))
        {
            appendLog(*log, QStringLiteral("call rollbackEditLayer(%1)").arg(index));
            refreshViewer(*viewer);
            window.statusBar()->showMessage(QStringLiteral("Edit session rolled back."));
            updateUi();
        }
    });

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);

    QObject::connect(viewer, &GisViewer::layerEditSessionStarted, viewer, [&](GisLayer* layer) {
        if (layer != editLayerPtr)
            return;

        ++startedSignalCount;
        appendLog(*log, QStringLiteral("signal layerEditSessionStarted(%1)").arg(layer->name()));
        updateUi();
    });

    QObject::connect(viewer, &GisViewer::layerEditSessionCommitted, viewer, [&](GisLayer* layer) {
        if (layer != editLayerPtr)
            return;

        ++committedSignalCount;
        appendLog(*log, QStringLiteral("signal layerEditSessionCommitted(%1)").arg(layer->name()));
        updateUi();
    });

    QObject::connect(viewer, &GisViewer::layerEditSessionRolledBack, viewer, [&](GisLayer* layer) {
        if (layer != editLayerPtr)
            return;

        ++rolledBackSignalCount;
        appendLog(*log, QStringLiteral("signal layerEditSessionRolledBack(%1)").arg(layer->name()));
        updateUi();
    });

    updateUi();
    appendLog(*log, QStringLiteral("Ready. Waiting for edit session signals."));

    window.show();
    viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));

    return app.exec();
}
