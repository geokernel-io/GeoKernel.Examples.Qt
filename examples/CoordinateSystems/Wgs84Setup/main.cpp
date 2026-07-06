#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QSize>
#include <QSplitter>
#include <QStatusBar>
#include <QString>
#include <QTextEdit>
#include <QToolBar>

#include <memory>

#include "Viewer/GisViewer.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShapePoint.h"
#include "Layers/GisLayerStyle.h"
#include "Layers/GisLayerVector.h"
#include "CoordinateSystems/Defs/CoordinateAxis.h"
#include "CoordinateSystems/Defs/GeographicCoordinateSystem.h"
#include "CoordinateSystems/Defs/KnownCoordinateSystems.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::CoordinateSystems::Defs;

QIcon sampleIcon(const QString& fileName)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString path = QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/images/%1").arg(fileName)));
    QIcon icon;

    for (const auto mode : { QIcon::Normal, QIcon::Active, QIcon::Selected, QIcon::Disabled })
    {
        icon.addFile(path, QSize(), mode, QIcon::Off);
        icon.addFile(path, QSize(), mode, QIcon::On);
    }

    return icon;
}

QString sampleDataPath(const QString& relativePath)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/data/%1").arg(relativePath)));
}

QString axisDirectionText(CoordinateAxisDirection direction)
{
    switch (direction)
    {
        case CoordinateAxisDirection::North:
            return QStringLiteral("north");
        case CoordinateAxisDirection::South:
            return QStringLiteral("south");
        case CoordinateAxisDirection::East:
            return QStringLiteral("east");
        case CoordinateAxisDirection::West:
            return QStringLiteral("west");
        case CoordinateAxisDirection::Up:
            return QStringLiteral("up");
        case CoordinateAxisDirection::Down:
            return QStringLiteral("down");
        case CoordinateAxisDirection::Other:
            return QStringLiteral("other");
        default:
            return QStringLiteral("unknown");
    }
}

GisLayerStyle worldStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#D8E5E1"));
    style.setFillOpacity(210);
    style.setLineColor(QStringLiteral("#6F8883"));
    style.setLineWidth(0.7f);
    return style;
}

GisLayerStyle cityStyle()
{
    GisLayerStyle style;
    style.setPointColor(QStringLiteral("#D95D39"));
    style.setLineColor(QStringLiteral("#8C321D"));
    style.setPointSize(10.0f);
    style.setLineWidth(1.2f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("name"));
    style.setLabelFontSize(10.0f);
    style.setLabelColor(QStringLiteral("#263238"));
    style.setLabelHaloEnabled(true);
    style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
    style.setLabelHaloWidth(2.0f);
    style.setLabelOffsetY(-12.0f);
    style.setLabelAllowOverlap(true);
    return style;
}

bool loadWorldLayer(GisViewer& viewer)
{
    QString errorMessage;
    if (!viewer.addLayerFromPath(sampleDataPath(QStringLiteral("shapefile/world_4326.shp")), &errorMessage))
    {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("Wgs84Setup"),
            QStringLiteral("World layer could not be loaded:\n%1").arg(errorMessage));
        return false;
    }

    if (auto* worldLayer = viewer.mapLayerAt(0))
    {
        worldLayer->setName(QStringLiteral("World - EPSG:4326"));
        worldLayer->style() = worldStyle();
        worldLayer->setCoordinateSystem(std::make_shared<GeographicCoordinateSystem>(KnownCoordinateSystems::wgs84()));
    }

    return true;
}

std::unique_ptr<GisLayerVector> createCityLayer(const GeographicCoordinateSystem& wgs84)
{
    auto layer = GisLayerVector::createInMemory(
        QStringLiteral("WGS84 city points"),
        GisShapeType::Point,
        GisExtent(-180.0, -90.0, 180.0, 90.0));

    layer->setCoordinateSystem(std::make_shared<GeographicCoordinateSystem>(wgs84));
    layer->style() = cityStyle();
    layer->addAttributeDefinition({ QStringLiteral("name"), GisAttributeType::String, 64, 0 });
    layer->addAttributeDefinition({ QStringLiteral("lon"), GisAttributeType::Double, 18, 6 });
    layer->addAttributeDefinition({ QStringLiteral("lat"), GisAttributeType::Double, 18, 6 });

    const QVector<std::tuple<QString, double, double>> cities = {
        { QStringLiteral("Istanbul"), 28.9784, 41.0082 },
        { QStringLiteral("London"), -0.1276, 51.5072 },
        { QStringLiteral("New York"), -74.0060, 40.7128 },
        { QStringLiteral("Tokyo"), 139.6917, 35.6895 },
        { QStringLiteral("Sydney"), 151.2093, -33.8688 }
    };

    for (const auto& [name, lon, lat] : cities)
    {
        QHash<QString, QVariant> attributes;
        attributes.insert(QStringLiteral("name"), name);
        attributes.insert(QStringLiteral("lon"), lon);
        attributes.insert(QStringLiteral("lat"), lat);
        layer->addShapeEdit(std::make_unique<GisShapePoint>(lon, lat), attributes);
    }

    return layer;
}

