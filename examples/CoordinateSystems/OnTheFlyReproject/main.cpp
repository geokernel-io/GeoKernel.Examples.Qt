#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QSize>
#include <QStatusBar>
#include <QString>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>
#include <memory>

#include "Viewer/GisViewer.h"
#include "Layers/GisLayer.h"
#include "Shapes/GisExtent.h"
#include "CoordinateSystems/Defs/CoordinateOperation.h"
#include "CoordinateSystems/Defs/CoordinateOperationMethod.h"
#include "CoordinateSystems/Defs/GeographicCoordinateSystem.h"
#include "CoordinateSystems/Defs/KnownCoordinateSystems.h"
#include "CoordinateSystems/Defs/LinearUnit.h"
#include "CoordinateSystems/Defs/ProjectedCoordinateSystem.h"
#include "CoordinateSystems/Defs/ProjectionParameter.h"

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Core::CoordinateSystems::Defs;

struct SpatialReferenceOption
{
    QString label;
    QString shortName;
    std::function<std::shared_ptr<CoordinateSystem>()> create;
    GisExtent extent;
    int coordinateDecimals = 2;
};

QVector<ProjectionParameter> centralMeridianParameters()
{
    return {
        ProjectionParameter(QStringLiteral("Longitude of natural origin"), 0.0),
        ProjectionParameter(QStringLiteral("False easting"), 0.0),
        ProjectionParameter(QStringLiteral("False northing"), 0.0)
    };
}

std::shared_ptr<CoordinateSystem> createWorldProjection(
    int epsg,
    const QString& name,
    const QString& operationName,
    CoordinateOperationMethod method,
    const QVector<ProjectionParameter>& parameters)
{
    return std::make_shared<ProjectedCoordinateSystem>(
        epsg,
        name,
        GeographicCoordinateSystem::wgs84(),
        CoordinateOperation(operationName, method, parameters),
        LinearUnit::meter());
}

std::shared_ptr<CoordinateSystem> createWorldMercator()
{
    return createWorldProjection(
        3395,
        QStringLiteral("WGS 84 / World Mercator"),
        QStringLiteral("Mercator"),
        CoordinateOperationMethod::Mercator1SP,
        {
            ProjectionParameter(QStringLiteral("Latitude of natural origin"), 0.0),
            ProjectionParameter(QStringLiteral("Longitude of natural origin"), 0.0),
            ProjectionParameter(QStringLiteral("Scale factor at natural origin"), 1.0),
            ProjectionParameter(QStringLiteral("False easting"), 0.0),
            ProjectionParameter(QStringLiteral("False northing"), 0.0)
        });
}

QString sampleDataPath(const QString& fileName)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/data/%1").arg(fileName)));
}

