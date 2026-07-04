#include "Helpers.h"
#include "Viewer/GisViewer.h"
#include "Layers/GisLayer.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;

bool loadFileLayer(GisViewer& viewer, const QString& path)
{
    const int layerIndex = viewer.layerCount();
    QString errorMessage;

    if (!viewer.addLayerFromPath(path, &errorMessage))
    {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("AddLayers"),
            QStringLiteral("Layer could not be loaded:\n%1")
            .arg(errorMessage.isEmpty() ? path : errorMessage));
        return false;
    }

    if (GisLayer* layer = viewer.mapLayerAt(layerIndex))
    {
        if (path.endsWith(QStringLiteral(".kml"), Qt::CaseInsensitive))
        {
            layer->style().setPointColor(QStringLiteral("#D95D39"));
            layer->style().setPointSize(8.0f);
            layer->style().setLineColor(QStringLiteral("#D95D39"));
            layer->style().setLineWidth(1.5f);
        }
        else if (path.endsWith(QStringLiteral(".shp"), Qt::CaseInsensitive))
        {
            layer->style().setFillColor(QStringLiteral("#D8E5E1"));
            layer->style().setFillOpacity(140);
            layer->style().setLineColor(QStringLiteral("#5F7874"));
            layer->style().setLineWidth(1.0f);
        }
    }

    return true;
}

bool loadSampleLayers(GisViewer& viewer)
{
    viewer.clearLayers();

    viewer.addOpenStreetMapLayer(true);

    if (!loadFileLayer(viewer, sampleDataPath(QStringLiteral("tiff/usa_3857.tif"))))
        return false;

    if (!loadFileLayer(viewer, sampleDataPath(QStringLiteral("shapefile/usa_states_3857.shp"))))
        return false;

    if (!loadFileLayer(viewer, sampleDataPath(QStringLiteral("kml/usa_cities_4326.kml"))))
        return false;

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

    if (!loadSampleLayers(*viewer))
        return 1;

    window.show();
    viewer->zoomToLayer(2);

    return app.exec();
}
