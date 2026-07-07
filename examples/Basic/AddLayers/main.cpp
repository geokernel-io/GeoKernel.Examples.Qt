#include "Helpers.h"
#include "Layers/GisLayer.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;

GisLayer* loadNamedLayer(GisViewer& viewer, const QString& path, const QString& name, QWidget* parent)
{
    if (!loadLayer(viewer, path, parent))
        return nullptr;

    GisLayer* layer = viewer.mapLayerAt(0);
    if (layer == nullptr)
    {
        QMessageBox::critical(
            parent,
            QStringLiteral("AddLayers"),
            QStringLiteral("Layer was loaded but could not be resolved:\n%1").arg(path));
        return nullptr;
    }

    layer->setName(name);
    return layer;
}

bool loadSampleLayers(GisViewer& viewer, QWidget* parent)
{
    const QString rasterPath = ensureSampleFile(
        QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/world_8km_png.zip")),
        QStringLiteral("world_8km_png.zip"),
        QStringLiteral("world_8km_png"),
        QStringLiteral("world_8km.png"),
        parent);
    if (rasterPath.isEmpty())
        return false;

    const QString worldPath = ensureSampleFile(
        QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/world_4326.zip")),
        QStringLiteral("world_4326.zip"),
        QStringLiteral("world_4326"),
        QStringLiteral("world_4326.shp"),
        parent);
    if (worldPath.isEmpty())
        return false;

    const QString citiesPath = ensureSampleFile(
        QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/world_cities_4326.zip")),
        QStringLiteral("world_cities_4326.zip"),
        QStringLiteral("world_cities_4326"),
        QStringLiteral("world_cities_4326.shp"),
        parent);
    if (citiesPath.isEmpty())
        return false;

    viewer.clearLayers();

    if (loadNamedLayer(viewer, rasterPath, QStringLiteral("World raster"), parent) == nullptr)
        return false;

    GisLayer* countryLayer = loadNamedLayer(viewer, worldPath, QStringLiteral("Countries"), parent);
    if (countryLayer == nullptr)
        return false;

    countryLayer->style().setFillColor(QStringLiteral("#35475B"));
    countryLayer->style().setFillOpacity(172);
    countryLayer->style().setLineColor(QStringLiteral("#B7E8FF"));
    countryLayer->style().setLineWidth(0.85f);
    countryLayer->style().setLabelColor(QStringLiteral("#FFFFFF"));
    countryLayer->style().setLabelHaloColor(QStringLiteral("#10263A"));

    GisLayer* cityLayer = loadNamedLayer(viewer, citiesPath, QStringLiteral("Cities"), parent);
    if (cityLayer == nullptr)
        return false;

    cityLayer->style().setPointColor(QStringLiteral("#1D8FC7"));
    cityLayer->style().setLineColor(QStringLiteral("#74C3E8"));
    cityLayer->style().setLineWidth(0.9f);
    cityLayer->style().setPointSize(4.2f);

    viewer.refreshLayers();
    return true;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("AddLayers"));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    createNavigationToolbar(window, *viewer);

    window.show();
    
    QMetaObject::invokeMethod(&window, [&window, viewer]
    {
        if (loadSampleLayers(*viewer, &window))
            viewer->fullExtent();
    });

    return app.exec();
}