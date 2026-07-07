#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QSize>
#include <QStatusBar>
#include <QString>
#include <QToolBar>

#include <memory>

#include "Viewer/GisViewer.h"
#include "Shapes/GisExtent.h"
#include "Layers/GisLayerStyle.h"
#include "Layers/GisLayerVector.h"

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
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
        QStringLiteral("AddPointInteractive"),
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
    style.setPointSize(9.0f);
    style.setLineWidth(1.2f);
    return style;
}

std::unique_ptr<GisLayerVector> createPointLayer()
{
    auto layer = GisLayerVector::createInMemory(
        QStringLiteral("Clicked Points"),
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

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("AddPointInteractive"));
    window.statusBar()->showMessage(QStringLiteral("Choose Add Point, then click on the map."));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));
    toolbar->addSeparator();

    QActionGroup toolGroup(&window);
    toolGroup.setExclusive(true);

    QAction* addPointAction = toolbar->addAction(sampleIcon(QStringLiteral("Point.svg")), QStringLiteral("Add Point"));
    addPointAction->setCheckable(true);
    toolGroup.addAction(addPointAction);

    QAction* panAction = toolbar->addAction(sampleIcon(QStringLiteral("Pan.svg")), QStringLiteral("Pan"));
    panAction->setCheckable(true);
    toolGroup.addAction(panAction);

    toolbar->addSeparator();
    QAction* clearAction = toolbar->addAction(sampleIcon(QStringLiteral("Delete.svg")), QStringLiteral("Clear Points"));

    auto* countLabel = new QLabel(&window);
    countLabel->setContentsMargins(12, 0, 12, 0);
    toolbar->addWidget(countLabel);

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

    const auto updateCount = [&] {
        countLabel->setText(QStringLiteral("Point count: %1").arg(pointLayerPtr != nullptr ? pointLayerPtr->count() : 0));
    };

    const auto activatePointEditing = [&]() -> bool {
        const int pointLayerIndex = layerIndexOf(*viewer, pointLayerPtr);
        if (pointLayerIndex < 0)
        {
            window.statusBar()->showMessage(QStringLiteral("Clicked Points layer is not in the viewer."));
            return false;
        }

        if (!viewer->isLayerEditing(pointLayerIndex) && !viewer->beginEditLayer(pointLayerIndex))
        {
            window.statusBar()->showMessage(QStringLiteral("Clicked Points layer could not enter edit mode."));
            return false;
        }

        if (!viewer->setActiveEditLayerIndex(pointLayerIndex))
        {
            window.statusBar()->showMessage(QStringLiteral("Clicked Points layer could not be activated for editing."));
            return false;
        }

        return true;
    };

    activatePointEditing();

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);

    QObject::connect(addPointAction, &QAction::triggered, viewer, [&] {
        if (!activatePointEditing())
            return;

        viewer->setActiveTool(GisViewerTool::AddPoint);
        window.statusBar()->showMessage(QStringLiteral("Add Point tool active. Click the map to add points."));
    });

    QObject::connect(panAction, &QAction::triggered, viewer, [&] {
        viewer->setActiveTool(GisViewerTool::Pan);
        window.statusBar()->showMessage(QStringLiteral("Pan tool active."));
    });

    QObject::connect(clearAction, &QAction::triggered, viewer, [&] {
        const int pointLayerIndex = layerIndexOf(*viewer, pointLayerPtr);
        viewer->rollbackEditLayer(pointLayerIndex);
        activatePointEditing();
        refreshViewer(*viewer);
        updateCount();
        window.statusBar()->showMessage(QStringLiteral("Clicked points cleared."));
    });

    QObject::connect(viewer, &GisViewer::layerEditStateChanged, viewer, [&](GisLayer* layer) {
        if (layer != pointLayerPtr)
            return;

        refreshViewer(*viewer);
        updateCount();
    });

    QObject::connect(viewer, &GisViewer::mouseCoordinatesChanged, viewer, [&](const QPointF&, const QPointF& worldPoint) {
        if (viewer->activeTool() != GisViewerTool::AddPoint)
            return;

        window.statusBar()->showMessage(QStringLiteral("Add Point active. x=%1, y=%2")
            .arg(worldPoint.x(), 0, 'f', 4)
            .arg(worldPoint.y(), 0, 'f', 4));
    });

    QObject::connect(viewer, &GisViewer::mapClicked, viewer, [&](GisViewerTool tool, const QPointF&, const GisShapePoint& point, Qt::KeyboardModifiers) {
        if (tool != GisViewerTool::AddPoint)
            return;

        window.statusBar()->showMessage(QStringLiteral("Point click at x=%1, y=%2")
            .arg(point.x(), 0, 'f', 4)
            .arg(point.y(), 0, 'f', 4));
    });

    addPointAction->setChecked(true);
    viewer->setActiveTool(GisViewerTool::AddPoint);
    updateCount();

    window.show();
    viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));

    return app.exec();
}
