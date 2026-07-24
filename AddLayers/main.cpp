#include "SampleSupport.h"
#include "Layers/GisLayer.h"
#include "Viewer/GisViewer.h"

#include <QApplication>
#include <QIcon>

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;

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

    if (!loadLayer(viewer, rasterPath, parent))
        return false;

    GisLayer* rasterLayer = viewer.mapLayerAt(0);
    if (rasterLayer == nullptr)
    {
        QMessageBox::critical(parent, QStringLiteral("AddLayers"),
            QStringLiteral("The raster layer was loaded but could not be accessed."));
        return false;
    }
    rasterLayer->setName(QStringLiteral("World raster"));

    if (!loadLayer(viewer, worldPath, parent))
        return false;

    GisLayer* countryLayer = viewer.mapLayerAt(0);
    if (countryLayer == nullptr)
    {
        QMessageBox::critical(parent, QStringLiteral("AddLayers"),
            QStringLiteral("The country layer was loaded but could not be accessed."));
        return false;
    }
    countryLayer->setName(QStringLiteral("Countries"));
    countryLayer->style().setFillColor(QStringLiteral("#35475B"));
    countryLayer->style().setFillOpacity(172);
    countryLayer->style().setLineColor(QStringLiteral("#B7E8FF"));
    countryLayer->style().setLineWidth(0.85f);

    if (!loadLayer(viewer, citiesPath, parent))
        return false;

    GisLayer* cityLayer = viewer.mapLayerAt(0);
    if (cityLayer == nullptr)
    {
        QMessageBox::critical(parent, QStringLiteral("AddLayers"),
            QStringLiteral("The city layer was loaded but could not be accessed."));
        return false;
    }
    cityLayer->setName(QStringLiteral("Cities"));
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
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico")));

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("AddLayers"));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);

    window.setCentralWidget(viewer);
    createNavigationToolbar(window, *viewer);
    window.show();

    QMetaObject::invokeMethod(&window, [&window, viewer]() {
        if (loadSampleLayers(*viewer, &window))
            viewer->fullExtent();
    });

    return app.exec();
}
