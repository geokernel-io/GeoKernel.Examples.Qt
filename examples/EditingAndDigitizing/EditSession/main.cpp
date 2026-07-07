#include <QAction>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
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

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;

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
        QStringLiteral("EditSession"),
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

GisLayerStyle editPointStyle()
{
    GisLayerStyle style;
    style.setPointColor(QStringLiteral("#D85B35"));
    style.setLineColor(QStringLiteral("#8C321D"));
    style.setPointSize(9.0f);
    style.setLineWidth(1.2f);
    return style;
}

std::unique_ptr<GisLayerVector> createEditLayer()
{
    auto layer = std::make_unique<GisLayerVector>();
    layer->setName(QStringLiteral("Editable Cities"));
    layer->style() = editPointStyle();
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

    auto updateUi = [&]() {
        const bool editing = editLayerPtr != nullptr && editLayerPtr->isEditing();
        beginEditAction->setEnabled(!editing);
        addFeatureAction->setEnabled(editing);
        commitEditAction->setEnabled(editing);
        rollbackEditAction->setEnabled(editing);
        stateLabel->setText(QStringLiteral("Editing: %1 | Feature count: %2")
            .arg(editing ? QStringLiteral("ON") : QStringLiteral("OFF"))
            .arg(editLayerPtr != nullptr ? editLayerPtr->count() : 0));
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

    viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));

    return app.exec();
}
