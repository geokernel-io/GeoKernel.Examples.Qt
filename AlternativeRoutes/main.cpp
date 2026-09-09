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

        void setRouteMarkers(
            const std::optional<GisShapePoint>& start,
            const std::optional<GisShapePoint>& finish)
        {
            m_start = start;
            m_finish = finish;
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
            m_activeRoute = 0;
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
            drawMarker(painter, m_start, QColor(QStringLiteral("#22C55E")),
                QColor(QStringLiteral("#14532D")));
            drawMarker(painter, m_finish, QColor(QStringLiteral("#EF4444")),
                QColor(QStringLiteral("#7F1D1D")));
        }

    private:
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
            const std::optional<GisShapePoint>& point,
            const QColor& fill,
            const QColor& outline)
        {
            if (!point.has_value())
                return;

            const QPointF screenPoint = m_viewer->worldToScreen(*point);
            painter.setPen(QPen(outline, 2.0));
            painter.setBrush(fill);
            painter.drawEllipse(screenPoint, 8.0, 8.0);
        }

        GisViewer* m_viewer = nullptr;
        QVector<QVector<GisShapePoint>> m_routes;
        int m_activeRoute = 0;
        std::optional<GisShapePoint> m_start;
        std::optional<GisShapePoint> m_finish;
    };

    struct AlternativeRoute
    {
        QList<int> nodeIds;
        QVector<int> edgeIds;
        QVector<GisShapePoint> worldGeometry;
        double distance = 0.0;
        double time = 0.0;
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
    QApplication::setApplicationName(QStringLiteral("AlternativeRoutes"));

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("AlternativeRoutes"));
    window.setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico")));
    window.resize(1200, 760);

    auto* central = new QWidget(&window);
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* viewer = new GisViewer(central);
    viewer->setActiveTool(GisViewerTool::Pan);
    layout->addWidget(viewer, 1);
    auto* markerOverlay = new RouteMarkerOverlay(viewer);

    auto* directionsPanel = new QWidget(central);
    directionsPanel->setFixedWidth(300);
    auto* directionsLayout = new QVBoxLayout(directionsPanel);
    directionsLayout->setContentsMargins(10, 10, 10, 10);
    auto* directionsTitle = new QLabel(QStringLiteral("Alternative routes"), directionsPanel);
    QFont titleFont = directionsTitle->font();
    titleFont.setBold(true);
    directionsTitle->setFont(titleFont);
    auto* routeSummary = new QLabel(QStringLiteral("Select a start and finish point."), directionsPanel);
    routeSummary->setWordWrap(true);
    auto* alternativesList = new QListWidget(directionsPanel);
    alternativesList->setMaximumHeight(150);
    auto* roadTitle = new QLabel(QStringLiteral("Road directions"), directionsPanel);
    QFont roadTitleFont = roadTitle->font();
    roadTitleFont.setBold(true);
    roadTitle->setFont(roadTitleFont);
    auto* routeList = new QListWidget(directionsPanel);
    directionsLayout->addWidget(directionsTitle);
    directionsLayout->addWidget(routeSummary);
    directionsLayout->addWidget(alternativesList);
    directionsLayout->addWidget(roadTitle);
    directionsLayout->addWidget(routeList, 1);
    layout->addWidget(directionsPanel);
    window.setCentralWidget(central);
    QToolBar* navigationToolbar = createNavigationToolbar(window, *viewer);

    auto* routeButton = new QPushButton(QStringLiteral("Select route points"), &window);
    routeButton->setEnabled(false);
    window.addToolBarBreak();
    auto* routingToolbar = window.addToolBar(QStringLiteral("Routing"));
    routingToolbar->setMovable(false);
    routingToolbar->addWidget(routeButton);
    auto* markerLegend = new QLabel(
        QStringLiteral("  <b><font color='#16A34A'>●</font> Start</b> &nbsp;&nbsp; "
                       "<b><font color='#DC2626'>●</font> Finish</b>"),
        routingToolbar);
    markerLegend->setTextFormat(Qt::RichText);
    routingToolbar->addWidget(markerLegend);

    const auto wgs84 = CoordinateSystemFactory::fromEpsg(4326);
    const auto webMercator = CoordinateSystemFactory::fromEpsg(3857);
    auto wgs84ToWebMercator = std::make_shared<CoordinateTransformer>(*wgs84, *webMercator);
    auto webMercatorToWgs84 = std::make_shared<CoordinateTransformer>(*webMercator, *wgs84);
    auto stockholmExtent = std::make_shared<GisExtent>(GisExtent::empty());
    auto mainRoadComponent = std::make_shared<QSet<int>>();

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
    auto roadLayer = std::make_shared<GisLayer*>(nullptr);
    auto startPoint = std::make_shared<std::optional<GisShapePoint>>();
    auto endPoint = std::make_shared<std::optional<GisShapePoint>>();
    auto startNodeId = std::make_shared<int>(-1);
    auto routes = std::make_shared<QVector<AlternativeRoute>>();

    QObject::connect(alternativesList, &QListWidget::currentRowChanged, &window,
        [viewer, markerOverlay, routeList, routeSummary, routes](int routeIndex)
    {
        if (routeIndex < 0 || routeIndex >= routes->size())
            return;
        const AlternativeRoute& route = routes->at(routeIndex);
        markerOverlay->setActiveRoute(routeIndex);
        routeSummary->setText(QStringLiteral("Alternative %1\n%2 km  •  %3 min")
            .arg(routeIndex + 1)
            .arg(route.distance / 1000.0, 0, 'f', 2)
            .arg(route.time / 60.0, 0, 'f', 1));

        struct RoadStep { QString name; double distance = 0.0; };
        QVector<RoadStep> steps;
        const RoutingGraph* graph = viewer->routingGraph();
        if (graph != nullptr)
        {
            for (int edgeId : route.edgeIds)
            {
                const RoutingEdge* edge = graph->findEdge(edgeId);
                if (edge == nullptr)
                    continue;
                QString name = edge->attributes.value(QStringLiteral("name")).toString().trimmed();
                if (name.isEmpty())
                    name = QStringLiteral("Unnamed road");
                if (!steps.isEmpty() && steps.last().name.compare(name, Qt::CaseInsensitive) == 0)
                    steps.last().distance += edge->distance;
                else
                    steps.append(RoadStep{ name, edge->distance });
            }
        }

        routeList->clear();
        for (qsizetype index = 0; index < steps.size(); ++index)
        {
            const RoadStep& step = steps[index];
            const QString distance = step.distance >= 1000.0
                ? QStringLiteral("%1 km").arg(step.distance / 1000.0, 0, 'f', 1)
                : QStringLiteral("%1 m").arg(step.distance, 0, 'f', 0);
            routeList->addItem(QStringLiteral("%1. %2\n    %3")
                .arg(index + 1).arg(step.name, distance));
        }
    });

    QObject::connect(routeButton, &QPushButton::clicked, &window,
        [viewer, markerOverlay, alternativesList, routeList, routeSummary, routes,
         startPoint, endPoint, startNodeId, &window]
    {
        markerOverlay->clearRoute();
        routes->clear();
        alternativesList->clear();
        routeList->clear();
        routeSummary->setText(QStringLiteral("Select a start and finish point."));
        startPoint->reset();
        endPoint->reset();
        *startNodeId = -1;
        markerOverlay->setRouteMarkers(*startPoint, *endPoint);
        viewer->setActiveTool(GisViewerTool::Route);
        window.statusBar()->showMessage(QStringLiteral("Click the map to choose the start point."));
    });

    QObject::connect(viewer, &GisViewer::mapClicked, &window,
        [viewer, markerOverlay, alternativesList, routeList, routeSummary, routes,
         startNodeId, &window, routeButton, roadLayer, startPoint, endPoint,
         wgs84ToWebMercator, webMercatorToWgs84, mainRoadComponent]
        (GisViewerTool, const QPointF&, const GisShapePoint& worldPoint, Qt::KeyboardModifiers)
    {
        if (*roadLayer == nullptr)
            return;

        const RoutingGraph* routingGraph = viewer->routingGraph();
        const GisShapePoint sourcePoint = webMercatorToWgs84->transform(worldPoint);
        const QSet<int> snapComponent =
            routingGraph != nullptr && startPoint->has_value() && !endPoint->has_value()
            ? reachableNodes(*routingGraph, *startNodeId)
            : *mainRoadComponent;
        const RoutingNearestNodeResult snapped = routingGraph != nullptr
            ? nearestNodeInComponent(
                *routingGraph,
                snapComponent,
                RoutingPoint{ sourcePoint.x(), sourcePoint.y() },
                2000.0)
            : RoutingNearestNodeResult{};
        if (!snapped.found())
        {
            QMessageBox::warning(&window, QStringLiteral("AlternativeRoutes"),
                QStringLiteral("No road node was found near the selected point."));
            return;
        }
        const GisShapePoint snappedWorldPoint = wgs84ToWebMercator->transform(
            GisShapePoint(snapped.position.x, snapped.position.y));

        if (!startPoint->has_value() || endPoint->has_value())
        {
            markerOverlay->clearRoute();
            routes->clear();
            alternativesList->clear();
            routeList->clear();
            routeSummary->setText(QStringLiteral("Select the finish point."));
            *startPoint = snappedWorldPoint;
            *startNodeId = snapped.nodeId;
            endPoint->reset();
            markerOverlay->setRouteMarkers(*startPoint, *endPoint);
            routeButton->setEnabled(true);
            window.statusBar()->showMessage(QStringLiteral("Start selected. Click the map to choose the finish point."));
            return;
        }

        *endPoint = snappedWorldPoint;
        markerOverlay->setRouteMarkers(*startPoint, *endPoint);

        const RoutingGraph* graph = viewer->routingGraph();
        if (graph == nullptr || *startNodeId < 0)
        {
            QMessageBox::critical(&window, QStringLiteral("AlternativeRoutes"),
                QStringLiteral("Routing graph is not available."));
            return;
        }

        routes->clear();
        QHash<int, double> penalties;
        QSet<QString> signatures;
        for (int attempt = 0; attempt < 12 && routes->size() < 3; ++attempt)
        {
            const std::optional<AlternativeRoute> candidate = findAlternativeRoute(
                *graph, *startNodeId, snapped.nodeId, penalties, *wgs84ToWebMercator);
            if (!candidate.has_value())
                break;

            QStringList signatureParts;
            for (int edgeId : candidate->edgeIds)
                signatureParts.append(QString::number(edgeId));
            const QString signature = signatureParts.join(QLatin1Char(','));
            if (!signatures.contains(signature))
            {
                signatures.insert(signature);
                routes->append(*candidate);
            }
            for (int edgeId : candidate->edgeIds)
            {
                penalties.insert(edgeId, penalties.value(edgeId, 1.0) * 4.0);
                const RoutingEdge* edge = graph->findEdge(edgeId);
                if (edge != nullptr)
                {
                    const RoutingEdge* reverse = graph->findEdge(edge->toId, edge->fromId);
                    if (reverse != nullptr)
                        penalties.insert(reverse->id, penalties.value(reverse->id, 1.0) * 4.0);
                }
            }
        }

        if (routes->isEmpty())
        {
            QMessageBox::warning(&window, QStringLiteral("AlternativeRoutes"),
                QStringLiteral("No connected route was found."));
            return;
        }

        QVector<QVector<GisShapePoint>> routeGeometries;
        alternativesList->clear();
        for (qsizetype index = 0; index < routes->size(); ++index)
        {
            const AlternativeRoute& route = routes->at(index);
            routeGeometries.append(route.worldGeometry);
            alternativesList->addItem(QStringLiteral("%1. %2 km  •  %3 min")
                .arg(index + 1)
                .arg(route.distance / 1000.0, 0, 'f', 2)
                .arg(route.time / 60.0, 0, 'f', 1));
        }
        markerOverlay->setRoutes(routeGeometries);
        alternativesList->setCurrentRow(0);

        window.statusBar()->showMessage(
            QStringLiteral("%1 alternative route(s) found.").arg(routes->size()));
    });

    window.show();
    QMetaObject::invokeMethod(&window,
        [&window, viewer, routeButton, roadLayer, stockholmExtent,
         wgs84, webMercator, wgs84ToWebMercator, mainRoadComponent]
    {
        const QString path = ensureStockholmLayer(&window);
        if (path.isEmpty() || !loadLayer(*viewer, path, &window))
            return;

        *roadLayer = viewer->mapLayerAt(0);
        if (*roadLayer == nullptr)
            return;
        (*roadLayer)->setCoordinateSystem(wgs84);
        *stockholmExtent = projectedExtent((*roadLayer)->extent(), *wgs84ToWebMercator);

        viewer->setCoordinateSystem(webMercator);
        const int roadLayerIndex = layerIndex(*viewer, *roadLayer);
        if (roadLayerIndex < 0 || !viewer->buildRoutingGraphForLayer(roadLayerIndex, routingOptions()))
        {
            QMessageBox::critical(&window, QStringLiteral("AlternativeRoutes"),
                QStringLiteral("Routing graph could not be built."));
            return;
        }
        if (const RoutingGraph* graph = viewer->routingGraph())
            *mainRoadComponent = largestConnectedComponent(*graph);
        if (mainRoadComponent->isEmpty())
        {
            QMessageBox::critical(&window, QStringLiteral("AlternativeRoutes"),
                QStringLiteral("The main connected road network could not be identified."));
            return;
        }
        viewer->setViewExtent(*stockholmExtent);
        viewer->setActiveTool(GisViewerTool::Route);
        routeButton->setEnabled(true);
        window.statusBar()->showMessage(QStringLiteral("Click the map to choose the start point."));
    });

    return app.exec();
}