QVector<SpatialReferenceOption> spatialReferenceOptions()
{
    return {
        {
            QStringLiteral("EPSG:4326 - WGS 84"),
            QStringLiteral("EPSG:4326"),
            [] { return std::make_shared<GeographicCoordinateSystem>(KnownCoordinateSystems::wgs84()); },
            GisExtent(-180.0, -85.0, 180.0, 85.0),
            6
        },
        {
            QStringLiteral("EPSG:3857 - WGS 84 / Web Mercator"),
            QStringLiteral("EPSG:3857"),
            [] { return std::make_shared<ProjectedCoordinateSystem>(KnownCoordinateSystems::webMercator()); },
            GisExtent(-20037508.342789244, -20037508.342789244, 20037508.342789244, 20037508.342789244),
            2
        },
        {
            QStringLiteral("EPSG:3395 - WGS 84 / World Mercator"),
            QStringLiteral("EPSG:3395"),
            [] { return createWorldMercator(); },
            GisExtent(-20037508.342789244, -20000000.0, 20037508.342789244, 20000000.0),
            2
        },
        {
            QStringLiteral("World Miller Cylindrical"),
            QStringLiteral("Miller"),
            [] {
                return createWorldProjection(
                    0,
                    QStringLiteral("World Miller Cylindrical"),
                    QStringLiteral("Miller Cylindrical"),
                    CoordinateOperationMethod::MillerCylindrical,
                    centralMeridianParameters());
            },
            GisExtent(-20037508.342789244, -15500000.0, 20037508.342789244, 15500000.0),
            2
        },
        {
            QStringLiteral("World Mollweide"),
            QStringLiteral("Mollweide"),
            [] {
                return createWorldProjection(
                    0,
                    QStringLiteral("World Mollweide"),
                    QStringLiteral("Mollweide"),
                    CoordinateOperationMethod::Mollweide,
                    centralMeridianParameters());
            },
            GisExtent(-18500000.0, -9500000.0, 18500000.0, 9500000.0),
            2
        },
        {
            QStringLiteral("World Sinusoidal"),
            QStringLiteral("Sinusoidal"),
            [] {
                return createWorldProjection(
                    0,
                    QStringLiteral("World Sinusoidal"),
                    QStringLiteral("Sinusoidal"),
                    CoordinateOperationMethod::Sinusoidal,
                    centralMeridianParameters());
            },
            GisExtent(-20037508.342789244, -10500000.0, 20037508.342789244, 10500000.0),
            2
        },
        {
            QStringLiteral("World Eckert IV"),
            QStringLiteral("Eckert IV"),
            [] {
                return createWorldProjection(
                    0,
                    QStringLiteral("World Eckert IV"),
                    QStringLiteral("Eckert IV"),
                    CoordinateOperationMethod::EckertIV,
                    centralMeridianParameters());
            },
            GisExtent(-18500000.0, -9500000.0, 18500000.0, 9500000.0),
            2
        },
        {
            QStringLiteral("World Eckert VI"),
            QStringLiteral("Eckert VI"),
            [] {
                return createWorldProjection(
                    0,
                    QStringLiteral("World Eckert VI"),
                    QStringLiteral("Eckert VI"),
                    CoordinateOperationMethod::EckertVI,
                    centralMeridianParameters());
            },
            GisExtent(-18500000.0, -9500000.0, 18500000.0, 9500000.0),
            2
        }
    };
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
            QStringLiteral("OnTheFlyReproject"),
            QStringLiteral("World layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? QStringLiteral("shapefile/world_4326.shp") : errorMessage));
        return false;
    }

    if (GisLayer* layer = viewer.mapLayerAt(0))
    {
        layer->setName(QStringLiteral("World countries - source EPSG:4326"));
        layer->setCoordinateSystem(std::make_shared<GeographicCoordinateSystem>(KnownCoordinateSystems::wgs84()));
        layer->style() = worldStyle();
    }

    return true;
}

void applySpatialReference(GisViewer& viewer, const SpatialReferenceOption& option, QLabel& statusLabel)
{
    viewer.setCoordinateSystem(option.create());
    viewer.setViewExtent(option.extent);
    viewer.refreshLayers();
    statusLabel.setText(QStringLiteral("%1: -").arg(option.shortName));
}

