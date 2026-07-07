#include <QAction>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QMainWindow>
#include <QMessageBox>
#include <QStatusBar>
#include <QString>
#include <QToolBar>

#include <memory>

#include "Viewer/GisViewer.h"
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
        QStringLiteral("InMemoryLayers"),
        QStringLiteral("Layer could not be loaded:\n%1").arg(errorMessage.isEmpty() ? path : errorMessage));
    return false;
}

GisShapePolyline routeShape(double offset)
{
    GisShapePolyline route;
    route.parts().append({
        GisShapePoint(-122.4194 + offset, 37.7749),
        GisShapePoint(-118.2437 + offset, 34.0522),
        GisShapePoint(-112.0740 + offset, 33.4484),
        GisShapePoint(-104.9903 + offset, 39.7392)
    });
    return route;
}

GisShapePolygon regionShape(double offset)
{
    GisShapePolygon region;
    region.parts().append({
        GisShapePoint(-101.0 + offset, 30.0),
        GisShapePoint(-91.0 + offset, 30.0),
        GisShapePoint(-89.0 + offset, 37.0),
        GisShapePoint(-96.0 + offset, 42.0),
        GisShapePoint(-103.0 + offset, 38.0),
        GisShapePoint(-101.0 + offset, 30.0)
    });
    return region;
}

void refreshMemoryLayers(GisViewer& viewer)
{
    viewer.invalidateRenderCache(false, true);
    viewer.refreshLayers();
}

GisShapePoint generatedPoint(int index)
{
    constexpr int columns = 12;
    constexpr double startX = -124.0;
    constexpr double startY = 25.0;
    constexpr double stepX = 4.8;
    constexpr double stepY = 3.2;

    const int column = index % columns;
    const int row = index / columns;
    const double jitterX = (row % 3) * 0.35;
    const double jitterY = (column % 4) * 0.25;
    return GisShapePoint(startX + column * stepX + jitterX, startY + row * stepY + jitterY);
}

