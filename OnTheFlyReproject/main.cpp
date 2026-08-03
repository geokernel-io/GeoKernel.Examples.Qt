#include "SampleSupport.h"

#include <QApplication>
#include <QComboBox>
#include <QIcon>
#include <QHash>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointF>
#include <QSize>
#include <QStatusBar>
#include <QToolBar>

#include <exception>
#include <memory>

#include "CoordinateSystems/CoordinateSystemFactory.h"
#include "Layers/GisLayer.h"
#include "Layers/GisLayerStyle.h"
#include "Shapes/GisExtent.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::CoordinateSystems;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;

namespace
{
constexpr double WebMercatorLimit = 20037508.342789244;

struct SpatialReferenceOption
{
    QString label;
    QString shortName;
    QString definition;
    GisExtent extent;
    int coordinateDecimals;
};

const QVector<SpatialReferenceOption>& spatialReferenceOptions()
{
    static const QVector<SpatialReferenceOption> options = {
        {
            QStringLiteral("EPSG:4326 - WGS 84"),
            QStringLiteral("EPSG:4326"),
            QStringLiteral("EPSG:4326"),
            GisExtent(-180.0, -85.0, 180.0, 85.0),
            6
        },
        {
            QStringLiteral("EPSG:3857 - WGS 84 / Web Mercator"),
            QStringLiteral("EPSG:3857"),
            QStringLiteral("EPSG:3857"),
            GisExtent(-WebMercatorLimit, -WebMercatorLimit, WebMercatorLimit, WebMercatorLimit),
            2
        },
        {
            QStringLiteral("EPSG:3395 - WGS 84 / World Mercator"),
            QStringLiteral("EPSG:3395"),
            QStringLiteral("EPSG:3395"),
            GisExtent(-WebMercatorLimit, -20000000.0, WebMercatorLimit, 20000000.0),
            2
        },
        {
            QStringLiteral("World Miller Cylindrical"),
            QStringLiteral("Miller"),
            QStringLiteral("ESRI:54003"),
            GisExtent(-WebMercatorLimit, -15500000.0, WebMercatorLimit, 15500000.0),
            2
        },
        {
            QStringLiteral("World Mollweide"),
            QStringLiteral("Mollweide"),
            QStringLiteral("ESRI:54009"),
            GisExtent(-18500000.0, -9500000.0, 18500000.0, 9500000.0),
            2
        },
        {
            QStringLiteral("World Sinusoidal"),
            QStringLiteral("Sinusoidal"),
            QStringLiteral("ESRI:54008"),
            GisExtent(-WebMercatorLimit, -10500000.0, WebMercatorLimit, 10500000.0),
            2
        },
        {
            QStringLiteral("World Eckert IV"),
            QStringLiteral("Eckert IV"),
            QStringLiteral("ESRI:54012"),
            GisExtent(-18500000.0, -9500000.0, 18500000.0, 9500000.0),
            2
        },
        {
            QStringLiteral("World Eckert VI"),
            QStringLiteral("Eckert VI"),
            QStringLiteral("ESRI:54010"),
            GisExtent(-18500000.0, -9500000.0, 18500000.0, 9500000.0),
            2
        }
    };
    return options;
}

std::shared_ptr<CoordinateSystem> coordinateSystemFor(const SpatialReferenceOption& option)
{
    static QHash<QString, std::shared_ptr<CoordinateSystem>> cache;
    const auto existing = cache.constFind(option.definition);
    if (existing != cache.cend())
        return existing.value();

    auto coordinateSystem = CoordinateSystemFactory::fromUserInput(option.definition);
    cache.insert(option.definition, coordinateSystem);
    return coordinateSystem;
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
    QApplication::setApplicationName(QStringLiteral("OnTheFlyReproject"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico")));

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("OnTheFlyReproject"));

    auto* viewer = new GisViewer(&window);
    viewer->setMouseTracking(true);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* toolbar = window.addToolBar(QStringLiteral("Projection"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));

    QAction* fullExtentAction = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/full-extent.svg")),
        QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Spatial reference:"), toolbar));

    auto* spatialReferenceCombo = new QComboBox(toolbar);
    spatialReferenceCombo->setMinimumWidth(330);
    for (const SpatialReferenceOption& option : spatialReferenceOptions())
        spatialReferenceCombo->addItem(option.label);
    toolbar->addWidget(spatialReferenceCombo);

    auto* hint = new QLabel(
        QStringLiteral("  world_4326.shp is reprojected on the fly into the selected viewer CRS."),
        toolbar);
    hint->setStyleSheet(QStringLiteral("color: #4E5F5B;"));
    toolbar->addWidget(hint);

    auto* status = new QLabel(QStringLiteral("Preparing world sample data..."), &window);
    window.statusBar()->addPermanentWidget(status, 1);

    bool worldLayerLoaded = false;

    const auto selectedOption = [spatialReferenceCombo]() -> const SpatialReferenceOption*
    {
        const int index = spatialReferenceCombo->currentIndex();
        const auto& options = spatialReferenceOptions();
        return index >= 0 && index < options.size() ? &options[index] : nullptr;
    };

    const auto applySelectedSpatialReference = [&window, viewer, status, selectedOption, &worldLayerLoaded]
    {
        if (!worldLayerLoaded)
            return;

        const SpatialReferenceOption* option = selectedOption();
        if (option == nullptr)
            return;

        try
        {
            viewer->setCoordinateSystem(coordinateSystemFor(*option));
            viewer->setViewExtent(option->extent);
            status->setText(QStringLiteral("%1: world_4326.shp reprojected on the fly.")
                .arg(option->shortName));
        }
        catch (const std::exception& ex)
        {
            status->setText(QStringLiteral("%1 could not be applied.").arg(option->shortName));
            QMessageBox::critical(
                &window,
                QStringLiteral("OnTheFlyReproject"),
                QString::fromUtf8(ex.what()));
        }
    };

    QObject::connect(
        spatialReferenceCombo,
        &QComboBox::currentIndexChanged,
        viewer,
        [&applySelectedSpatialReference](int) { applySelectedSpatialReference(); });

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer, selectedOption]
    {
        if (const SpatialReferenceOption* option = selectedOption())
            viewer->setViewExtent(option->extent);
    });