QString wgs84Details(const GeographicCoordinateSystem& wgs84)
{
    QStringList lines;
    lines << QStringLiteral("KnownCoordinateSystems::wgs84()");
    lines << QString();
    lines << QStringLiteral("Coordinate system");
    lines << QStringLiteral("EPSG: %1").arg(wgs84.epsgCode());
    lines << QStringLiteral("Name: %1").arg(wgs84.name());
    lines << QStringLiteral("Type: %1").arg(wgs84.isGeographic() ? QStringLiteral("Geographic") : QStringLiteral("Other"));
    lines << QString();
    lines << QStringLiteral("Datum");
    lines << QStringLiteral("EPSG: %1").arg(wgs84.datum().epsgCode());
    lines << QStringLiteral("Name: %1").arg(wgs84.datum().name());
    lines << QStringLiteral("Ellipsoid: %1").arg(wgs84.datum().ellipsoid().name());
    lines << QString();
    lines << QStringLiteral("Prime meridian");
    lines << QStringLiteral("Name: %1").arg(wgs84.primeMeridian().name());
    lines << QStringLiteral("Longitude: %1 %2")
        .arg(wgs84.primeMeridian().longitude(), 0, 'f', 6)
        .arg(wgs84.primeMeridian().angularUnit().name());
    lines << QString();
    lines << QStringLiteral("Angular unit");
    lines << QStringLiteral("Name: %1").arg(wgs84.angularUnit().name());
    lines << QStringLiteral("Radians per unit: %1").arg(wgs84.angularUnit().radiansPerUnit(), 0, 'g', 12);
    lines << QString();
    lines << QStringLiteral("Axes");

    int axisIndex = 1;
    for (const CoordinateAxis& axis : wgs84.axes())
    {
        lines << QStringLiteral("%1. %2 (%3), direction: %4")
            .arg(axisIndex++)
            .arg(axis.name())
            .arg(axis.abbreviation())
            .arg(axisDirectionText(axis.direction()));
    }

    lines << QString();
    lines << QStringLiteral("Setup used in this sample");
    lines << QStringLiteral("auto wgs84 = KnownCoordinateSystems::wgs84();");
    lines << QStringLiteral("viewer.setCoordinateSystem(std::make_shared<GeographicCoordinateSystem>(wgs84));");
    lines << QStringLiteral("layer->setCoordinateSystem(std::make_shared<GeographicCoordinateSystem>(wgs84));");
    lines << QString();
    lines << QStringLiteral("Point coordinates are longitude/latitude degrees.");

    return lines.join(QStringLiteral("\n"));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon(QStringLiteral("GeoKernelAppIcon.ico")));

    const GeographicCoordinateSystem wgs84 = KnownCoordinateSystems::wgs84();

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("Wgs84Setup"));

    auto* splitter = new QSplitter(&window);
    splitter->setChildrenCollapsible(false);
    window.setCentralWidget(splitter);

    auto* viewer = new GisViewer(splitter);
    viewer->setMouseTracking(true);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);
    viewer->setCoordinateSystem(std::make_shared<GeographicCoordinateSystem>(wgs84));
    splitter->addWidget(viewer);

    auto* details = new QTextEdit(splitter);
    details->setReadOnly(true);
    details->setMinimumWidth(340);
    details->setPlainText(wgs84Details(wgs84));
    splitter->addWidget(details);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({ 860, 340 });

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));
    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-180.0, -85.0, 180.0, 85.0));
    });

    auto* coordinateStatus = new QLabel(QStringLiteral("Screen: - | World: -"), &window);
    window.statusBar()->addPermanentWidget(coordinateStatus, 1);
    QObject::connect(viewer, &GisViewer::mouseCoordinatesChanged, coordinateStatus, [coordinateStatus](const QPointF& screen, const QPointF& world)
    {
        coordinateStatus->setText(QStringLiteral("Screen: %1, %2 | World: %3, %4")
            .arg(screen.x(), 0, 'f', 0)
            .arg(screen.y(), 0, 'f', 0)
            .arg(world.x(), 0, 'f', 6)
            .arg(world.y(), 0, 'f', 6));
    });

    if (!loadWorldLayer(*viewer))
        return 1;

    auto cityLayer = createCityLayer(wgs84);
    viewer->addLayer(cityLayer);

    window.show();
    viewer->setViewExtent(GisExtent(-180.0, -85.0, 180.0, 85.0));

    return app.exec();
}
