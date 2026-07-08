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
#include "CoordinateSystems/Defs/GeographicCoordinateSystem.h"
#include "CoordinateSystems/Defs/KnownCoordinateSystems.h"
#include "CoordinateSystems/Defs/ProjectedCoordinateSystem.h"
#include "CoordinateSystems/Transform/CoordinateTransformer.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShapePoint.h"
#include "Symbology/GisRuleBasedSymbolRenderer.h"

#include "Helpers.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::CoordinateSystems::Defs;
using namespace GeoKernel::Core::CoordinateSystems::Transform;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Core::Symbology;

constexpr float MinimumPointSize = 4.0f;
constexpr float MaximumPointSize = 22.0f;

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

    const double radius = std::clamp(static_cast<double>(style.pointSize()), static_cast<double>(MinimumPointSize), static_cast<double>(MaximumPointSize)) / 2.0;
    painter.drawEllipse(QPointF(36.0, 21.0), radius, radius);
    return QIcon(pixmap);
}

void updateLegend(QListWidget& legend, const GisLayerVector& layer)
{
    legend.clear();
    if (layer.symbolRenderer() == nullptr)
        return;

    for (const GisSymbolLegendItem& item : layer.symbolRenderer()->legendItems())
    {
        if (!item.enabled)
            continue;

        auto* row = new QListWidgetItem(
            legendIcon(item.style),
            item.label.isEmpty() ? QStringLiteral("(unnamed rule)") : item.label);
        row->setSizeHint(QSize(210, 44));
        legend.addItem(row);
    }
}

std::unique_ptr<GisRuleBasedSymbolRenderer> createPopClassRenderer(const GisLayerStyle& defaultStyle)
{
    auto renderer = std::make_unique<GisRuleBasedSymbolRenderer>();
    renderer->setDefaultStyle(defaultStyle);

    struct RuleDefinition
    {
        const char* label;
        const char* fillColor;
        const char* outlineColor;
        float pointSize;
    };

    const RuleDefinition rules[] = {
        { "Less than 50,000", "#7B8794", "#4B5563", MinimumPointSize },
        { "50,000 to 100,000", "#4FA3C4", "#1D6D83", 5.5f },
        { "100,000 to 250,000", "#55B889", "#2E7D59", 7.5f },
        { "250,000 to 500,000", "#F2B84B", "#9B6B18", 10.0f },
        { "500,000 to 1,000,000", "#E56B5D", "#9A3E32", 14.0f },
        { "1,000,000 to 5,000,000", "#A9423A", "#61201C", 19.0f }
    };

    for (const RuleDefinition& definition : rules)
    {
        QColor fill(QString::fromLatin1(definition.fillColor));
        fill.setAlpha(145);
        QColor outline(QString::fromLatin1(definition.outlineColor));
        outline.setAlpha(210);

        GisLayerStyle style;
        style.setPointColor(fill.name(QColor::HexArgb));
        style.setLineColor(outline.name(QColor::HexArgb));
        style.setPointSize(definition.pointSize);
        style.setLineWidth(std::clamp(definition.pointSize * 0.06f, 0.8f, 1.5f));

        GisSymbolRule rule;
        rule.fieldName = QStringLiteral("POP_CLASS");
        rule.op = GisSymbolRuleOperator::Equals;
        rule.value = QString::fromLatin1(definition.label);
        rule.label = QString::fromLatin1(definition.label);
        rule.style = style;
        renderer->addRule(rule);
    }

    return renderer;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("RuleBasedRenderer"));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* legendList = new QListWidget(&window);
    auto* legendDock = new QDockWidget(QStringLiteral("POP_CLASS rules"), &window);
    legendDock->setWidget(legendList);
    legendDock->setMinimumWidth(250);
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

        viewer->addOpenStreetMapLayer();

        QString errorMessage;
        if (!viewer->addLayerFromPath(path, &errorMessage))
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("RuleBasedRenderer"),
                QStringLiteral("Layer could not be loaded:\n%1")
                    .arg(errorMessage.isEmpty() ? path : errorMessage));
            legendList->clear();
            legendList->addItem(QStringLiteral("Layer could not be loaded."));
            window.statusBar()->showMessage(QStringLiteral("Layer could not be loaded."));
            return;
        }

        auto* citiesLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
        if (citiesLayer == nullptr)
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("RuleBasedRenderer"),
                QStringLiteral("Loaded layer is not a vector layer."));
            legendList->clear();
            legendList->addItem(QStringLiteral("Loaded layer is not a vector layer."));
            window.statusBar()->showMessage(QStringLiteral("Loaded layer is not a vector layer."));
            return;
        }

        GisLayerStyle cityStyle;
        QColor pointFill(QStringLiteral("#7B8794"));
        pointFill.setAlpha(145);
        QColor pointOutline(QStringLiteral("#4B5563"));
        pointOutline.setAlpha(210);
        cityStyle.setPointColor(pointFill.name(QColor::HexArgb));
        cityStyle.setLineColor(pointOutline.name(QColor::HexArgb));
        cityStyle.setPointSize(MinimumPointSize);
        cityStyle.setLineWidth(0.9f);

        citiesLayer->setName(QStringLiteral("Cities - rule based by POP_CLASS"));
        citiesLayer->style() = cityStyle;
        citiesLayer->setSymbolRenderer(createPopClassRenderer(cityStyle));

        const GisExtent viewExtent = projectedLayerExtent(*citiesLayer);
        updateLegend(*legendList, *citiesLayer);
        window.statusBar()->showMessage(QStringLiteral("Rule based renderer applied: POP_CLASS"));
        viewer->setViewExtent(viewExtent);
    });

    return app.exec();
}