    QObject::connect(
        viewer,
        &GisViewer::mouseCoordinatesChanged,
        status,
        [status, selectedOption](const QPointF& screen, const QPointF& world)
        {
            const SpatialReferenceOption* option = selectedOption();
            if (option == nullptr)
                return;

            status->setText(QStringLiteral("Screen: %1, %2    |    %3: %4, %5")
                .arg(screen.x(), 0, 'f', 0)
                .arg(screen.y(), 0, 'f', 0)
                .arg(option->shortName)
                .arg(world.x(), 0, 'f', option->coordinateDecimals)
                .arg(world.y(), 0, 'f', option->coordinateDecimals));
        });

    window.show();

    QMetaObject::invokeMethod(&window, [&window, viewer, spatialReferenceCombo, status,
                                        &worldLayerLoaded, &applySelectedSpatialReference]
    {
        const QString path = ensureWorldLayer(&window);
        if (path.isEmpty() || !loadWorldLayer(*viewer, path, &window))
        {
            status->setText(QStringLiteral("World sample data could not be loaded."));
            return;
        }

        worldLayerLoaded = true;

        // Resolve the projection definitions once while the initial map is
        // being prepared so later combo-box changes only trigger rendering.
        for (const SpatialReferenceOption& option : spatialReferenceOptions())
            coordinateSystemFor(option);

        spatialReferenceCombo->setCurrentIndex(1);
        applySelectedSpatialReference();
    });

    return app.exec();
}
