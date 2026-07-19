#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointF>
#include <QSize>
#include <QStatusBar>
#include <QString>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

#include "Viewer/GisViewer.h"
#include "Layers/GisLayer.h"
#include "Layers/GisLayerStyle.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShapePoint.h"
#include "CoordinateSystems/CoordinateSystemFactory.h"
#include "CoordinateSystems/CoordinateTransformer.h"

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::CoordinateSystems;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;

QString sampleDataPath(const QString& fileName)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/data/%1").arg(fileName)));
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

bool loadWorldLayer(GisViewer& viewer)
{
    QString errorMessage;
    if (!viewer.addLayerFromPath(sampleDataPath(QStringLiteral("shapefile/world_4326.shp")), &errorMessage))
    {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("CoordinateTransform"),
            QStringLiteral("World layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? QStringLiteral("shapefile/world_4326.shp") : errorMessage));
        return false;
    }

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
    app.setWindowIcon(sampleIcon());

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
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));

    auto* coordinateStatus = new QLabel(QStringLiteral("Move mouse over the map."), &window);
    window.statusBar()->addPermanentWidget(coordinateStatus, 1);

    if (!loadWorldLayer(*viewer))
        return 1;

    const GisExtent worldExtent(-180.0, -85.0, 180.0, 85.0);
    viewer->setViewExtent(worldExtent);

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer, worldExtent]
    {
        viewer->setViewExtent(worldExtent);
    });

    QObject::connect(viewer, &GisViewer::mouseCoordinatesChanged, coordinateStatus, [coordinateStatus](const QPointF&, const QPointF& world)
    {
        const GisShapePoint lonLat(world.x(), world.y());
        const auto wgs84 = CoordinateSystemFactory::fromEpsg(4326);
        const auto webMercator = CoordinateSystemFactory::fromEpsg(3857);
        const CoordinateTransformer wgs84ToWebMercator(*wgs84, *webMercator);
        const GisShapePoint webMercatorPoint = wgs84ToWebMercator.transform(lonLat);
        coordinateStatus->setText(coordinateText(lonLat, webMercatorPoint));
    });

    window.show();
    return app.exec();
}