void updateStatus(QStatusBar& statusBar, const GisLayerVector* cityLayer, const GisLayerVector* routeLayer, const GisLayerVector* regionLayer)
{
    statusBar.showMessage(QStringLiteral("Memory features - points: %1 | lines: %2 | polygons: %3")
        .arg(cityLayer != nullptr ? cityLayer->count() : 0)
        .arg(routeLayer != nullptr ? routeLayer->count() : 0)
        .arg(regionLayer != nullptr ? regionLayer->count() : 0));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("InMemoryLayers"));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(247, 248, 250));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);
    window.statusBar()->showMessage(QStringLiteral("Ready"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    QAction* addPointAction = toolbar->addAction(QStringLiteral("Add Point"));
    QAction* addLineAction = toolbar->addAction(QStringLiteral("Add Line"));
    QAction* addPolygonAction = toolbar->addAction(QStringLiteral("Add Polygon"));
    toolbar->addSeparator();
    QAction* clearMemoryAction = toolbar->addAction(QStringLiteral("Clear Memory Layers"));
    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));

    if (!loadLayer(*viewer, sampleDataPath(QStringLiteral("shapefile/world_4326.shp"))))
        return 1;

    if (auto* worldLayer = viewer->mapLayerAt(0))
    {
        worldLayer->setName(QStringLiteral("World"));
        worldLayer->style().setFillColor(QStringLiteral("#D8E5E1"));
        worldLayer->style().setFillOpacity(210);
        worldLayer->style().setLineColor(QStringLiteral("#6F8883"));
        worldLayer->style().setLineWidth(0.7f);
    }

    GisLayerStyle pointStyle;
    pointStyle.setPointColor(QStringLiteral("#D95F35"));
    pointStyle.setPointSize(7.0f);

    GisLayerStyle lineStyle;
    lineStyle.setLineColor(QStringLiteral("#266D8F"));
    lineStyle.setLineWidth(2.2f);

    GisLayerStyle polygonStyle;
    polygonStyle.setFillColor(QStringLiteral("#F1D58A"));
    polygonStyle.setFillOpacity(150);
    polygonStyle.setLineColor(QStringLiteral("#9A7A1F"));
    polygonStyle.setLineWidth(1.5f);

    GisLayerVector* regionLayerPtr = nullptr;
    GisLayerVector* routeLayerPtr = nullptr;
    GisLayerVector* cityLayerPtr = nullptr;

    auto addMemoryLayers = [&]
    {
        auto regionLayer = std::make_unique<GisLayerVector>();
        regionLayer->setName(QStringLiteral("Memory Regions"));
        regionLayer->style() = polygonStyle;
        regionLayer->addPolygon(regionShape(0.0));
        regionLayer->open();
        regionLayerPtr = regionLayer.get();
        viewer->addLayer(regionLayer);

        auto routeLayer = std::make_unique<GisLayerVector>();
        routeLayer->setName(QStringLiteral("Memory Routes"));
        routeLayer->style() = lineStyle;
        routeLayer->addPolyline(routeShape(0.0));
        routeLayer->open();
        routeLayerPtr = routeLayer.get();
        viewer->addLayer(routeLayer);

        auto cityLayer = std::make_unique<GisLayerVector>();
        cityLayer->setName(QStringLiteral("Memory Cities"));
        cityLayer->style() = pointStyle;
        cityLayer->addPoint(GisShapePoint(-122.4194, 37.7749));
        cityLayer->addPoint(GisShapePoint(-118.2437, 34.0522));
        cityLayer->open();
        cityLayerPtr = cityLayer.get();
        viewer->addLayer(cityLayer);
    };

    addMemoryLayers();

    auto clearLayerShapes = [](GisLayerVector* layer)
    {
        if (layer == nullptr)
            return;

        if (!layer->isEditing())
            layer->beginEdit();

        while (layer->count() > 0)
        {
            const QVector<GisShape*> shapes = layer->items();
            if (shapes.isEmpty() || shapes.first() == nullptr)
                break;

            if (!layer->deleteShapeEdit(shapes.first()->id()))
                break;
        }

        if (layer->isEditing())
            layer->commitEdit();

        layer->clearEditHistory();
    };

    int pointCursor = 0;
    int lineCursor = 1;
    int polygonCursor = 1;

    QObject::connect(addPointAction, &QAction::triggered, viewer, [&]
    {
        if (cityLayerPtr != nullptr)
        {
            cityLayerPtr->addPoint(generatedPoint(pointCursor));
            ++pointCursor;
            refreshMemoryLayers(*viewer);
            updateStatus(*window.statusBar(), cityLayerPtr, routeLayerPtr, regionLayerPtr);
        }
    });

    QObject::connect(addLineAction, &QAction::triggered, viewer, [&]
    {
        if (routeLayerPtr != nullptr)
        {
            routeLayerPtr->addPolyline(routeShape(lineCursor * 2.0));
            ++lineCursor;
            refreshMemoryLayers(*viewer);
            updateStatus(*window.statusBar(), cityLayerPtr, routeLayerPtr, regionLayerPtr);
        }
    });

    QObject::connect(addPolygonAction, &QAction::triggered, viewer, [&]
    {
        if (regionLayerPtr != nullptr)
        {
            regionLayerPtr->addPolygon(regionShape(polygonCursor * 5.0));
            ++polygonCursor;
            refreshMemoryLayers(*viewer);
            updateStatus(*window.statusBar(), cityLayerPtr, routeLayerPtr, regionLayerPtr);
        }
    });

    QObject::connect(clearMemoryAction, &QAction::triggered, viewer, [&]
    {
        clearLayerShapes(cityLayerPtr);
        clearLayerShapes(routeLayerPtr);
        clearLayerShapes(regionLayerPtr);

        regionLayerPtr->addPolygon(regionShape(0.0));
        routeLayerPtr->addPolyline(routeShape(0.0));
        cityLayerPtr->addPoint(GisShapePoint(-122.4194, 37.7749));
        cityLayerPtr->addPoint(GisShapePoint(-118.2437, 34.0522));

        pointCursor = 0;
        lineCursor = 1;
        polygonCursor = 1;
        refreshMemoryLayers(*viewer);
        updateStatus(*window.statusBar(), cityLayerPtr, routeLayerPtr, regionLayerPtr);
    });

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);

    window.show();
    updateStatus(*window.statusBar(), cityLayerPtr, routeLayerPtr, regionLayerPtr);
    viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));

    return app.exec();
}
