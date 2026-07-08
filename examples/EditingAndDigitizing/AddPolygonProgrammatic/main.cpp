#include <memory>

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
#include <QVector>

#include "Layers/GisLayerStyle.h"
#include "Layers/GisLayerVector.h"
#include "Shapes/GisExtent.h"
#include "Viewer/GisViewer.h"

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Core::Layers;
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
        QStringLiteral("AddPolygonProgrammatic"),
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

GisLayerStyle polygonStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#F2D27A"));
    style.setFillOpacity(160);
    style.setLineColor(QStringLiteral("#D95D39"));
    style.setLineWidth(2.0f);
    return style;
}

std::unique_ptr<GisLayerVector> createPolygonLayer()
{
    auto layer = GisLayerVector::createInMemory(
        QStringLiteral("Programmatic Polygons"),
        GisShapeType::Polygon,
        GisExtent(-180.0, -90.0, 180.0, 90.0));
    layer->style() = polygonStyle();
    return layer;
}

QVector<GisShapePoint> samplePolygonAt(int index)
{
    static constexpr double startX = -124.0;
    static constexpr double startY = 27.0;
    static constexpr double xStep = 7.5;
    static constexpr double yStep = 4.2;
    static constexpr int columns = 7;

    const int column = index % columns;
    const int row = index / columns;
    const double x = startX + column * xStep;
    const double y = startY + row * yStep;

    return {
        GisShapePoint(x, y),
        GisShapePoint(x + 4.4, y + 0.2),
        GisShapePoint(x + 5.6, y + 2.4),
        GisShapePoint(x + 2.3, y + 3.4),
        GisShapePoint(x - 0.4, y + 2.0),
        GisShapePoint(x, y)
    };
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

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("AddPolygonProgrammatic"));
    window.statusBar()->showMessage(QStringLiteral("Click Add Polygon to call addPolygonToEditLayer(index, points)."));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QAction* addPolygonAction = toolbar->addAction(sampleIcon(QStringLiteral("Polygon.svg")), QStringLiteral("Add Polygon"));
    QAction* clearAction = toolbar->addAction(sampleIcon(QStringLiteral("Delete.svg")), QStringLiteral("Clear Polygons"));
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

    auto polygonLayer = createPolygonLayer();
    auto* polygonLayerPtr = polygonLayer.get();
    viewer->addLayer(polygonLayer);

    int polygonCursor = 0;

    const auto updateCount = [&] {
        countLabel->setText(QStringLiteral("Polygon count: %1").arg(polygonLayerPtr != nullptr ? polygonLayerPtr->count() : 0));
    };

    const auto activatePolygonEditing = [&]() -> int {
        const int polygonLayerIndex = layerIndexOf(*viewer, polygonLayerPtr);
        if (polygonLayerIndex < 0)
        {
            window.statusBar()->showMessage(QStringLiteral("Programmatic Polygons layer is not in the viewer."));
            return -1;
        }

        if (!viewer->isLayerEditing(polygonLayerIndex) && !viewer->beginEditLayer(polygonLayerIndex))
        {
            window.statusBar()->showMessage(QStringLiteral("Programmatic Polygons layer could not enter edit mode."));
            return -1;
        }

        if (!viewer->setActiveEditLayerIndex(polygonLayerIndex))
        {
            window.statusBar()->showMessage(QStringLiteral("Programmatic Polygons layer could not be activated for editing."));
            return -1;
        }

        return polygonLayerIndex;
    };

    activatePolygonEditing();
    updateCount();

    QObject::connect(addPolygonAction, &QAction::triggered, viewer, [&] {
        const int polygonLayerIndex = activatePolygonEditing();
        if (polygonLayerIndex < 0)
            return;

        const QVector<GisShapePoint> points = samplePolygonAt(polygonCursor);
        if (!viewer->addPolygonToEditLayer(polygonLayerIndex, points))
        {
            window.statusBar()->showMessage(QStringLiteral("Polygon could not be added."));
            return;
        }

        ++polygonCursor;
        refreshViewer(*viewer);
        updateCount();
        window.statusBar()->showMessage(QStringLiteral("addPolygonToEditLayer(%1, %2 vertices)")
            .arg(polygonLayerIndex)
            .arg(points.size()));
    });

    QObject::connect(clearAction, &QAction::triggered, viewer, [&] {
        const int polygonLayerIndex = layerIndexOf(*viewer, polygonLayerPtr);
        viewer->rollbackEditLayer(polygonLayerIndex);
        activatePolygonEditing();
        polygonCursor = 0;
        refreshViewer(*viewer);
        updateCount();
        window.statusBar()->showMessage(QStringLiteral("Programmatic polygons cleared."));
    });

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);

    QObject::connect(viewer, &GisViewer::layerEditStateChanged, viewer, [&](GisLayer* layer) {
        if (layer != polygonLayerPtr)
            return;

        refreshViewer(*viewer);
        updateCount();
    });

    window.show();
    viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));

    return app.exec();
}
