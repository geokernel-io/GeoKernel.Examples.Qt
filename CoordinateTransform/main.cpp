#include <QApplication>
#include "SampleSupport.h"
#include <QColor>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QPointF>
#include <QSize>
#include <QStatusBar>
#include <QString>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <exception>
#include <memory>

#include "Viewer/GisViewer.h"
#include "Layers/GisLayer.h"
#include "Layers/GisLayerStyle.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShapePoint.h"
#include "CoordinateSystems/CoordinateSystemFactory.h"
#include "CoordinateSystems/CoordinateTransformer.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::CoordinateSystems;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;

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

    if (GisLayer* layer = viewer.mapLayerAt(0))
    {
        layer->setName(QStringLiteral("World countries"));
        layer->setCoordinateSystem(CoordinateSystemFactory::fromEpsg(4326));
        layer->style() = worldStyle();
    }

    return true;
}

QString coordinateText(const GisShapePoint& lonLat, const GisShapePoint& webMercator)
{
    return QStringLiteral("EPSG:4326 lon/lat: %1, %2    ->    EPSG:3857 meters: %3, %4")
        .arg(lonLat.x(), 0, 'f', 6)
        .arg(lonLat.y(), 0, 'f', 6)
        .arg(webMercator.x(), 0, 'f', 2)
        .arg(webMercator.y(), 0, 'f', 2);
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("CoordinateTransform"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico")));

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("CoordinateTransform"));

    auto* centralWidget = new QWidget(&window);
    auto* layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    window.setCentralWidget(centralWidget);

    auto* header = new QWidget(centralWidget);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 6, 8, 6);
    auto* descriptionLabel = new QLabel(
        QStringLiteral("Move the mouse over the map to transform EPSG:4326 longitude/latitude to EPSG:3857 Web Mercator meters."),
        header);
    headerLayout->addWidget(descriptionLabel);
    headerLayout->addStretch(1);
    layout->addWidget(header);

    auto* viewer = new GisViewer(centralWidget);
    viewer->setMouseTracking(true);
    viewer->setActiveTool(GisViewerTool::Pan);
    viewer->setCoordinateSystem(CoordinateSystemFactory::fromEpsg(4326));
    layout->addWidget(viewer, 1);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);
    QAction* fullExtentAction = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/full-extent.svg")),
        QStringLiteral("Full Extent"));

    auto* coordinateStatus = new QLabel(QStringLiteral("Move mouse over the map."), &window);
    window.statusBar()->addPermanentWidget(coordinateStatus, 1);

    const QString worldLayerPath = ensureWorldLayer(&window);
    if (worldLayerPath.isEmpty() || !loadWorldLayer(*viewer, worldLayerPath, &window))
        return 1;

    const GisExtent worldExtent(-180.0, -85.0, 180.0, 85.0);
    viewer->setViewExtent(worldExtent);

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer, worldExtent]
    {
        viewer->setViewExtent(worldExtent);
    });

    QObject::connect(viewer, &GisViewer::mouseCoordinatesChanged, coordinateStatus, [coordinateStatus](const QPointF&, const QPointF& world)
    {
        const double longitude = world.x();
        const double latitude = world.y();
        if (!std::isfinite(longitude) || !std::isfinite(latitude)
            || longitude < -180.0 || longitude > 180.0
            || latitude <= -90.0 || latitude >= 90.0)
        {
            coordinateStatus->setText(QStringLiteral("Move mouse over the map."));
            return;
        }

        try
        {
            static const auto wgs84 = CoordinateSystemFactory::fromEpsg(4326);
            static const auto webMercator = CoordinateSystemFactory::fromEpsg(3857);
            static const CoordinateTransformer wgs84ToWebMercator(*wgs84, *webMercator);

            const GisShapePoint lonLat(longitude, latitude);
            const GisShapePoint webMercatorPoint = wgs84ToWebMercator.transform(lonLat);
            coordinateStatus->setText(coordinateText(lonLat, webMercatorPoint));
        }
        catch (const std::exception&)
        {
            coordinateStatus->setText(QStringLiteral("Coordinate is outside the transformable range."));
        }
    });

    window.show();
    return app.exec();
}
