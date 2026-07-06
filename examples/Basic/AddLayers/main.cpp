#include "Helpers.h"
#include "Layers/GisLayer.h"
#include "Symbology/GisDefaultSymbolStyles.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Symbology;

void applyAddLayersStyle(GisLayer* countryLayer, GisLayer* cityLayer)
{
    if (countryLayer != nullptr)
    {
        countryLayer->style().setFillColor(QStringLiteral("#667C92"));
        countryLayer->style().setFillOpacity(205);
        countryLayer->style().setLineColor(QStringLiteral("#E8EEF4"));
        countryLayer->style().setLineWidth(0.8f);
        countryLayer->style().setLabelColor(QStringLiteral("#FFFFFF"));
        countryLayer->style().setLabelHaloColor(QStringLiteral("#1C2B3A"));
    }

    if (cityLayer != nullptr)
    {
        cityLayer->style().setPointColor(QStringLiteral("#FDB52A"));
        cityLayer->style().setLineColor(QStringLiteral("#FFF4D0"));
        cityLayer->style().setLineWidth(1.2f);
        cityLayer->style().setPointSize(4.5f);
        cityLayer->style().setLabelColor(QStringLiteral("#FFFFFF"));
        cityLayer->style().setLabelHaloColor(QStringLiteral("#1C2B3A"));
    }
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
    if (rasterPath.isEmpty() || !loadLayer(viewer, rasterPath, parent))
        return false;

    const QString worldPath = ensureSampleFile(
        QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/world_4326.zip")),
        QStringLiteral("world_4326.zip"),
        QStringLiteral("world_4326"),
        QStringLiteral("world_4326.shp"),
        parent);
    if (worldPath.isEmpty() || !loadLayer(viewer, worldPath, parent))
        return false;
    GisLayer* countryLayer = viewer.mapLayerAt(0);

    const QString citiesPath = ensureSampleFile(
        QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/world_cities_4326.zip")),
        QStringLiteral("world_cities_4326.zip"),
        QStringLiteral("world_cities_4326"),
        QStringLiteral("world_cities_4326.shp"),
        parent);
    if (citiesPath.isEmpty() || !loadLayer(viewer, citiesPath, parent))
        return false;
    GisLayer* cityLayer = viewer.mapLayerAt(0);

    viewer.setMapStyle(GisSymbolTheme::SoftProfessional);
    applyAddLayersStyle(countryLayer, cityLayer);
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
