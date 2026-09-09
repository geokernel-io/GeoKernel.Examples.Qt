#include "SampleSupport.h"

#include <QApplication>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMetaObject>
#include <QPointF>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#include "CoordinateSystems/CoordinateSystemFactory.h"
#include "Layers/GisLayer.h"
#include "Layers/GisLayerStyle.h"
#include "Shapes/GisExtent.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::CoordinateSystems;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Viewer;

namespace
{
const GisExtent& wgs84WorldExtent()
{
    static const GisExtent extent(-180.0, -85.0, 180.0, 85.0);
    return extent;
}

GisLayerStyle worldStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#D8E5E1"));
    style.setFillOpacity(210);
    style.setLineColor(QStringLiteral("#6F8883"));
    style.setLineWidth(0.75f);
    return style;
}

bool loadWorldLayer(GisViewer& viewer, const QString& path, QWidget* parent)
{
    if (!loadLayer(viewer, path, parent))
        return false;

    GisLayer* layer = viewer.mapLayerAt(0);
    if (layer == nullptr)
        return false;

    layer->setName(QStringLiteral("World countries - EPSG:4326"));
    layer->setCoordinateSystem(CoordinateSystemFactory::fromEpsg(4326));
    layer->style() = worldStyle();
    return true;
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Wgs84Setup"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico")));

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("Wgs84Setup"));

    auto* central = new QWidget(&window);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    window.setCentralWidget(central);

    const auto wgs84 = CoordinateSystemFactory::fromEpsg(4326);
    auto* details = new QLabel(
        QStringLiteral("EPSG:%1 - %2    |    Geographic longitude/latitude coordinates in degrees")
            .arg(wgs84->epsgCode())
            .arg(wgs84->name()),
        central);
    details->setContentsMargins(8, 6, 8, 6);
    layout->addWidget(details);

    auto* viewer = new GisViewer(central);
    viewer->setMouseTracking(true);
    viewer->setActiveTool(GisViewerTool::Pan);
    layout->addWidget(viewer, 1);

    createNavigationToolbar(window, *viewer);

    auto* status = new QLabel(QStringLiteral("Preparing world sample data..."), &window);
    window.statusBar()->addPermanentWidget(status, 1);

    QObject::connect(
        viewer,
        &GisViewer::mouseCoordinatesChanged,
        status,
        [status](const QPointF& screen, const QPointF& world)
        {
            status->setText(QStringLiteral("Screen: %1, %2    |    EPSG:4326 lon/lat: %3, %4")
                .arg(screen.x(), 0, 'f', 0)
                .arg(screen.y(), 0, 'f', 0)
                .arg(world.x(), 0, 'f', 6)
                .arg(world.y(), 0, 'f', 6));
        });

    window.show();

    QMetaObject::invokeMethod(&window, [&window, viewer, status, wgs84]
    {
        const QString path = ensureWorldLayer(&window);
        if (path.isEmpty() || !loadWorldLayer(*viewer, path, &window))
        {
            status->setText(QStringLiteral("World sample data could not be loaded."));
            return;
        }

        viewer->setCoordinateSystem(wgs84);
        viewer->setViewExtent(wgs84WorldExtent());
        status->setText(QStringLiteral("Move the mouse over the map to inspect EPSG:4326 longitude and latitude."));
    });

    return app.exec();
}