void createToolbar(QMainWindow& window, GisViewer& viewer, QComboBox& crsCombo, const QVector<SpatialReferenceOption>& options)
{
    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QAction* zoomInAction = toolbar->addAction(sampleIcon(QStringLiteral("ZoomIn.svg")), QStringLiteral("Zoom In"));
    QAction* zoomOutAction = toolbar->addAction(sampleIcon(QStringLiteral("ZoomOut.svg")), QStringLiteral("Zoom Out"));
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));
    toolbar->addSeparator();

    auto* toolGroup = new QActionGroup(&window);
    toolGroup->setExclusive(true);

    QAction* zoomRectAction = toolbar->addAction(sampleIcon(QStringLiteral("RectangularZoom.svg")), QStringLiteral("Zoom Rect"));
    zoomRectAction->setCheckable(true);
    toolGroup->addAction(zoomRectAction);

    QAction* panAction = toolbar->addAction(sampleIcon(QStringLiteral("Pan.svg")), QStringLiteral("Pan"));
    panAction->setCheckable(true);
    toolGroup->addAction(panAction);

    QObject::connect(zoomInAction, &QAction::triggered, &viewer, [&viewer] { viewer.zoomIn(); });
    QObject::connect(zoomOutAction, &QAction::triggered, &viewer, [&viewer] { viewer.zoomOut(); });
    QObject::connect(fullExtentAction, &QAction::triggered, &viewer, [&viewer, &crsCombo, options]
    {
        const int index = crsCombo.currentData().toInt();
        if (index >= 0 && index < options.size())
            viewer.setViewExtent(options[index].extent);
    });
    QObject::connect(zoomRectAction, &QAction::triggered, &viewer, [&viewer] { viewer.setActiveTool(GisViewerTool::ZoomBox); });
    QObject::connect(panAction, &QAction::triggered, &viewer, [&viewer] { viewer.setActiveTool(GisViewerTool::Pan); });

    panAction->setChecked(true);
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    const QVector<SpatialReferenceOption> options = spatialReferenceOptions();

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("OnTheFlyReproject"));

    auto* centralWidget = new QWidget(&window);
    auto* layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    window.setCentralWidget(centralWidget);

    auto* topPanel = new QWidget(centralWidget);
    auto* topLayout = new QHBoxLayout(topPanel);
    topLayout->setContentsMargins(8, 6, 8, 6);
    topLayout->setSpacing(8);

    auto* crsLabel = new QLabel(QStringLiteral("Spatial reference:"), topPanel);
    auto* crsCombo = new QComboBox(topPanel);
    crsCombo->setMinimumWidth(360);
    for (int i = 0; i < options.size(); ++i)
        crsCombo->addItem(options[i].label, i);
    crsCombo->setCurrentIndex(1);

    auto* hintLabel = new QLabel(QStringLiteral("world_4326.shp is reprojected on the fly into the selected viewer CRS."), topPanel);
    hintLabel->setStyleSheet(QStringLiteral("color: #4E5F5B;"));

    topLayout->addWidget(crsLabel);
    topLayout->addWidget(crsCombo);
    topLayout->addWidget(hintLabel, 1);
    layout->addWidget(topPanel);

    auto* viewer = new GisViewer(centralWidget);
    viewer->setMouseTracking(true);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);
    layout->addWidget(viewer, 1);

    auto* coordinateStatus = new QLabel(QStringLiteral("EPSG:3857: -"), &window);
    window.statusBar()->addPermanentWidget(coordinateStatus, 1);

    createToolbar(window, *viewer, *crsCombo, options);

    if (!loadWorldLayer(*viewer))
        return 1;

    QObject::connect(crsCombo, &QComboBox::currentIndexChanged, viewer, [viewer, coordinateStatus, crsCombo, options]
    {
        const int index = crsCombo->currentData().toInt();
        if (index >= 0 && index < options.size())
            applySpatialReference(*viewer, options[index], *coordinateStatus);
    });

    QObject::connect(viewer, &GisViewer::mouseCoordinatesChanged, coordinateStatus, [coordinateStatus, crsCombo, options](const QPointF&, const QPointF& world)
    {
        const int index = crsCombo->currentData().toInt();
        if (index < 0 || index >= options.size())
            return;

        const SpatialReferenceOption& option = options[index];
        coordinateStatus->setText(QStringLiteral("%1: %2, %3")
            .arg(option.shortName)
            .arg(world.x(), 0, 'f', option.coordinateDecimals)
            .arg(world.y(), 0, 'f', option.coordinateDecimals));
    });

    window.show();
    applySpatialReference(*viewer, options[crsCombo->currentData().toInt()], *coordinateStatus);

    return app.exec();
}
