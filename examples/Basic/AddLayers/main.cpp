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

void applyCountryStyle(GisLayer& layer)
{
    layer.style().setFillColor(QStringLiteral("#35475B"));
    layer.style().setFillOpacity(172);
    layer.style().setLineColor(QStringLiteral("#B7E8FF"));
    layer.style().setLineWidth(0.85f);
    layer.style().setLabelColor(QStringLiteral("#FFFFFF"));
    layer.style().setLabelHaloColor(QStringLiteral("#10263A"));
}

void applyCityStyle(GisLayer& layer)
{
    layer.style().setPointColor(QStringLiteral("#1D8FC7"));
    layer.style().setLineColor(QStringLiteral("#74C3E8"));    
    layer.style().setLineWidth(0.9f);
    layer.style().setPointSize(4.2f);
}

bool loadSampleLayers(GisViewer& viewer, QWidget* parent)
{
    viewer.clearLayers();

    const QString rasterPath = ensureSampleFile(
        QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/world_8km_png.zip")),
        QStringLiteral("world_8km_png.zip"),
        QStringLiteral("world_8km_png"),
        QStringLiteral("world_8km.png"),
        parent);
    if (rasterPath.isEmpty() || loadNamedLayer(viewer, rasterPath, QStringLiteral("World raster"), parent) == nullptr)
        return false;

    const QString worldPath = ensureSampleFile(
        QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/world_4326.zip")),
        QStringLiteral("world_4326.zip"),
        QStringLiteral("world_4326"),
        QStringLiteral("world_4326.shp"),
        parent);
    if (worldPath.isEmpty())
        return false;

    GisLayer* countryLayer = loadNamedLayer(viewer, worldPath, QStringLiteral("Countries"), parent);
    if (countryLayer == nullptr)
        return false;

    applyCountryStyle(*countryLayer);

    const QString citiesPath = ensureSampleFile(
        QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/world_cities_4326.zip")),
        QStringLiteral("world_cities_4326.zip"),
        QStringLiteral("world_cities_4326"),
        QStringLiteral("world_cities_4326.shp"),
        parent);
    if (citiesPath.isEmpty())
        return false;

    GisLayer* cityLayer = loadNamedLayer(viewer, citiesPath, QStringLiteral("Cities"), parent);
    if (cityLayer == nullptr)
        return false;

    applyCityStyle(*cityLayer);

    viewer.refreshLayers();
    return true;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon(QStringLiteral("GeoKernelAppIcon.ico")));

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("AddLayers"));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    createNavigationToolbar(window, *viewer);

    if (!loadSampleLayers(*viewer, &window))
        return 1;

    window.show();
    viewer->fullExtent();

    return app.exec();
}
