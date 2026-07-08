#include "Helpers.h"
#include "Viewer/GisViewer.h"
#include "Layers/GisLayer.h"
#include "Layers/GisLayerStyle.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShapePoint.h"
#include "CoordinateSystems/Defs/GeographicCoordinateSystem.h"
#include "CoordinateSystems/Defs/KnownCoordinateSystems.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Core::CoordinateSystems::Defs;

bool addLayerExtentRectangle(GisViewer& viewer, int layerIndex)
{
    const GisLayer* layer = viewer.mapLayerAt(layerIndex);
    if (layer == nullptr)
        return false;

    const GisExtent extent = layer->extent();
    if (extent.isEmpty())
        return false;

    QVector<GisShapePoint> rectangle
    {
        GisShapePoint(extent.xMin(), extent.yMin()),
        GisShapePoint(extent.xMax(), extent.yMin()),
        GisShapePoint(extent.xMax(), extent.yMax()),
        GisShapePoint(extent.xMin(), extent.yMax()),
        GisShapePoint(extent.xMin(), extent.yMin())
    };

    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#FFFFFF"));
    style.setFillOpacity(0);
    style.setLineColor(QStringLiteral("#E2453D"));
    style.setLineWidth(2.2f);

    return viewer.addPolygonLayer(
        QStringLiteral("Layer Extent"),
        QVector<QVector<GisShapePoint>> { rectangle },
        style,
        std::make_shared<GeographicCoordinateSystem>(KnownCoordinateSystems::wgs84())) >= 0;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("LayerExtent"));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    window.show();

    QMetaObject::invokeMethod(&window, [&window, viewer]
    {
        const QString californiaPath = ensureSampleFile(
            QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/california.zip")),
            QStringLiteral("california.zip"),
            QStringLiteral("california"),
            QStringLiteral("california.shp"),
            &window);
        if (californiaPath.isEmpty())
            return;

        if (!loadLayer(*viewer, californiaPath, &window))
            return;

        GisLayer* layer = viewer->mapLayerAt(0);
        if (layer == nullptr)
            return;

        layer->setName(QStringLiteral("California"));
        layer->style().setFillColor(QStringLiteral("#D8E5E1"));
        layer->style().setFillOpacity(210);
        layer->style().setLineColor(QStringLiteral("#6F8883"));
        layer->style().setLineWidth(0.9f);

        if (!addLayerExtentRectangle(*viewer, 0))
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("LayerExtent"),
                QStringLiteral("Layer extent rectangle could not be created."));
            return;
        }

        viewer->refreshLayers();
        viewer->fullExtent();
    });

    return app.exec();
}
