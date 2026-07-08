#include <QAction>
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

#include "Viewer/GisViewer.h"
#include "Shapes/GisExtent.h"
#include "Layers/GisLayerStyle.h"
#include "Layers/GisLayerVector.h"

#include <memory>

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
        QStringLiteral("AddPointProgrammatic"),
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

std::unique_ptr<GisLayerVector> createPointLayer()
{
    auto layer = GisLayerVector::createInMemory(
        QStringLiteral("Programmatic Points"),
        GisShapeType::Point,
        GisExtent(-180.0, -90.0, 180.0, 90.0));
    layer->style() = pointStyle();
    return layer;
}

GisShapePoint samplePointAt(int index)
{
    static constexpr double xMin = -124.0;
    static constexpr double yMin = 26.0;
    static constexpr double xStep = 1.9;
    static constexpr double yStep = 1.8;
    static constexpr int columns = 29;
    static constexpr int rows = 13;

    const int cell = index % (columns * rows);
    const int column = (cell * 7) % columns;
    const int row = ((cell / columns) + (cell * 11)) % rows;
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

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("AddPointProgrammatic"));
    window.statusBar()->showMessage(QStringLiteral("Click Add Point to call addPointToEditLayer(index, worldPoint)."));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QAction* addPointAction = toolbar->addAction(sampleIcon(QStringLiteral("Point.svg")), QStringLiteral("Add Point"));
    QAction* clearAction = toolbar->addAction(sampleIcon(QStringLiteral("Delete.svg")), QStringLiteral("Clear Points"));
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));

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

    int pointCursor = 0;

    const auto updateCount = [&] {
        countLabel->setText(QStringLiteral("Point count: %1").arg(pointLayerPtr != nullptr ? pointLayerPtr->count() : 0));
    };

    const auto activatePointEditing = [&]() -> int {
        const int pointLayerIndex = layerIndexOf(*viewer, pointLayerPtr);
        if (pointLayerIndex < 0)
        {
            window.statusBar()->showMessage(QStringLiteral("Programmatic Points layer is not in the viewer."));
            return -1;
        }

        if (!viewer->isLayerEditing(pointLayerIndex) && !viewer->beginEditLayer(pointLayerIndex))
        {
            window.statusBar()->showMessage(QStringLiteral("Programmatic Points layer could not enter edit mode."));
            return -1;
        }

        if (!viewer->setActiveEditLayerIndex(pointLayerIndex))
        {
            window.statusBar()->showMessage(QStringLiteral("Programmatic Points layer could not be activated for editing."));
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

        const GisShapePoint worldPoint = samplePointAt(pointCursor);
        if (!viewer->addPointToEditLayer(pointLayerIndex, worldPoint))
        {
            window.statusBar()->showMessage(QStringLiteral("Point could not be added."));
            return;
        }

        ++pointCursor;
        refreshViewer(*viewer);
        updateCount();
        window.statusBar()->showMessage(QStringLiteral("addPointToEditLayer(%1, [%2, %3])")
            .arg(pointLayerIndex)
            .arg(worldPoint.x(), 0, 'f', 4)
            .arg(worldPoint.y(), 0, 'f', 4));
    });

    QObject::connect(clearAction, &QAction::triggered, viewer, [&] {
        const int pointLayerIndex = layerIndexOf(*viewer, pointLayerPtr);
        viewer->rollbackEditLayer(pointLayerIndex);
        activatePointEditing();
        pointCursor = 0;
        refreshViewer(*viewer);
        updateCount();
        window.statusBar()->showMessage(QStringLiteral("Programmatic points cleared."));
    });

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);

    QObject::connect(viewer, &GisViewer::layerEditStateChanged, viewer, [&](GisLayer* layer) {
        if (layer != pointLayerPtr)
            return;

        refreshViewer(*viewer);
        updateCount();
    });

    window.show();
    viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));
    
    return app.exec();
}
