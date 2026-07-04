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
#include "CoordinateSystems/Defs/ProjectedCoordinateSystem.h"
#include "CoordinateSystems/Transform/CoordinateTransformer.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::CoordinateSystems::Defs;
using namespace GeoKernel::Core::CoordinateSystems::Transform;

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

QString operationMethodText(CoordinateOperationMethod method)
{
    switch (method)
    {
        case CoordinateOperationMethod::WebMercator:
            return QStringLiteral("WebMercator");
        case CoordinateOperationMethod::TransverseMercator:
            return QStringLiteral("TransverseMercator");
        case CoordinateOperationMethod::Geographic:
            return QStringLiteral("Geographic");
        default:
            return QStringLiteral("Other");
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

bool loadWorldLayer(GisViewer& viewer, const ProjectedCoordinateSystem& webMercator)
{
    QString errorMessage;
    if (!viewer.addLayerFromPath(sampleDataPath(QStringLiteral("shapefile/world_3857.shp")), &errorMessage))
    {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("WebMercator"),
            QStringLiteral("World layer could not be loaded:\n%1").arg(errorMessage));
        return false;
    }

    if (auto* worldLayer = viewer.mapLayerAt(0))
    {
        worldLayer->setName(QStringLiteral("World source - EPSG:3857"));
        worldLayer->style() = worldStyle();
        worldLayer->setCoordinateSystem(std::make_shared<ProjectedCoordinateSystem>(webMercator));
    }

    return true;
}

std::unique_ptr<GisLayerVector> createCityLayer(const GeographicCoordinateSystem& wgs84, const ProjectedCoordinateSystem& webMercator)
{
    auto layer = GisLayerVector::createInMemory(
        QStringLiteral("City points - EPSG:3857"),
        GisShapeType::Point,
        GisExtent(-20037508.342789244, -20037508.342789244, 20037508.342789244, 20037508.342789244));

    layer->setCoordinateSystem(std::make_shared<ProjectedCoordinateSystem>(webMercator));
    layer->style() = cityStyle();
    layer->addAttributeDefinition({ QStringLiteral("name"), GisAttributeType::String, 64, 0 });
    layer->addAttributeDefinition({ QStringLiteral("lon"), GisAttributeType::Double, 18, 6 });
    layer->addAttributeDefinition({ QStringLiteral("lat"), GisAttributeType::Double, 18, 6 });
    layer->addAttributeDefinition({ QStringLiteral("x_3857"), GisAttributeType::Double, 18, 3 });
    layer->addAttributeDefinition({ QStringLiteral("y_3857"), GisAttributeType::Double, 18, 3 });

    const CoordinateTransformer transformer(wgs84, webMercator);
    const QVector<std::tuple<QString, double, double>> cities = {
        { QStringLiteral("Istanbul"), 28.9784, 41.0082 },
        { QStringLiteral("London"), -0.1276, 51.5072 },
        { QStringLiteral("New York"), -74.0060, 40.7128 },
        { QStringLiteral("Tokyo"), 139.6917, 35.6895 },
        { QStringLiteral("Sydney"), 151.2093, -33.8688 }
    };

    for (const auto& [name, lon, lat] : cities)
    {
        const GisShapePoint projected = transformer.transform(GisShapePoint(lon, lat));

        QHash<QString, QVariant> attributes;
        attributes.insert(QStringLiteral("name"), name);
        attributes.insert(QStringLiteral("lon"), lon);
        attributes.insert(QStringLiteral("lat"), lat);
        attributes.insert(QStringLiteral("x_3857"), projected.x());
        attributes.insert(QStringLiteral("y_3857"), projected.y());
        layer->addShapeEdit(std::make_unique<GisShapePoint>(projected), attributes);
    }

    return layer;
}

QString webMercatorDetails(const ProjectedCoordinateSystem& webMercator)
{
    QStringList lines;
    lines << QStringLiteral("KnownCoordinateSystems::webMercator()");
    lines << QString();
    lines << QStringLiteral("Coordinate system");
    lines << QStringLiteral("EPSG: %1").arg(webMercator.epsgCode());
    lines << QStringLiteral("Name: %1").arg(webMercator.name());
    lines << QStringLiteral("Type: %1").arg(webMercator.isProjected() ? QStringLiteral("Projected") : QStringLiteral("Other"));
    lines << QString();
    lines << QStringLiteral("Base geographic coordinate system");
    lines << QStringLiteral("EPSG: %1").arg(webMercator.baseGeographicCoordinateSystem().epsgCode());
    lines << QStringLiteral("Name: %1").arg(webMercator.baseGeographicCoordinateSystem().name());
    lines << QString();
    lines << QStringLiteral("Projection");
    lines << QStringLiteral("Name: %1").arg(webMercator.coordinateOperation().name());
    lines << QStringLiteral("Method: %1").arg(operationMethodText(webMercator.coordinateOperation().method()));
    lines << QString();
    lines << QStringLiteral("Linear unit");
    lines << QStringLiteral("Name: %1").arg(webMercator.linearUnit().name());
    lines << QStringLiteral("Meters per unit: %1").arg(webMercator.linearUnit().metersPerUnit(), 0, 'g', 12);
    lines << QString();
    lines << QStringLiteral("Axes");

    int axisIndex = 1;
    for (const CoordinateAxis& axis : webMercator.axes())
    {
        lines << QStringLiteral("%1. %2 (%3), direction: %4")
            .arg(axisIndex++)
            .arg(axis.name())
            .arg(axis.abbreviation())
            .arg(axisDirectionText(axis.direction()));
    }

    lines << QString();
    lines << QStringLiteral("Setup used in this sample");
    lines << QStringLiteral("auto webMercator = KnownCoordinateSystems::webMercator();");
    lines << QStringLiteral("viewer.setCoordinateSystem(std::make_shared<ProjectedCoordinateSystem>(webMercator));");
    lines << QStringLiteral("layer->setCoordinateSystem(std::make_shared<ProjectedCoordinateSystem>(webMercator));");
    lines << QString();
    lines << QStringLiteral("The world layer is read from world_3857.shp and assigned EPSG:3857.");
    lines << QStringLiteral("City points are transformed from longitude/latitude degrees to EPSG:3857 meters.");

    return lines.join(QStringLiteral("\n"));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon(QStringLiteral("GeoKernelAppIcon.ico")));

    const GeographicCoordinateSystem wgs84 = KnownCoordinateSystems::wgs84();
    const ProjectedCoordinateSystem webMercator = KnownCoordinateSystems::webMercator();

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("WebMercator"));

    auto* splitter = new QSplitter(&window);
    splitter->setChildrenCollapsible(false);
    window.setCentralWidget(splitter);

    auto* viewer = new GisViewer(splitter);
    viewer->setMouseTracking(true);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);
    viewer->setCoordinateSystem(std::make_shared<ProjectedCoordinateSystem>(webMercator));
    splitter->addWidget(viewer);

    auto* details = new QTextEdit(splitter);
    details->setReadOnly(true);
    details->setMinimumWidth(360);
    details->setPlainText(webMercatorDetails(webMercator));
    splitter->addWidget(details);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({ 840, 360 });

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));
    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(
            -20037508.342789244,
            -20037508.342789244,
            20037508.342789244,
            20037508.342789244));
    });

    auto* coordinateStatus = new QLabel(QStringLiteral("Screen: - | WebMercator: -"), &window);
    window.statusBar()->addPermanentWidget(coordinateStatus, 1);
    QObject::connect(viewer, &GisViewer::mouseCoordinatesChanged, coordinateStatus, [coordinateStatus](const QPointF& screen, const QPointF& world)
    {
        coordinateStatus->setText(QStringLiteral("Screen: %1, %2 | WebMercator meters: %3, %4")
            .arg(screen.x(), 0, 'f', 0)
            .arg(screen.y(), 0, 'f', 0)
            .arg(world.x(), 0, 'f', 2)
            .arg(world.y(), 0, 'f', 2));
    });

    if (!loadWorldLayer(*viewer, webMercator))
        return 1;

    auto cityLayer = createCityLayer(wgs84, webMercator);
    viewer->addLayer(cityLayer);

    window.show();
    viewer->setViewExtent(GisExtent(
        -20037508.342789244,
        -20037508.342789244,
        20037508.342789244,
        20037508.342789244));

    return app.exec();
}
