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
#include <QWidget>

#include <optional>
#include <algorithm>
#include <limits>
#include <queue>

#include "SampleSupport.h"
#include "CoordinateSystems/CoordinateSystemFactory.h"
#include "CoordinateSystems/CoordinateTransformer.h"
#include "Layers/GisLayer.h"
#include "Layers/GisLayerStyle.h"
#include "Routing/RoutingCostMetric.h"
#include "Types/GisViewerRoutingBuildOptions.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Routing;
using namespace GeoKernel::Core::CoordinateSystems;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Viewer;

namespace
{
    class RouteMarkerOverlay final : public QWidget
    {
    public:
        explicit RouteMarkerOverlay(GisViewer* viewer)
            : QWidget(viewer), m_viewer(viewer)
        {
            setAttribute(Qt::WA_TransparentForMouseEvents);
            setAttribute(Qt::WA_TranslucentBackground);
            setGeometry(viewer->rect());
            viewer->installEventFilter(this);
            QObject::connect(viewer, &GisViewer::viewChanged, this,
                qOverload<>(&QWidget::update));
            show();
            raise();
        }

        void setStops(const QVector<GisShapePoint>& stops)
        {
            m_stops = stops;
            update();
        }

        void setRoutes(const QVector<QVector<GisShapePoint>>& routes)
        {
            m_routes = routes;
            update();
        }

        void setActiveRoute(int index)
        {
            m_activeRoute = index;
            update();
        }

        void clearRoute()
        {
            m_routes.clear();
            m_highlight.clear();
            m_activeRoute = 0;
            update();
        }

        void setHighlightedGeometry(const QVector<GisShapePoint>& geometry)
        {
            m_highlight = geometry;
            update();
        }

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
            for (int index = 0; index < m_routes.size(); ++index)
            {
                if (index != m_activeRoute)
                    drawRoute(painter, m_routes[index], index, false);
            }
            if (m_activeRoute >= 0 && m_activeRoute < m_routes.size())
                drawRoute(painter, m_routes[m_activeRoute], m_activeRoute, true);
            drawHighlight(painter);
            for (int index = 0; index < m_stops.size(); ++index)
                drawMarker(painter, m_stops[index], index, m_stops.size());
        }

