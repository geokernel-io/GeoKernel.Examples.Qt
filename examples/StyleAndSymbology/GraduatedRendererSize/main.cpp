#include <QApplication>
#include <QColor>
#include <QDockWidget>
#include <QIcon>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QSize>
#include <QStatusBar>
#include <QString>

#include <algorithm>
#include <memory>

#include "Viewer/GisViewer.h"
#include "Layers/GisLayerVector.h"
#include "CoordinateSystems/Defs/ProjectedCoordinateSystem.h"
#include "CoordinateSystems/Transform/CoordinateTransformer.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShapePoint.h"
#include "Symbology/GisColorRampRegistry.h"
#include "Symbology/GisSymbolRendererFactory.h"
#include "FeatureSources/GdalShapefileFeatureSource.h"
#include "CoordinateSystems/Defs/GeographicCoordinateSystem.h"
#include "CoordinateSystems/Defs/KnownCoordinateSystems.h"

#include "Helpers.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Viewer::FeatureSources;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Core::Symbology;
using namespace GeoKernel::Core::CoordinateSystems::Defs;
using namespace GeoKernel::Core::CoordinateSystems::Transform;

constexpr const char* SizeField = "POP_CLASS_SIZE";
constexpr double MinimumPointSize = 3.0;
constexpr double MaximumPointSize = 36.0;

double populationClassSize(const QString& popClass)
{
    const QString value = popClass.trimmed();
    if (value.compare(QStringLiteral("Less than 50,000"), Qt::CaseInsensitive) == 0)
        return 1.0;
    if (value.compare(QStringLiteral("50,000 to 100,000"), Qt::CaseInsensitive) == 0)
        return 2.0;
    if (value.compare(QStringLiteral("100,000 to 250,000"), Qt::CaseInsensitive) == 0)
        return 3.0;
    if (value.compare(QStringLiteral("250,000 to 500,000"), Qt::CaseInsensitive) == 0)
        return 4.0;
    if (value.compare(QStringLiteral("500,000 to 1,000,000"), Qt::CaseInsensitive) == 0)
        return 5.0;
    if (value.compare(QStringLiteral("1,000,000 to 5,000,000"), Qt::CaseInsensitive) == 0)
        return 6.0;

    return 0.0;
}

void makePointRangesReadable(GisGraduatedSymbolRenderer& renderer, const GisLayerStyle& defaultStyle)
{
    QVector<GisGraduatedSymbolRange> ranges = renderer.ranges();
    for (GisGraduatedSymbolRange& range : ranges)
    {
        QColor pointFill(range.style.pointColor());
        if (!pointFill.isValid())
            pointFill = QColor(QStringLiteral("#D95F35"));
        pointFill.setAlpha(72);

        QColor pointOutline(range.style.lineColor());
        if (!pointOutline.isValid())
            pointOutline = QColor(QStringLiteral("#8A3A24"));
        pointOutline.setAlpha(175);

        range.style.setPointColor(pointFill.name(QColor::HexArgb));
        range.style.setLineColor(pointOutline.name(QColor::HexArgb));
        range.style.setLineWidth(static_cast<float>(std::clamp(range.style.pointSize() * 0.07f, 0.9f, 2.2f)));
    }

    renderer.setDefaultStyle(defaultStyle);
    renderer.setRanges(std::move(ranges));
}

GisShapePoint toWebMercator(const GisShapePoint& lonLat)
{
    const GeographicCoordinateSystem wgs84 = KnownCoordinateSystems::wgs84();
    const ProjectedCoordinateSystem webMercator = KnownCoordinateSystems::webMercator();
    return CoordinateTransformer(wgs84, webMercator).transform(lonLat);
}

GisExtent projectedLayerExtent(const GisLayerVector& layer)
{
    const GisExtent lonLatExtent = layer.extent();
    if (lonLatExtent.isEmpty())
        return GisExtent(-20037508.342789244, -20037508.342789244, 20037508.342789244, 20037508.342789244);

    const GisShapePoint minPoint = toWebMercator(GisShapePoint(lonLatExtent.xMin(), lonLatExtent.yMin()));
    const GisShapePoint maxPoint = toWebMercator(GisShapePoint(lonLatExtent.xMax(), lonLatExtent.yMax()));
    const GisExtent projectedExtent(minPoint.x(), minPoint.y(), maxPoint.x(), maxPoint.y());
    const double paddingX = std::max(500000.0, projectedExtent.width() * 0.12);
    const double paddingY = std::max(500000.0, projectedExtent.height() * 0.12);
    return projectedExtent.inflate(paddingX, paddingY);
}

QIcon legendIcon(const GisLayerStyle& style)
{
    QPixmap pixmap(72, 42);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(style.lineColor()), std::max(1.0f, style.lineWidth())));
    painter.setBrush(QColor(style.pointColor()));

    const double radius = std::clamp(static_cast<double>(style.pointSize()), MinimumPointSize, MaximumPointSize) / 2.0;
    painter.drawEllipse(QPointF(36.0, 21.0), radius, radius);
    return QIcon(pixmap);
}

