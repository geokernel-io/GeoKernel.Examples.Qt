#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QStatusBar>
#include <QString>
#include <QToolBar>

#include <memory>

#include "Viewer/GisViewer.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShapePoint.h"
#include "Layers/GisLayerVector.h"

#include "Helpers.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;

constexpr int WorldLayerIndex = 0;

bool loadWorldLayer(GisViewer& viewer, const QString& path)
{
    QString errorMessage;
    if (viewer.addLayerFromPath(path, &errorMessage))
        return true;

    QMessageBox::critical(
        nullptr,
        QStringLiteral("EditSession"),
        QStringLiteral("Layer could not be loaded:\n%1").arg(errorMessage.isEmpty() ? path : errorMessage));
    return false;
}

std::unique_ptr<GisLayerVector> createEditLayer()
{
    auto layer = std::make_unique<GisLayerVector>();
    layer->setName(QStringLiteral("Editable Cities"));
    layer->addPoint(GisShapePoint(-122.4194, 37.7749));
    layer->addPoint(GisShapePoint(-118.2437, 34.0522));
    layer->addPoint(GisShapePoint(-112.0740, 33.4484));
    layer->open();
    return layer;
}

void refreshViewer(GisViewer& viewer)
{
    viewer.invalidateRenderCache(true, true);
    viewer.refreshLayers();
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
    const int column = index % 11;
    const int row = (index / 11) % 6;
    const int cycle = index / 66;

    return GisShapePoint(
        -124.0 + (column * 5.6) + (cycle * 0.35),
        25.0 + (row * 4.2) + (cycle * 0.35));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("EditSession"));
    window.statusBar()->showMessage(QStringLiteral("Ready."));

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
    beginEditAction->setEnabled(false);
    addFeatureAction->setEnabled(false);
    commitEditAction->setEnabled(false);
    rollbackEditAction->setEnabled(false);
    fullExtentAction->setEnabled(false);

    auto* stateLabel = new QLabel(&window);
    stateLabel->setContentsMargins(12, 0, 12, 0);
    toolbar->addWidget(stateLabel);

    std::unique_ptr<GisLayerVector> editLayer;
    GisLayerVector* editLayerPtr = nullptr;
    int editPointCursor = 0;

    auto updateUi = [&]() {
        const bool hasEditLayer = editLayerPtr != nullptr;
        const bool editing = hasEditLayer && editLayerPtr->isEditing();
        beginEditAction->setEnabled(hasEditLayer && !editing);
        addFeatureAction->setEnabled(editing);
        commitEditAction->setEnabled(editing);
        rollbackEditAction->setEnabled(editing);
        fullExtentAction->setEnabled(viewer->layerCount() > 0);
        stateLabel->setText(QStringLiteral("Editing: %1 | Feature count: %2")
            .arg(editing ? QStringLiteral("ON") : QStringLiteral("OFF"))
            .arg(hasEditLayer ? editLayerPtr->count() : 0));
    };

    QObject::connect(beginEditAction, &QAction::triggered, viewer, [&] {
        const int editLayerIndex = layerIndexOf(*viewer, editLayerPtr);
        if (viewer->beginEditLayer(editLayerIndex))
        {
            window.statusBar()->showMessage(QStringLiteral("Edit session started."));
            updateUi();
        }
    });

    QObject::connect(addFeatureAction, &QAction::triggered, viewer, [&] {
        if (editLayerPtr == nullptr || !editLayerPtr->isEditing())
            return;

        auto point = std::make_unique<GisShapePoint>(generatedEditPoint(editPointCursor));
        ++editPointCursor;

        if (editLayerPtr->addShapeEdit(std::move(point)))
        {
            refreshViewer(*viewer);
            window.statusBar()->showMessage(QStringLiteral("Feature added inside the active edit session."));
            updateUi();
        }
    });

    QObject::connect(commitEditAction, &QAction::triggered, viewer, [&] {
        const int editLayerIndex = layerIndexOf(*viewer, editLayerPtr);
        if (viewer->commitEditLayer(editLayerIndex))
        {
            refreshViewer(*viewer);
            window.statusBar()->showMessage(QStringLiteral("Edit session committed. Added features remain in the layer."));
            updateUi();
        }
    });

    QObject::connect(rollbackEditAction, &QAction::triggered, viewer, [&] {
        const int editLayerIndex = layerIndexOf(*viewer, editLayerPtr);
        if (viewer->rollbackEditLayer(editLayerIndex))
        {
            refreshViewer(*viewer);
            window.statusBar()->showMessage(QStringLiteral("Edit session rolled back. Uncommitted features were removed."));
            updateUi();
        }
    });

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);

    updateUi();
    window.show();

    QMetaObject::invokeMethod(&window, [&window, viewer, &editLayer, &editLayerPtr, &updateUi]
    {
        window.statusBar()->showMessage(QStringLiteral("Preparing world sample data..."));

        const QString worldPath = ensureSampleFile(
            QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/world_4326.zip")),
            QStringLiteral("world_4326.zip"),
            QStringLiteral("world_4326"),
            QStringLiteral("world_4326.shp"),
            &window);

        if (worldPath.isEmpty())
        {
            window.statusBar()->showMessage(QStringLiteral("World sample data could not be prepared."));
            updateUi();
            return;
        }

        if (!loadWorldLayer(*viewer, worldPath))
        {
            updateUi();
            return;
        }

        if (auto* worldLayer = viewer->mapLayerAt(WorldLayerIndex))
        {
            worldLayer->setName(QStringLiteral("World"));
            worldLayer->style().setFillColor(QStringLiteral("#D8E5E1"));
            worldLayer->style().setFillOpacity(210);
            worldLayer->style().setLineColor(QStringLiteral("#6F8883"));
            worldLayer->style().setLineWidth(0.7f);
        }

        editLayer = createEditLayer();
        editLayerPtr = editLayer.get();
        editLayerPtr->style().setPointColor(QStringLiteral("#D85B35"));
        editLayerPtr->style().setLineColor(QStringLiteral("#8C321D"));
        editLayerPtr->style().setPointSize(9.0f);
        editLayerPtr->style().setLineWidth(1.2f);
        viewer->addLayer(editLayer);
        viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));
        window.statusBar()->showMessage(QStringLiteral("Ready. Start an edit session to add temporary features."));
        updateUi();
    });

    return app.exec();
}
