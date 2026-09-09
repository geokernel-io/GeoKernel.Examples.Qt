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
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <optional>
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

#include "SampleSupport.h"
#include "CoordinateSystems/CoordinateSystemFactory.h"
#include "CoordinateSystems/CoordinateTransformer.h"
#include "Layers/GisLayer.h"
#include "Layers/GisLayerStyle.h"
#include "Routing/RoutingCostMetric.h"
#include "Routing/RoutingDijkstra.h"
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
                QColor(QStringLiteral("#9333EA")),
                QColor(QStringLiteral("#0891B2")),
                QColor(QStringLiteral("#DB2777")),
                QColor(QStringLiteral("#65A30D")),
                QColor(QStringLiteral("#CA8A04")),
                QColor(QStringLiteral("#4F46E5"))
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
            int)
        {
            const QColor fill = index == 0
                ? QColor(QStringLiteral("#22C55E"))
                : QColor(QStringLiteral("#F59E0B"));
            const QColor outline = index == 0
                ? QColor(QStringLiteral("#14532D"))
                : QColor(QStringLiteral("#78350F"));
            const QPointF screenPoint = m_viewer->worldToScreen(point);
            painter.setPen(QPen(outline, 2.0));
            painter.setBrush(fill);
            painter.drawEllipse(screenPoint, 8.0, 8.0);
            painter.setPen(Qt::white);
            painter.drawText(QRectF(screenPoint.x() - 8.0, screenPoint.y() - 8.0, 16.0, 16.0),
                Qt::AlignCenter, index == 0 ? QStringLiteral("D") : QString::number(index));
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
    QApplication::setApplicationName(QStringLiteral("RouteOptimization"));

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("RouteOptimization"));
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
    auto* title = new QLabel(QStringLiteral("Service vehicle routes"), panel);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    auto* summary = new QLabel(QStringLiteral("Select a depot and at least two visits."), panel);
    auto* vehicleRow = new QWidget(panel);
    auto* vehicleLayout = new QHBoxLayout(vehicleRow);
    vehicleLayout->setContentsMargins(0, 0, 0, 0);
    vehicleLayout->addWidget(new QLabel(QStringLiteral("Service vehicles:"), vehicleRow));
    auto* vehicleSpin = new QSpinBox(vehicleRow);
    vehicleSpin->setRange(1, 1);
    vehicleSpin->setValue(1);
    vehicleLayout->addWidget(vehicleSpin);
    auto* legsList = new QListWidget(panel);
    legsList->setMaximumHeight(190);
    auto* roadsTitle = new QLabel(QStringLiteral("Road directions"), panel);
    QFont roadsFont = roadsTitle->font();
    roadsFont.setBold(true);
    roadsTitle->setFont(roadsFont);
    auto* roadsList = new QListWidget(panel);
    panelLayout->addWidget(title);
    panelLayout->addWidget(summary);
    panelLayout->addWidget(vehicleRow);
    panelLayout->addWidget(legsList);
    panelLayout->addWidget(roadsTitle);
    panelLayout->addWidget(roadsList, 1);
    layout->addWidget(panel);
    window.setCentralWidget(central);

    QToolBar* navigationToolbar = createNavigationToolbar(window, *viewer);
    window.addToolBarBreak();
    auto* routingToolbar = window.addToolBar(QStringLiteral("Routing"));
    routingToolbar->setMovable(false);
    auto* resetButton = new QPushButton(QStringLiteral("New optimization"), &window);
    auto* calculateButton = new QPushButton(QStringLiteral("Optimize route"), &window);
    resetButton->setEnabled(false);
    calculateButton->setEnabled(false);
    routingToolbar->addWidget(resetButton);
    routingToolbar->addWidget(calculateButton);
    routingToolbar->addWidget(new QLabel(
        QStringLiteral("  <b><font color='#16A34A'>●</font> Depot</b> &nbsp; "
                       "<b><font color='#F59E0B'>●</font> Visit</b>"), routingToolbar));

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
    auto vehicleRoadSteps = std::make_shared<QVector<QVector<RoadDirectionStep>>>();
    auto selectedVehicle = std::make_shared<int>(-1);

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
        [overlay, legGeometries, roadsList, vehicleRoadSteps, selectedVehicle](int index)
    {
        *selectedVehicle = index;
        overlay->setHighlightedGeometry(
            index >= 0 && index < legGeometries->size()
                ? legGeometries->at(index)
                : QVector<GisShapePoint>{});
        roadsList->clear();
        if (index < 0 || index >= vehicleRoadSteps->size())
            return;
        const QVector<RoadDirectionStep>& steps = vehicleRoadSteps->at(index);
        for (qsizetype stepIndex = 0; stepIndex < steps.size(); ++stepIndex)
        {
            const RoadDirectionStep& step = steps[stepIndex];
            const QString distance = step.distance >= 1000.0
                ? QStringLiteral("%1 km").arg(step.distance / 1000.0, 0, 'f', 1)
                : QStringLiteral("%1 m").arg(step.distance, 0, 'f', 0);
            roadsList->addItem(QStringLiteral("%1. %2\n    %3")
                .arg(stepIndex + 1).arg(step.name, distance));
        }
    });
    QObject::connect(roadsList, &QListWidget::currentRowChanged, &window,
        [overlay, vehicleRoadSteps, selectedVehicle](int index)
    {
        if (*selectedVehicle < 0 || *selectedVehicle >= vehicleRoadSteps->size())
            return;
        const QVector<RoadDirectionStep>& steps = vehicleRoadSteps->at(*selectedVehicle);
        overlay->setHighlightedGeometry(
            index >= 0 && index < steps.size()
                ? steps[index].geometry
                : QVector<GisShapePoint>{});
    });

    QObject::connect(resetButton, &QPushButton::clicked, &window,
        [viewer, overlay, calculateButton, vehicleSpin, legsList, roadsList, summary,
         stopPoints, stopNodeIds, legGeometries, vehicleRoadSteps, selectedVehicle, &window]
    {
        stopPoints->clear();
        stopNodeIds->clear();
        legGeometries->clear();
        vehicleRoadSteps->clear();
        *selectedVehicle = -1;
        overlay->setStops(*stopPoints);
        overlay->clearRoute();
        legsList->clear();
        roadsList->clear();
        summary->setText(QStringLiteral("Select a depot and at least two visits."));
        calculateButton->setEnabled(false);
        vehicleSpin->setMaximum(1);
        vehicleSpin->setValue(1);
        viewer->setActiveTool(GisViewerTool::Route);
        window.statusBar()->showMessage(QStringLiteral("Click the map to add the depot."));
    });

    QObject::connect(viewer, &GisViewer::mapClicked, &window,
        [viewer, overlay, calculateButton, vehicleSpin, legsList, roadsList, summary,
         roadLayer, stopPoints, stopNodeIds, legGeometries, vehicleRoadSteps,
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
            QMessageBox::warning(&window, QStringLiteral("RouteOptimization"),
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
        vehicleRoadSteps->clear();
        legsList->clear();
        roadsList->clear();
        summary->setText(QStringLiteral("Depot + %1 visit(s) selected.").arg(qMax(0, stopPoints->size() - 1)));
        vehicleSpin->setMaximum(qMax(1, stopPoints->size() - 1));
        calculateButton->setEnabled(stopPoints->size() >= 3);
        window.statusBar()->showMessage(stopPoints->size() == 1
            ? QStringLiteral("Depot selected. Add at least two visit points.")
            : QStringLiteral("Visit %1 added. Add another visit or optimize.").arg(stopPoints->size() - 1));
    });

    QObject::connect(calculateButton, &QPushButton::clicked, &window,
        [viewer, overlay, legsList, roadsList, summary, vehicleSpin, stopNodeIds,
         legGeometries, vehicleRoadSteps, toViewer, &window]
    {
        const RoutingGraph* graph = viewer->routingGraph();
        if (graph == nullptr || stopNodeIds->size() < 3)
            return;

        QList<int> selectedNodes;
        for (int nodeId : *stopNodeIds)
            selectedNodes.append(nodeId);
        const QVector<QVector<double>> matrix = RoutingDijkstra::allPairsDistances(
            *graph, selectedNodes);
        auto orderCost = [&matrix](const QVector<int>& order)
        {
            double cost = 0.0;
            for (qsizetype index = 1; index < order.size(); ++index)
                cost += matrix[order[index - 1]][order[index]];
            cost += matrix[order.last()][0];
            return cost;
        };

        QVector<int> optimizedOrder{ 0 };
        QSet<int> remaining;
        for (int index = 1; index < stopNodeIds->size(); ++index)
            remaining.insert(index);
        while (!remaining.isEmpty())
        {
            const int current = optimizedOrder.last();
            int best = -1;
            double bestCost = std::numeric_limits<double>::infinity();
            for (int candidate : remaining)
            {
                if (matrix[current][candidate] < bestCost)
                {
                    best = candidate;
                    bestCost = matrix[current][candidate];
                }
            }
            if (best < 0 || !std::isfinite(bestCost))
            {
                QMessageBox::warning(&window, QStringLiteral("RouteOptimization"),
                    QStringLiteral("The selected visits cannot form a connected tour."));
                return;
            }
            optimizedOrder.append(best);
            remaining.remove(best);
        }

        bool improved = true;
        while (improved)
        {
            improved = false;
            double bestCost = orderCost(optimizedOrder);
            for (int first = 1; first < optimizedOrder.size(); ++first)
            {
                for (int second = first + 1; second < optimizedOrder.size(); ++second)
                {
                    QVector<int> candidate = optimizedOrder;
                    std::swap(candidate[first], candidate[second]);
                    const double candidateCost = orderCost(candidate);
                    if (candidateCost + 0.01 < bestCost)
                    {
                        optimizedOrder = std::move(candidate);
                        bestCost = candidateCost;
                        improved = true;
                    }
                }
            }
        }

        const int vehicleCount = qMin(vehicleSpin->value(), stopNodeIds->size() - 1);
        vehicleSpin->setValue(vehicleCount);
        QVector<QVector<int>> vehicleOrders(vehicleCount, QVector<int>{ 0 });
        const int visitCount = optimizedOrder.size() - 1;
        for (int position = 1; position < optimizedOrder.size(); ++position)
        {
            const int vehicle = qMin(vehicleCount - 1,
                ((position - 1) * vehicleCount) / visitCount);
            vehicleOrders[vehicle].append(optimizedOrder[position]);
        }

        QVector<QVector<GisShapePoint>> vehicleGeometries;
        QVector<QVector<int>> vehicleEdges;
        double totalDistance = 0.0;
        double totalTime = 0.0;
        double longestVehicleDistance = 0.0;
        legsList->clear();
        legGeometries->clear();
        vehicleRoadSteps->clear();
        QHash<int, double> noPenalties;
        auto stopLabel = [](int selectedIndex)
        {
            return selectedIndex == 0
                ? QStringLiteral("D")
                : QString::number(selectedIndex);
        };
        for (int vehicle = 0; vehicle < vehicleOrders.size(); ++vehicle)
        {
            const QVector<int>& order = vehicleOrders[vehicle];
            QVector<GisShapePoint> vehicleGeometry;
            QVector<int> currentVehicleEdges;
            double vehicleDistance = 0.0;
            double vehicleTime = 0.0;
            for (qsizetype index = 1; index <= order.size(); ++index)
            {
                const int fromIndex = order[index - 1];
                const int toIndex = index == order.size() ? 0 : order[index];
                const auto leg = findAlternativeRoute(
                    *graph, stopNodeIds->at(fromIndex), stopNodeIds->at(toIndex), noPenalties, *toViewer);
                if (!leg.has_value())
                {
                    QMessageBox::warning(&window, QStringLiteral("RouteOptimization"),
                        QStringLiteral("No route was found for vehicle %1.").arg(vehicle + 1));
                    return;
                }
                vehicleDistance += leg->distance;
                vehicleTime += leg->time;
                currentVehicleEdges += leg->edgeIds;
                for (const GisShapePoint& point : leg->worldGeometry)
                {
                    if (vehicleGeometry.isEmpty() || vehicleGeometry.last().x() != point.x() ||
                        vehicleGeometry.last().y() != point.y())
                        vehicleGeometry.append(point);
                }
            }
            totalDistance += vehicleDistance;
            totalTime += vehicleTime;
            longestVehicleDistance = qMax(longestVehicleDistance, vehicleDistance);
            vehicleGeometries.append(vehicleGeometry);
            vehicleEdges.append(currentVehicleEdges);
            legGeometries->append(vehicleGeometry);
            QStringList labels;
            for (int selectedIndex : order)
                labels.append(stopLabel(selectedIndex));
            labels.append(QStringLiteral("D"));
            legsList->addItem(QStringLiteral("Vehicle %1: %2\n%3 km • %4 min")
                .arg(vehicle + 1)
                .arg(labels.join(QStringLiteral(" → ")))
                .arg(vehicleDistance / 1000.0, 0, 'f', 2)
                .arg(vehicleTime / 60.0, 0, 'f', 1));
        }
        overlay->setRoutes(vehicleGeometries);

        for (const QVector<int>& edgeIds : vehicleEdges)
        {
            QVector<RoadDirectionStep> steps;
            for (int edgeId : edgeIds)
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
                if (!steps.isEmpty() &&
                    steps.last().name.compare(name, Qt::CaseInsensitive) == 0)
                {
                    steps.last().distance += edge->distance;
                    for (const GisShapePoint& point : edgeGeometry)
                    {
                        if (steps.last().geometry.isEmpty() ||
                            steps.last().geometry.last().x() != point.x() ||
                            steps.last().geometry.last().y() != point.y())
                            steps.last().geometry.append(point);
                    }
                }
                else
                    steps.append(RoadDirectionStep{ name, edge->distance, edgeGeometry });
            }
            vehicleRoadSteps->append(std::move(steps));
        }
        roadsList->clear();
        legsList->setCurrentRow(-1);
        legsList->setCurrentRow(0);
        summary->setText(QStringLiteral(
            "%1 vehicles • %2 visits\nFleet distance: %3 km\n"
            "Longest route: %4 km\nCombined driving time: %5 min")
            .arg(vehicleCount)
            .arg(stopNodeIds->size() - 1)
            .arg(totalDistance / 1000.0, 0, 'f', 2)
            .arg(longestVehicleDistance / 1000.0, 0, 'f', 2)
            .arg(totalTime / 60.0, 0, 'f', 1));
        window.statusBar()->showMessage(QStringLiteral("Routes optimized for all service vehicles."));
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
            QMessageBox::critical(&window, QStringLiteral("RouteOptimization"),
                QStringLiteral("Routing graph could not be built."));
            return;
        }
        if (const RoutingGraph* graph = viewer->routingGraph())
            *mainComponent = largestConnectedComponent(*graph);
        viewer->setViewExtent(*stockholmExtent);
        viewer->setActiveTool(GisViewerTool::Route);
        resetButton->setEnabled(true);
        window.statusBar()->showMessage(QStringLiteral("Click the map to add the depot."));
    });
    return app.exec();
}
