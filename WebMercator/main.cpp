#include "SampleSupport.h"

#include <QApplication>
#include <QColor>
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
constexpr double WebMercatorLimit = 20037508.342789244;

const GisExtent& webMercatorWorldExtent()
{
    static const GisExtent extent(
        -WebMercatorLimit,
        -WebMercatorLimit,
        WebMercatorLimit,
        WebMercatorLimit);
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

    layer->setName(QStringLiteral("World countries - source EPSG:4326"));
    layer->setCoordinateSystem(CoordinateSystemFactory::fromEpsg(4326));
    layer->style() = worldStyle();
    return true;
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("WebMercator"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico")));

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("WebMercator"));

    auto* central = new QWidget(&window);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    window.setCentralWidget(central);

    const auto webMercator = CoordinateSystemFactory::fromEpsg(3857);
    auto* details = new QLabel(
        QStringLiteral("EPSG:%1 - %2    |    Projected coordinates in meters    |    Meters per unit: %3")
            .arg(webMercator->epsgCode())
            .arg(webMercator->name())
            .arg(webMercator->metersPerUnit().value_or(0.0), 0, 'g', 12),
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
            status->setText(QStringLiteral("Screen: %1, %2    |    EPSG:3857 meters: %3, %4")
                .arg(screen.x(), 0, 'f', 0)
                .arg(screen.y(), 0, 'f', 0)
                .arg(world.x(), 0, 'f', 2)
                .arg(world.y(), 0, 'f', 2));
        });

    window.show();

    QMetaObject::invokeMethod(&window, [&window, viewer, status, webMercator]
    {
        const QString path = ensureWorldLayer(&window);
        if (path.isEmpty() || !loadWorldLayer(*viewer, path, &window))
        {
            status->setText(QStringLiteral("World sample data could not be loaded."));
            return;
        }

        // Set the viewer CRS after the source layer CRS is known so the
        // renderer builds the EPSG:4326 -> EPSG:3857 transform correctly.
        viewer->setCoordinateSystem(webMercator);
        viewer->setViewExtent(webMercatorWorldExtent());
        status->setText(QStringLiteral("Move the mouse over the map to inspect EPSG:3857 meter coordinates."));
    });

    return app.exec();
}