    private:
        void drawHighlight(QPainter& painter)
        {
            if (m_highlight.size() < 2)
                return;
            QPainterPath path;
            path.moveTo(m_viewer->worldToScreen(m_highlight.first()));
            for (qsizetype index = 1; index < m_highlight.size(); ++index)
                path.lineTo(m_viewer->worldToScreen(m_highlight[index]));
            painter.setPen(QPen(QColor(255, 214, 10, 210), 10.0,
                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(path);
            painter.setPen(QPen(QColor(QStringLiteral("#DC2626")), 4.0,
                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(path);
        }

        void drawRoute(
            QPainter& painter,
            const QVector<GisShapePoint>& route,
            int routeIndex,
            bool active)
        {
            if (route.size() < 2)
                return;

            QPainterPath path;
            path.moveTo(m_viewer->worldToScreen(route.first()));
            for (qsizetype index = 1; index < route.size(); ++index)
                path.lineTo(m_viewer->worldToScreen(route[index]));

            static const QVector<QColor> colors {
                QColor(QStringLiteral("#2563EB")),
                QColor(QStringLiteral("#F97316")),
                QColor(QStringLiteral("#9333EA"))
            };
            QColor color = colors[routeIndex % colors.size()];
            color.setAlpha(active ? 255 : 135);
            painter.setPen(QPen(
                color, active ? 5.0 : 3.0,
                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
        }

        void drawMarker(
            QPainter& painter,
            const GisShapePoint& point,
            int index,
            int stopCount)
        {
            const QColor fill = index == 0
                ? QColor(QStringLiteral("#22C55E"))
                : (index == stopCount - 1
                    ? QColor(QStringLiteral("#EF4444"))
                    : QColor(QStringLiteral("#F59E0B")));
            const QColor outline = index == 0
                ? QColor(QStringLiteral("#14532D"))
                : (index == stopCount - 1
                    ? QColor(QStringLiteral("#7F1D1D"))
                    : QColor(QStringLiteral("#78350F")));
            const QPointF screenPoint = m_viewer->worldToScreen(point);
            painter.setPen(QPen(outline, 2.0));
            painter.setBrush(fill);
            painter.drawEllipse(screenPoint, 8.0, 8.0);
            painter.setPen(Qt::white);
            painter.drawText(QRectF(screenPoint.x() - 8.0, screenPoint.y() - 8.0, 16.0, 16.0),
                Qt::AlignCenter, QString::number(index + 1));
        }

        GisViewer* m_viewer = nullptr;
        QVector<QVector<GisShapePoint>> m_routes;
        QVector<GisShapePoint> m_highlight;
        int m_activeRoute = 0;
        QVector<GisShapePoint> m_stops;
    };

    struct AlternativeRoute
    {
        QList<int> nodeIds;
        QVector<int> edgeIds;
        QVector<GisShapePoint> worldGeometry;
        double distance = 0.0;
        double time = 0.0;
    };

    struct RoadDirectionStep
    {
        QString name;
        double distance = 0.0;
        QVector<GisShapePoint> geometry;
    };

    std::optional<AlternativeRoute> findAlternativeRoute(
        const RoutingGraph& graph,
        int startNode,
        int finishNode,
        const QHash<int, double>& edgePenalties,
        const CoordinateTransformer& transformer)
    {
        using QueueEntry = std::pair<double, int>;
        std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> queue;
        QHash<int, double> distances;
        QHash<int, int> previousNode;
        QHash<int, int> previousEdge;
        distances.insert(startNode, 0.0);
        queue.push({ 0.0, startNode });

        while (!queue.empty())
        {
            const auto [distance, nodeId] = queue.top();
            queue.pop();
            if (distance > distances.value(nodeId, std::numeric_limits<double>::infinity()))
                continue;
            if (nodeId == finishNode)
                break;

            for (const RoutingEdge* edge : graph.outEdges(nodeId))
            {
                if (edge == nullptr)
                    continue;
                const double candidate = distance +
                    edge->distance * edgePenalties.value(edge->id, 1.0);
                if (candidate >= distances.value(edge->toId, std::numeric_limits<double>::infinity()))
                    continue;
                distances.insert(edge->toId, candidate);
                previousNode.insert(edge->toId, nodeId);
                previousEdge.insert(edge->toId, edge->id);
                queue.push({ candidate, edge->toId });
            }
        }

        if (!distances.contains(finishNode))
            return std::nullopt;

        AlternativeRoute route;
        int nodeId = finishNode;
        route.nodeIds.prepend(nodeId);
        while (nodeId != startNode)
        {
            if (!previousNode.contains(nodeId) || !previousEdge.contains(nodeId))
                return std::nullopt;
            route.edgeIds.prepend(previousEdge.value(nodeId));
            nodeId = previousNode.value(nodeId);
            route.nodeIds.prepend(nodeId);
        }

        for (int edgeId : route.edgeIds)
        {
            const RoutingEdge* edge = graph.findEdge(edgeId);
            if (edge == nullptr)
                continue;
            route.distance += edge->distance;
            route.time += edge->travelTime();
            for (const RoutingPoint& point : edge->geometry)
            {
                const GisShapePoint worldPoint = transformer.transform(GisShapePoint(point.x, point.y));
                if (route.worldGeometry.isEmpty() ||
                    route.worldGeometry.last().x() != worldPoint.x() ||
                    route.worldGeometry.last().y() != worldPoint.y())
                {
                    route.worldGeometry.append(worldPoint);
                }
            }
        }
        if (route.worldGeometry.size() < 2)
            return std::nullopt;
        return route;
    }

    QSet<int> largestConnectedComponent(const RoutingGraph& graph)
    {
        QSet<int> visited;
        QSet<int> largest;
        for (auto nodeIt = graph.nodes().constBegin(); nodeIt != graph.nodes().constEnd(); ++nodeIt)
        {
            const int seed = nodeIt.key();
            if (visited.contains(seed))
                continue;

            QSet<int> component;
            QList<int> queue{ seed };
            visited.insert(seed);
            for (qsizetype queueIndex = 0; queueIndex < queue.size(); ++queueIndex)
            {
                const int nodeId = queue[queueIndex];
                component.insert(nodeId);
                for (int neighborId : graph.neighbors(nodeId))
                {
                    if (visited.contains(neighborId))
                        continue;
                    visited.insert(neighborId);
                    queue.append(neighborId);
                }
            }
            if (component.size() > largest.size())
                largest = std::move(component);
        }
        return largest;
    }

    RoutingNearestNodeResult nearestNodeInComponent(
        const RoutingGraph& graph,
        const QSet<int>& component,
        const RoutingPoint& point,
        double maxDistance)
    {
        RoutingNearestNodeResult result;
        for (int nodeId : component)
        {
            const RoutingNode* node = graph.findNode(nodeId);
            if (node == nullptr)
                continue;
            const double distance = graph.distance(point, node->position);
            if (distance > maxDistance || distance >= result.distance)
                continue;
            result.nodeId = nodeId;
            result.position = node->position;
            result.distance = distance;
        }
        return result;
    }

    QSet<int> reachableNodes(const RoutingGraph& graph, int startNode)
    {
        QSet<int> reachable;
        QList<int> queue{ startNode };
        reachable.insert(startNode);
        for (qsizetype index = 0; index < queue.size(); ++index)
        {
            for (int neighborId : graph.neighbors(queue[index]))
            {
                if (reachable.contains(neighborId))
                    continue;
                reachable.insert(neighborId);
                queue.append(neighborId);
            }
        }
        return reachable;
    }

    int layerIndex(const GisViewer& viewer, const GisLayer* target)
    {
        for (int index = 0; index < viewer.layerCount(); ++index)
        {
            if (viewer.mapLayerAt(index) == target)
                return index;
        }
        return -1;
    }

    GisViewerRoutingBuildOptions routingOptions()
    {
        GisViewerRoutingBuildOptions options;
        options.snapTolerance = 1e-6;
        options.undirected = true;
        options.speedFieldName = QStringLiteral("maxspeed");
        options.nameFieldName = QStringLiteral("name");
        options.oneWayFieldName = QStringLiteral("oneway");
        options.defaultSpeedKmh = 50.0;
        return options;
    }

    GisExtent projectedExtent(
        const GisExtent& source,
        const CoordinateTransformer& transformer)
    {
        const QVector<GisShapePoint> corners {
            GisShapePoint(source.xMin(), source.yMin()),
            GisShapePoint(source.xMin(), source.yMax()),
            GisShapePoint(source.xMax(), source.yMin()),
            GisShapePoint(source.xMax(), source.yMax())
        };

        GisShapePoint first = transformer.transform(corners.first());
        double xMin = first.x();
        double yMin = first.y();
        double xMax = first.x();
        double yMax = first.y();
        for (int index = 1; index < corners.size(); ++index)
        {
            const GisShapePoint point = transformer.transform(corners[index]);
            xMin = qMin(xMin, point.x());
            yMin = qMin(yMin, point.y());
            xMax = qMax(xMax, point.x());
            yMax = qMax(yMax, point.y());
        }
        const GisExtent result(xMin, yMin, xMax, yMax);
        return result.inflate(result.width() * 0.04, result.height() * 0.04);
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("MultiStopRoute"));

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("MultiStopRoute"));
    window.setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico")));
    window.resize(1200, 760);

    auto* central = new QWidget(&window);
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* viewer = new GisViewer(central);
    layout->addWidget(viewer, 1);
    auto* overlay = new RouteMarkerOverlay(viewer);

    auto* panel = new QWidget(central);
    panel->setFixedWidth(320);
    auto* panelLayout = new QVBoxLayout(panel);
    auto* title = new QLabel(QStringLiteral("Multi-stop route"), panel);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    auto* summary = new QLabel(QStringLiteral("Add at least two stops."), panel);
    auto* legsList = new QListWidget(panel);
    legsList->setMaximumHeight(190);
    auto* roadsTitle = new QLabel(QStringLiteral("Road directions"), panel);
    QFont roadsFont = roadsTitle->font();
    roadsFont.setBold(true);
    roadsTitle->setFont(roadsFont);
    auto* roadsList = new QListWidget(panel);
    panelLayout->addWidget(title);
    panelLayout->addWidget(summary);
    panelLayout->addWidget(legsList);
    panelLayout->addWidget(roadsTitle);
    panelLayout->addWidget(roadsList, 1);
    layout->addWidget(panel);
    window.setCentralWidget(central);

    QToolBar* navigationToolbar = createNavigationToolbar(window, *viewer);
    window.addToolBarBreak();
    auto* routingToolbar = window.addToolBar(QStringLiteral("Routing"));
    routingToolbar->setMovable(false);
    auto* resetButton = new QPushButton(QStringLiteral("New multi-stop route"), &window);
    auto* calculateButton = new QPushButton(QStringLiteral("Calculate route"), &window);
    resetButton->setEnabled(false);
    calculateButton->setEnabled(false);
    routingToolbar->addWidget(resetButton);
    routingToolbar->addWidget(calculateButton);
    routingToolbar->addWidget(new QLabel(
        QStringLiteral("  <b><font color='#16A34A'>●</font> Start</b> &nbsp; "
                       "<b><font color='#F59E0B'>●</font> Stop</b> &nbsp; "
                       "<b><font color='#DC2626'>●</font> Finish</b>"), routingToolbar));

    const auto wgs84 = CoordinateSystemFactory::fromEpsg(4326);
    const auto webMercator = CoordinateSystemFactory::fromEpsg(3857);
    auto toViewer = std::make_shared<CoordinateTransformer>(*wgs84, *webMercator);
    auto toSource = std::make_shared<CoordinateTransformer>(*webMercator, *wgs84);
    auto stockholmExtent = std::make_shared<GisExtent>(GisExtent::empty());
    auto mainComponent = std::make_shared<QSet<int>>();
    auto roadLayer = std::make_shared<GisLayer*>(nullptr);
    auto stopPoints = std::make_shared<QVector<GisShapePoint>>();
    auto stopNodeIds = std::make_shared<QVector<int>>();
    auto legGeometries = std::make_shared<QVector<QVector<GisShapePoint>>>();
    auto roadSteps = std::make_shared<QVector<RoadDirectionStep>>();

    for (QAction* action : navigationToolbar->actions())
    {
        if (action->text() == QStringLiteral("Full Extent"))
        {
            QObject::connect(action, &QAction::triggered, &window, [viewer, stockholmExtent]
            {
                if (!stockholmExtent->isEmpty())
                    viewer->setViewExtent(*stockholmExtent);
            });
            break;
        }
    }

    QObject::connect(legsList, &QListWidget::currentRowChanged, &window,
        [overlay, legGeometries](int index)
    {
        overlay->setHighlightedGeometry(
            index >= 0 && index < legGeometries->size()
                ? legGeometries->at(index)
                : QVector<GisShapePoint>{});
    });
    QObject::connect(roadsList, &QListWidget::currentRowChanged, &window,
        [overlay, roadSteps](int index)
    {
        overlay->setHighlightedGeometry(
            index >= 0 && index < roadSteps->size()
                ? roadSteps->at(index).geometry
                : QVector<GisShapePoint>{});
    });

    QObject::connect(resetButton, &QPushButton::clicked, &window,
        [viewer, overlay, calculateButton, legsList, roadsList, summary,
         stopPoints, stopNodeIds, legGeometries, roadSteps, &window]
    {
        stopPoints->clear();
        stopNodeIds->clear();
        legGeometries->clear();
        roadSteps->clear();
        overlay->setStops(*stopPoints);
        overlay->clearRoute();
        legsList->clear();
        roadsList->clear();
        summary->setText(QStringLiteral("Add at least two stops."));
        calculateButton->setEnabled(false);
        viewer->setActiveTool(GisViewerTool::Route);
        window.statusBar()->showMessage(QStringLiteral("Click the map to add the start point."));
    });

    QObject::connect(viewer, &GisViewer::mapClicked, &window,
        [viewer, overlay, calculateButton, legsList, roadsList, summary,
         roadLayer, stopPoints, stopNodeIds, legGeometries, roadSteps,
         mainComponent, toViewer, toSource, &window]
        (GisViewerTool tool, const QPointF&, const GisShapePoint& worldPoint, Qt::KeyboardModifiers)
    {
        if (tool != GisViewerTool::Route || *roadLayer == nullptr)
            return;
        const RoutingGraph* graph = viewer->routingGraph();
        if (graph == nullptr)
            return;
        const QSet<int> candidates = stopNodeIds->isEmpty()
            ? *mainComponent
            : reachableNodes(*graph, stopNodeIds->last());
        const GisShapePoint sourcePoint = toSource->transform(worldPoint);
        const RoutingNearestNodeResult snapped = nearestNodeInComponent(
            *graph, candidates, RoutingPoint{ sourcePoint.x(), sourcePoint.y() }, 2000.0);
        if (!snapped.found())
        {
            QMessageBox::warning(&window, QStringLiteral("MultiStopRoute"),
                QStringLiteral("No reachable road node was found near this point."));
            return;
        }
        if (!stopNodeIds->isEmpty() && stopNodeIds->last() == snapped.nodeId)
            return;

        stopNodeIds->append(snapped.nodeId);
        stopPoints->append(toViewer->transform(GisShapePoint(snapped.position.x, snapped.position.y)));
        overlay->setStops(*stopPoints);
        overlay->clearRoute();
        legGeometries->clear();
        roadSteps->clear();
        legsList->clear();
        roadsList->clear();
        summary->setText(QStringLiteral("%1 stop(s) selected.").arg(stopPoints->size()));
        calculateButton->setEnabled(stopPoints->size() >= 2);
        window.statusBar()->showMessage(QStringLiteral("Stop %1 added. Add another stop or calculate the route.")
            .arg(stopPoints->size()));
    });

    QObject::connect(calculateButton, &QPushButton::clicked, &window,
        [viewer, overlay, legsList, roadsList, summary, stopNodeIds,
         legGeometries, roadSteps, toViewer, &window]
    {
        const RoutingGraph* graph = viewer->routingGraph();
        if (graph == nullptr || stopNodeIds->size() < 2)
            return;

        QVector<GisShapePoint> combinedGeometry;
        QVector<int> combinedEdges;
        double totalDistance = 0.0;
        double totalTime = 0.0;
        legsList->clear();
        legGeometries->clear();
        roadSteps->clear();
        QHash<int, double> noPenalties;
        for (qsizetype index = 1; index < stopNodeIds->size(); ++index)
        {
            const auto leg = findAlternativeRoute(
                *graph, stopNodeIds->at(index - 1), stopNodeIds->at(index), noPenalties, *toViewer);
            if (!leg.has_value())
            {
                QMessageBox::warning(&window, QStringLiteral("MultiStopRoute"),
                    QStringLiteral("No route was found for leg %1.").arg(index));
                return;
            }
            totalDistance += leg->distance;
            totalTime += leg->time;
            legGeometries->append(leg->worldGeometry);
            combinedEdges += leg->edgeIds;
            for (const GisShapePoint& point : leg->worldGeometry)
            {
                if (combinedGeometry.isEmpty() || combinedGeometry.last().x() != point.x() ||
                    combinedGeometry.last().y() != point.y())
                    combinedGeometry.append(point);
            }
            legsList->addItem(QStringLiteral("%1 → %2   %3 km • %4 min")
                .arg(index).arg(index + 1)
                .arg(leg->distance / 1000.0, 0, 'f', 2)
                .arg(leg->time / 60.0, 0, 'f', 1));
        }
        overlay->setRoutes({ combinedGeometry });

        for (int edgeId : combinedEdges)
        {
            const RoutingEdge* edge = graph->findEdge(edgeId);
            if (edge == nullptr)
                continue;
            QString name = edge->attributes.value(QStringLiteral("name")).toString().trimmed();
            if (name.isEmpty())
                name = QStringLiteral("Unnamed road");
            QVector<GisShapePoint> edgeGeometry;
            for (const RoutingPoint& point : edge->geometry)
                edgeGeometry.append(toViewer->transform(GisShapePoint(point.x, point.y)));
            if (!roadSteps->isEmpty() &&
                roadSteps->last().name.compare(name, Qt::CaseInsensitive) == 0)
            {
                roadSteps->last().distance += edge->distance;
                for (const GisShapePoint& point : edgeGeometry)
                {
                    if (roadSteps->last().geometry.isEmpty() ||
                        roadSteps->last().geometry.last().x() != point.x() ||
                        roadSteps->last().geometry.last().y() != point.y())
                        roadSteps->last().geometry.append(point);
                }
            }
            else
                roadSteps->append(RoadDirectionStep{ name, edge->distance, edgeGeometry });
        }
        roadsList->clear();
        for (qsizetype index = 0; index < roadSteps->size(); ++index)
        {
            const RoadDirectionStep& step = roadSteps->at(index);
            const QString distance = step.distance >= 1000.0
                ? QStringLiteral("%1 km").arg(step.distance / 1000.0, 0, 'f', 1)
                : QStringLiteral("%1 m").arg(step.distance, 0, 'f', 0);
            roadsList->addItem(QStringLiteral("%1. %2\n    %3")
                .arg(index + 1).arg(step.name, distance));
        }
        summary->setText(QStringLiteral("%1 stops\n%2 km  •  %3 min")
            .arg(stopNodeIds->size())
            .arg(totalDistance / 1000.0, 0, 'f', 2)
            .arg(totalTime / 60.0, 0, 'f', 1));
        window.statusBar()->showMessage(QStringLiteral("Multi-stop route calculated successfully."));
    });

    window.show();
    QMetaObject::invokeMethod(&window,
        [&window, viewer, resetButton, roadLayer, stockholmExtent,
         wgs84, webMercator, toViewer, mainComponent]
    {
        const QString path = ensureStockholmLayer(&window);
        if (path.isEmpty() || !loadLayer(*viewer, path, &window))
            return;
        *roadLayer = viewer->mapLayerAt(0);
        if (*roadLayer == nullptr)
            return;
        (*roadLayer)->setCoordinateSystem(wgs84);
        *stockholmExtent = projectedExtent((*roadLayer)->extent(), *toViewer);
        viewer->setCoordinateSystem(webMercator);
        const int roadIndex = layerIndex(*viewer, *roadLayer);
        if (roadIndex < 0 || !viewer->buildRoutingGraphForLayer(roadIndex, routingOptions()))
        {
            QMessageBox::critical(&window, QStringLiteral("MultiStopRoute"),
                QStringLiteral("Routing graph could not be built."));
            return;
        }
        if (const RoutingGraph* graph = viewer->routingGraph())
            *mainComponent = largestConnectedComponent(*graph);
        viewer->setViewExtent(*stockholmExtent);
        viewer->setActiveTool(GisViewerTool::Route);
        resetButton->setEnabled(true);
        window.statusBar()->showMessage(QStringLiteral("Click the map to add the start point."));
    });
    return app.exec();
}