void updateLegend(QListWidget& legend, const GisLayerVector& layer)
{
    legend.clear();
    if (layer.symbolRenderer() == nullptr)
        return;

    const QStringList labels = {
        QStringLiteral("Less than 50,000"),
        QStringLiteral("50,000 to 100,000"),
        QStringLiteral("100,000 to 250,000"),
        QStringLiteral("250,000 to 500,000"),
        QStringLiteral("500,000 to 1,000,000"),
        QStringLiteral("1,000,000 to 5,000,000")
    };

    const QVector<GisSymbolLegendItem> items = layer.symbolRenderer()->legendItems();
    for (int i = 0; i < items.size(); ++i)
    {
        const GisSymbolLegendItem& item = items[i];
        if (!item.enabled)
            continue;

        auto* row = new QListWidgetItem(
            legendIcon(item.style),
            i < labels.size() ? labels[i] : item.label);
        row->setSizeHint(QSize(210, 44));
        legend.addItem(row);
    }
}

std::unique_ptr<GisLayerVector> loadCitiesAsMemoryLayer(const QString& path, const GisLayerStyle& defaultStyle, QString* errorMessage)
{
    auto source = std::make_unique<GdalShapefileFeatureSource>(path);
    if (!source->open())
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("Could not open cities shapefile.");
        return nullptr;
    }

    auto layer = std::make_unique<GisLayerVector>();
    layer->setName(QStringLiteral("Cities - graduated size by POP_CLASS"));
    layer->setCoordinateSystem(std::make_shared<GeographicCoordinateSystem>(KnownCoordinateSystems::wgs84()));
    layer->style() = defaultStyle;

    int added = 0;
    for (int row = 0; row < source->featureCount(); ++row)
    {
        GisFeatureGeometry geometry;
        if (!source->readGeometry(row, geometry) || geometry.points.isEmpty())
            continue;

        QHash<QString, QVariant> attributes = source->attributes(row);
        attributes.insert(SizeField, populationClassSize(attributes.value(QStringLiteral("POP_CLASS")).toString()));

        for (const GisShapePoint& point : geometry.points)
        {
            auto shape = std::make_unique<GisShapePoint>(point.x(), point.y());
            shape->attributes() = attributes;
            if (layer->addShape(std::move(shape)))
                ++added;
        }
    }

    if (added == 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("No city points could be loaded.");
        return nullptr;
    }

    layer->open();
    return layer;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("GraduatedRendererSize"));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* legendList = new QListWidget(&window);
    auto* legendDock = new QDockWidget(QStringLiteral("POP_CLASS size classes"), &window);
    legendDock->setWidget(legendList);
    legendDock->setMinimumWidth(245);
    window.addDockWidget(Qt::LeftDockWidgetArea, legendDock);

    window.show();

    QMetaObject::invokeMethod(&window, [&window, viewer, legendList]
    {
        legendList->clear();
        legendList->addItem(QStringLiteral("Preparing USA cities sample data..."));

        const QString path = ensureSampleFile(
            QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/usa_cities.zip")),
            QStringLiteral("usa_cities.zip"),
            QStringLiteral("usa_cities"),
            QStringLiteral("usa_cities.shp"),
            &window);
        if (path.isEmpty())
        {
            legendList->clear();
            legendList->addItem(QStringLiteral("Sample data could not be prepared."));
            window.statusBar()->showMessage(QStringLiteral("Sample data could not be prepared."));
            return;
        }

        GisLayerStyle cityStyle;
        QColor pointFill(QStringLiteral("#D95F35"));
        pointFill.setAlpha(72);
        QColor pointOutline(QStringLiteral("#8A3A24"));
        pointOutline.setAlpha(175);
        cityStyle.setPointColor(pointFill.name(QColor::HexArgb));
        cityStyle.setLineColor(pointOutline.name(QColor::HexArgb));
        cityStyle.setPointSize(static_cast<float>(MinimumPointSize));
        cityStyle.setLineWidth(0.9f);

        viewer->addOpenStreetMapLayer();

        QString errorMessage;
        auto cityLayer = loadCitiesAsMemoryLayer(path, cityStyle, &errorMessage);
        if (!cityLayer)
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("GraduatedRendererSize"),
                errorMessage.isEmpty() ? QStringLiteral("usa_cities.shp could not be loaded.") : errorMessage);
            legendList->clear();
            legendList->addItem(QStringLiteral("Cities layer could not be loaded."));
            window.statusBar()->showMessage(QStringLiteral("Cities layer could not be loaded."));
            return;
        }

        auto renderer = GisSymbolRendererFactory::createGraduatedRenderer(
            *cityLayer,
            QString::fromLatin1(SizeField),
            GisClassificationMethod::EqualInterval,
            6,
            GisColorRampRegistry::ramp(QStringLiteral("Plasma")),
            cityStyle,
            0.0,
            {},
            GisColorRampMode::Discrete,
            false,
            GisSymbolRendererFactory::DefaultProviderBackedSampleRowLimit,
            GisSymbolStyleTarget::SizeOrWidth,
            MinimumPointSize,
            MaximumPointSize);

        if (!renderer)
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("GraduatedRendererSize"),
                QStringLiteral("Could not create graduated size renderer from POP_CLASS."));
            legendList->clear();
            legendList->addItem(QStringLiteral("Graduated size renderer could not be created."));
            window.statusBar()->showMessage(QStringLiteral("Graduated size renderer could not be created."));
            return;
        }

        makePointRangesReadable(*renderer, cityStyle);
        cityLayer->setSymbolRenderer(std::move(renderer));
        const GisExtent viewExtent = projectedLayerExtent(*cityLayer);
        updateLegend(*legendList, *cityLayer);
        viewer->addLayer(cityLayer);
        window.statusBar()->showMessage(QStringLiteral("Graduated size renderer applied: POP_CLASS"));
        viewer->setViewExtent(viewExtent);
    });

    return app.exec();
}
