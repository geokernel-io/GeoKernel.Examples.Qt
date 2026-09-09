#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSet>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <optional>

#include "SampleSupport.h"
#include "CoordinateSystems/CoordinateSystemFactory.h"
#include "CoordinateSystems/CoordinateTransformer.h"
#include "Layers/GisLayer.h"
#include "Routing/RoutingDijkstra.h"
#include "Types/GisViewerRoutingBuildOptions.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::CoordinateSystems;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Routing;
using namespace GeoKernel::Viewer;

namespace
{
    class IsochroneOverlay final : public QWidget
    {
    public:
        explicit IsochroneOverlay(GisViewer* viewer) : QWidget(viewer), m_viewer(viewer)
        {
            setAttribute(Qt::WA_TransparentForMouseEvents);
            setAttribute(Qt::WA_TranslucentBackground);
            setGeometry(viewer->rect());
            viewer->installEventFilter(this);
            QObject::connect(viewer, &GisViewer::viewChanged, this, qOverload<>(&QWidget::update));
            show();
            raise();
        }
        void setOrigin(const std::optional<GisShapePoint>& value) { m_origin = value; update(); }
        void setBands(const QVector<QVector<QVector<GisShapePoint>>>& value) { m_bands = value; update(); }
        void setActiveBand(int value) { m_activeBand = value; update(); }
        void clear() { m_origin.reset(); m_bands.clear(); m_activeBand = 0; update(); }
    protected:
        bool eventFilter(QObject* watched, QEvent* event) override
        {
            if (watched == m_viewer && event->type() == QEvent::Resize)
                setGeometry(m_viewer->rect());
            return QWidget::eventFilter(watched, event);
        }
        void paintEvent(QPaintEvent*) override
        {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing, true);
            static const QVector<QColor> colors {
                QColor(QStringLiteral("#16A34A")), QColor(QStringLiteral("#F59E0B")),
                QColor(QStringLiteral("#DC2626")) };
            for (int band = m_bands.size() - 1; band >= 0; --band)
            {
                QColor color = colors[band];
                color.setAlpha(band == m_activeBand ? 250 : 135);
                painter.setPen(QPen(color, band == m_activeBand ? 4.5 : 2.5,
                    Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                for (const QVector<GisShapePoint>& line : m_bands[band])
                {
                    if (line.size() < 2) continue;
                    QPainterPath path;
                    path.moveTo(m_viewer->worldToScreen(line.first()));
                    for (qsizetype i = 1; i < line.size(); ++i)
                        path.lineTo(m_viewer->worldToScreen(line[i]));
                    painter.drawPath(path);
                }
            }
            if (m_origin.has_value())
            {
                const QPointF point = m_viewer->worldToScreen(*m_origin);
                painter.setPen(QPen(QColor(QStringLiteral("#14532D")), 2.0));
                painter.setBrush(QColor(QStringLiteral("#22C55E")));
                painter.drawEllipse(point, 9.0, 9.0);
            }
        }
    private:
        GisViewer* m_viewer = nullptr;
        std::optional<GisShapePoint> m_origin;
        QVector<QVector<QVector<GisShapePoint>>> m_bands;
        int m_activeBand = 0;
    };

    int layerIndex(const GisViewer& viewer, const GisLayer* target)
    {
        for (int i = 0; i < viewer.layerCount(); ++i)
            if (viewer.mapLayerAt(i) == target) return i;
        return -1;
    }
    GisViewerRoutingBuildOptions routingOptions()
    {
        GisViewerRoutingBuildOptions options;
        options.snapTolerance = 1e-6; options.undirected = true;
        options.speedFieldName = QStringLiteral("maxspeed");
        options.nameFieldName = QStringLiteral("name");
        options.oneWayFieldName = QStringLiteral("oneway");
        options.defaultSpeedKmh = 50.0;
        return options;
    }
    GisExtent projectedExtent(const GisExtent& source, const CoordinateTransformer& transformer)
    {
        const QVector<GisShapePoint> corners { {source.xMin(), source.yMin()}, {source.xMin(), source.yMax()},
            {source.xMax(), source.yMin()}, {source.xMax(), source.yMax()} };
        const GisShapePoint first = transformer.transform(corners.first());
        double x1 = first.x(), y1 = first.y(), x2 = first.x(), y2 = first.y();
        for (qsizetype i = 1; i < corners.size(); ++i)
        {
            const GisShapePoint p = transformer.transform(corners[i]);
            x1 = qMin(x1, p.x()); y1 = qMin(y1, p.y()); x2 = qMax(x2, p.x()); y2 = qMax(y2, p.y());
        }
        const GisExtent result(x1, y1, x2, y2);
        return result.inflate(result.width() * 0.04, result.height() * 0.04);
    }
    QSet<int> largestConnectedComponent(const RoutingGraph& graph)
    {
        QSet<int> visited, largest;
        for (auto it = graph.nodes().constBegin(); it != graph.nodes().constEnd(); ++it)
        {
            if (visited.contains(it.key())) continue;
            QSet<int> component; QList<int> queue{it.key()}; visited.insert(it.key());
            for (qsizetype i = 0; i < queue.size(); ++i)
            {
                component.insert(queue[i]);
                for (int neighbor : graph.neighbors(queue[i]))
                    if (!visited.contains(neighbor)) { visited.insert(neighbor); queue.append(neighbor); }
            }
            if (component.size() > largest.size()) largest = std::move(component);
        }
        return largest;
    }
    RoutingNearestNodeResult nearestNode(
        const RoutingGraph& graph, const QSet<int>& component, const RoutingPoint& point, double maximum)
    {
        RoutingNearestNodeResult result;
        for (int id : component)
        {
            const RoutingNode* node = graph.findNode(id); if (!node) continue;
            const double distance = graph.distance(point, node->position);
            if (distance <= maximum && distance < result.distance)
            { result.nodeId = id; result.position = node->position; result.distance = distance; }
        }
        return result;
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Isochrone"));
    QMainWindow window; window.setWindowTitle(QStringLiteral("Isochrone"));
    window.setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico"))); window.resize(1200, 760);

    auto* central = new QWidget(&window); auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    auto* viewer = new GisViewer(central); layout->addWidget(viewer, 1);
    auto* overlay = new IsochroneOverlay(viewer);
    auto* panel = new QWidget(central); panel->setFixedWidth(310); auto* panelLayout = new QVBoxLayout(panel);
    auto* title = new QLabel(QStringLiteral("Travel-time isochrone"), panel);
    QFont font = title->font(); font.setBold(true); title->setFont(font);
    auto* summary = new QLabel(QStringLiteral("Select an origin point."), panel); summary->setWordWrap(true);
    auto* bandList = new QListWidget(panel);
    auto* help = new QLabel(QStringLiteral("Click a band to highlight it.\n\nGreen: 0–5 min\nOrange: 5–10 min\nRed: 10–15 min"), panel);
    panelLayout->addWidget(title); panelLayout->addWidget(summary); panelLayout->addWidget(bandList);
    panelLayout->addWidget(help); panelLayout->addStretch(1); layout->addWidget(panel); window.setCentralWidget(central);

    QToolBar* navigation = createNavigationToolbar(window, *viewer); window.addToolBarBreak();
    auto* toolbar = window.addToolBar(QStringLiteral("Routing")); toolbar->setMovable(false);
    auto* selectButton = new QPushButton(QStringLiteral("Select isochrone origin"), &window); selectButton->setEnabled(false);
    toolbar->addWidget(selectButton); toolbar->addWidget(new QLabel(QStringLiteral("  <b><font color='#16A34A'>●</font> Origin</b>"), toolbar));

    const auto wgs84 = CoordinateSystemFactory::fromEpsg(4326), webMercator = CoordinateSystemFactory::fromEpsg(3857);
    auto toViewer = std::make_shared<CoordinateTransformer>(*wgs84, *webMercator);
    auto toSource = std::make_shared<CoordinateTransformer>(*webMercator, *wgs84);
    auto extent = std::make_shared<GisExtent>(GisExtent::empty()); auto component = std::make_shared<QSet<int>>();
    auto roadLayer = std::make_shared<GisLayer*>(nullptr);
    for (QAction* action : navigation->actions()) if (action->text() == QStringLiteral("Full Extent"))
    { QObject::connect(action, &QAction::triggered, &window, [viewer, extent]{ if (!extent->isEmpty()) viewer->setViewExtent(*extent); }); break; }

    QObject::connect(selectButton, &QPushButton::clicked, &window, [viewer, overlay, bandList, summary]
    { overlay->clear(); bandList->clear(); summary->setText(QStringLiteral("Select an origin point.")); viewer->setActiveTool(GisViewerTool::Route); });
    QObject::connect(bandList, &QListWidget::currentRowChanged, &window,
        [overlay](int row){ if (row >= 0) overlay->setActiveBand(row); });
    QObject::connect(viewer, &GisViewer::mapClicked, &window,
        [viewer, overlay, bandList, summary, roadLayer, component, toViewer, toSource, &window]
        (GisViewerTool tool, const QPointF&, const GisShapePoint& world, Qt::KeyboardModifiers)
    {
        if (tool != GisViewerTool::Route || !*roadLayer) return;
        const RoutingGraph* graph = viewer->routingGraph(); if (!graph) return;
        const GisShapePoint source = toSource->transform(world);
        const RoutingNearestNodeResult origin = nearestNode(*graph, *component, {source.x(), source.y()}, 2000.0);
        if (!origin.found()) { QMessageBox::warning(&window, QStringLiteral("Isochrone"), QStringLiteral("No main-network road node was found nearby.")); return; }
        overlay->setOrigin(toViewer->transform({origin.position.x, origin.position.y}));
        const RoutingDijkstraResult result = RoutingDijkstra::run(*graph, origin.nodeId, -1, RoutingCostMetric::TravelTime, 50.0);
        const QVector<double> limits{300.0, 600.0, 900.0};
        QVector<QVector<QVector<GisShapePoint>>> bands(3); QVector<int> nodes(3, 0), edges(3, 0);
        for (auto it = result.costs.constBegin(); it != result.costs.constEnd(); ++it)
            for (int b = 0; b < 3; ++b) if (it.value() <= limits[b]) ++nodes[b];
        for (const RoutingEdge& edge : graph->edges())
        {
            if (!result.costs.contains(edge.fromId) || !result.costs.contains(edge.toId)) continue;
            const double cost = qMax(result.costTo(edge.fromId), result.costTo(edge.toId));
            const int band = cost <= 300.0 ? 0 : cost <= 600.0 ? 1 : cost <= 900.0 ? 2 : -1;
            if (band < 0 || edge.geometry.size() < 2) continue;
            QVector<GisShapePoint> line; for (const RoutingPoint& p : edge.geometry) line.append(toViewer->transform({p.x, p.y}));
            bands[band].append(std::move(line)); ++edges[band];
        }
        overlay->setBands(bands); bandList->clear();
        for (int b = 0; b < 3; ++b) bandList->addItem(QStringLiteral("Within %1 minutes\n%2 cumulative nodes • %3 band edges")
            .arg((b + 1) * 5).arg(nodes[b]).arg(edges[b]));
        bandList->setCurrentRow(0);
        summary->setText(QStringLiteral("Origin snap: %1 m\n%2 nodes reachable within 15 minutes.")
            .arg(origin.distance, 0, 'f', 1).arg(nodes.last()));
        window.statusBar()->showMessage(QStringLiteral("Isochrone calculated successfully."));
    });

    window.show();
    QMetaObject::invokeMethod(&window, [&window, viewer, selectButton, roadLayer, extent, component, wgs84, webMercator, toViewer]
    {
        const QString path = ensureStockholmLayer(&window); if (path.isEmpty() || !loadLayer(*viewer, path, &window)) return;
        *roadLayer = viewer->mapLayerAt(0); if (!*roadLayer) return; (*roadLayer)->setCoordinateSystem(wgs84);
        *extent = projectedExtent((*roadLayer)->extent(), *toViewer); viewer->setCoordinateSystem(webMercator);
        const int index = layerIndex(*viewer, *roadLayer);
        if (index < 0 || !viewer->buildRoutingGraphForLayer(index, routingOptions()))
        { QMessageBox::critical(&window, QStringLiteral("Isochrone"), QStringLiteral("Routing graph could not be built.")); return; }
        if (const RoutingGraph* graph = viewer->routingGraph()) *component = largestConnectedComponent(*graph);
        viewer->setViewExtent(*extent); viewer->setActiveTool(GisViewerTool::Route); selectButton->setEnabled(true);
        window.statusBar()->showMessage(QStringLiteral("Click the map to select an isochrone origin."));
    });
    return app.exec();
}
