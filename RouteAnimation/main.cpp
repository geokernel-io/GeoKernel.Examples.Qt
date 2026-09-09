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
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <optional>
#include <cmath>

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

        void setRouteGeometry(const QVector<GisShapePoint>& route)
        {
            m_route = route;
            m_vehicleProgress = route.size() >= 2 ? 0.0 : -1.0;
            update();
        }

        void setVehicleProgress(double progress)
        {
            m_vehicleProgress = qBound(0.0, progress, 1.0);
            update();
        }

        void clearRoute()
        {
            m_route.clear();
            m_vehicleProgress = -1.0;
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
            drawRoute(painter);
            drawMarker(painter, m_start, QColor(QStringLiteral("#22C55E")),
                QColor(QStringLiteral("#14532D")));
            drawMarker(painter, m_finish, QColor(QStringLiteral("#EF4444")),
                QColor(QStringLiteral("#7F1D1D")));
            drawVehicle(painter);
        }

    private:
        void drawVehicle(QPainter& painter)
        {
            if (m_vehicleProgress < 0.0 || m_route.size() < 2)
                return;
            QVector<double> lengths;
            lengths.reserve(m_route.size() - 1);
            double totalLength = 0.0;
            for (qsizetype index = 1; index < m_route.size(); ++index)
            {
                const double dx = m_route[index].x() - m_route[index - 1].x();
                const double dy = m_route[index].y() - m_route[index - 1].y();
                const double length = std::hypot(dx, dy);
                lengths.append(length);
                totalLength += length;
            }
            if (totalLength <= 0.0)
                return;

            const double target = totalLength * m_vehicleProgress;
            double traversed = 0.0;
            GisShapePoint position = m_route.first();
            GisShapePoint directionPoint = m_route[1];
            for (qsizetype index = 1; index < m_route.size(); ++index)
            {
                const double length = lengths[index - 1];
                if (target <= traversed + length || index == m_route.size() - 1)
                {
                    const double ratio = length > 0.0
                        ? qBound(0.0, (target - traversed) / length, 1.0)
                        : 0.0;
                    position = GisShapePoint(
                        m_route[index - 1].x() + (m_route[index].x() - m_route[index - 1].x()) * ratio,
                        m_route[index - 1].y() + (m_route[index].y() - m_route[index - 1].y()) * ratio);
                    directionPoint = m_route[index];
                    break;
                }
                traversed += length;
            }

            const QPointF screen = m_viewer->worldToScreen(position);
            const QPointF direction = m_viewer->worldToScreen(directionPoint);
            const double angle = std::atan2(direction.y() - screen.y(), direction.x() - screen.x());
            painter.setPen(QPen(QColor(QStringLiteral("#1E3A8A")), 2.0));
            painter.setBrush(QColor(QStringLiteral("#2563EB")));
            painter.drawEllipse(screen, 10.0, 10.0);
            painter.setPen(QPen(Qt::white, 3.0, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(screen, screen + QPointF(std::cos(angle) * 7.0, std::sin(angle) * 7.0));
        }

        void drawRoute(QPainter& painter)
        {
            if (m_route.size() < 2)
                return;

            QPainterPath path;
            path.moveTo(m_viewer->worldToScreen(m_route.first()));
            for (qsizetype index = 1; index < m_route.size(); ++index)
                path.lineTo(m_viewer->worldToScreen(m_route[index]));

            painter.setPen(QPen(
                QColor(QStringLiteral("#EF4444")), 4.0,
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
        QVector<GisShapePoint> m_route;
        double m_vehicleProgress = -1.0;
        std::optional<GisShapePoint> m_start;
        std::optional<GisShapePoint> m_finish;
    };

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

    QSet<int> largestConnectedComponent(const RoutingGraph& graph)
    {
        QSet<int> visited;
        QSet<int> largest;
        for (auto it = graph.nodes().constBegin(); it != graph.nodes().constEnd(); ++it)
        {
            if (visited.contains(it.key()))
                continue;
            QSet<int> component;
            QList<int> queue{ it.key() };
            visited.insert(it.key());
            for (qsizetype index = 0; index < queue.size(); ++index)
            {
                const int nodeId = queue[index];
                component.insert(nodeId);
                for (int neighbor : graph.neighbors(nodeId))
                {
                    if (!visited.contains(neighbor))
                    {
                        visited.insert(neighbor);
                        queue.append(neighbor);
                    }
                }
            }
            if (component.size() > largest.size())
                largest = std::move(component);
        }
        return largest;
    }

    QSet<int> reachableNodes(const RoutingGraph& graph, int startNode)
    {
        QSet<int> result{ startNode };
        QList<int> queue{ startNode };
        for (qsizetype index = 0; index < queue.size(); ++index)
        {
            for (int neighbor : graph.neighbors(queue[index]))
            {
                if (!result.contains(neighbor))
                {
                    result.insert(neighbor);
                    queue.append(neighbor);
                }
            }
        }
        return result;
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
            if (distance <= maxDistance && distance < result.distance)
            {
                result.nodeId = nodeId;
                result.position = node->position;
                result.distance = distance;
            }
        }
        return result;
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("RouteAnimation"));

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("RouteAnimation"));
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
    auto* directionsTitle = new QLabel(QStringLiteral("Route directions"), directionsPanel);
    QFont titleFont = directionsTitle->font();
    titleFont.setBold(true);
    directionsTitle->setFont(titleFont);
    auto* routeSummary = new QLabel(QStringLiteral("Select a start and finish point."), directionsPanel);
    routeSummary->setWordWrap(true);
    auto* animationStatus = new QLabel(QStringLiteral("Animation is waiting for a route."), directionsPanel);
    animationStatus->setWordWrap(true);
    auto* animationButtons = new QWidget(directionsPanel);
    auto* animationButtonLayout = new QHBoxLayout(animationButtons);
    animationButtonLayout->setContentsMargins(0, 0, 0, 0);
    auto* playButton = new QPushButton(QStringLiteral("Play"), animationButtons);
    auto* pauseButton = new QPushButton(QStringLiteral("Pause"), animationButtons);
    auto* resetAnimationButton = new QPushButton(QStringLiteral("Reset"), animationButtons);
    playButton->setEnabled(false);
    pauseButton->setEnabled(false);
    resetAnimationButton->setEnabled(false);
    animationButtonLayout->addWidget(playButton);
    animationButtonLayout->addWidget(pauseButton);
    animationButtonLayout->addWidget(resetAnimationButton);
    auto* routeList = new QListWidget(directionsPanel);
    directionsLayout->addWidget(directionsTitle);
    directionsLayout->addWidget(routeSummary);
    directionsLayout->addWidget(animationStatus);
    directionsLayout->addWidget(animationButtons);
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
    auto animationProgress = std::make_shared<double>(0.0);
    auto routeDistance = std::make_shared<double>(0.0);
    auto routeTime = std::make_shared<double>(0.0);
    auto animationDurationMs = std::make_shared<double>(5000.0);
    auto* animationTimer = new QTimer(&window);
    animationTimer->setInterval(33);

    QObject::connect(animationTimer, &QTimer::timeout, &window,
        [animationTimer, markerOverlay, animationStatus, pauseButton,
         animationProgress, routeDistance, routeTime, animationDurationMs]
    {
        *animationProgress = qMin(1.0,
            *animationProgress + animationTimer->interval() / *animationDurationMs);
        markerOverlay->setVehicleProgress(*animationProgress);
        const double remainingDistance = *routeDistance * (1.0 - *animationProgress);
        const double remainingTime = *routeTime * (1.0 - *animationProgress);
        animationStatus->setText(QStringLiteral(
            "Progress: %1%\nRemaining: %2 km • %3 min")
            .arg(*animationProgress * 100.0, 0, 'f', 0)
            .arg(remainingDistance / 1000.0, 0, 'f', 2)
            .arg(remainingTime / 60.0, 0, 'f', 1));
        if (*animationProgress >= 1.0)
        {
            animationTimer->stop();
            pauseButton->setEnabled(false);
            animationStatus->setText(QStringLiteral("Destination reached."));
        }
    });
    QObject::connect(playButton, &QPushButton::clicked, &window,
        [animationTimer, playButton, pauseButton, animationProgress]
    {
        if (*animationProgress >= 1.0)
            *animationProgress = 0.0;
        animationTimer->start();
        playButton->setEnabled(false);
        pauseButton->setEnabled(true);
    });
    QObject::connect(pauseButton, &QPushButton::clicked, &window,
        [animationTimer, playButton, pauseButton]
    {
        animationTimer->stop();
        playButton->setEnabled(true);
        pauseButton->setEnabled(false);
    });
    QObject::connect(resetAnimationButton, &QPushButton::clicked, &window,
        [animationTimer, markerOverlay, animationStatus, playButton, pauseButton,
         animationProgress, routeDistance, routeTime]
    {
        animationTimer->stop();
        *animationProgress = 0.0;
        markerOverlay->setVehicleProgress(0.0);
        animationStatus->setText(QStringLiteral("Ready: %1 km • %2 min")
            .arg(*routeDistance / 1000.0, 0, 'f', 2)
            .arg(*routeTime / 60.0, 0, 'f', 1));
        playButton->setEnabled(true);
        pauseButton->setEnabled(false);
    });

    QObject::connect(routeButton, &QPushButton::clicked, &window,
        [viewer, markerOverlay, routeList, routeSummary, animationStatus,
         animationTimer, playButton, pauseButton, resetAnimationButton,
         animationProgress, startNodeId, &window, startPoint, endPoint]
    {
        animationTimer->stop();
        *animationProgress = 0.0;
        playButton->setEnabled(false);
        pauseButton->setEnabled(false);
        resetAnimationButton->setEnabled(false);
        animationStatus->setText(QStringLiteral("Animation is waiting for a route."));
        markerOverlay->clearRoute();
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
        [viewer, markerOverlay, routeList, routeSummary, animationStatus,
         animationTimer, playButton, pauseButton, resetAnimationButton,
         animationProgress, routeDistance, routeTime, animationDurationMs,
         &window, routeButton, roadLayer, startPoint, endPoint,
         startNodeId, wgs84ToWebMercator, webMercatorToWgs84, mainRoadComponent]
        (GisViewerTool tool, const QPointF&, const GisShapePoint& worldPoint, Qt::KeyboardModifiers)
    {
        if (tool != GisViewerTool::Route || *roadLayer == nullptr)
            return;

        const RoutingGraph* routingGraph = viewer->routingGraph();
        const GisShapePoint sourcePoint = webMercatorToWgs84->transform(worldPoint);
        const QSet<int> candidates = routingGraph != nullptr && startPoint->has_value() && !endPoint->has_value()
            ? reachableNodes(*routingGraph, *startNodeId)
            : *mainRoadComponent;
        const RoutingNearestNodeResult snapped = routingGraph != nullptr
            ? nearestNodeInComponent(*routingGraph, candidates,
                RoutingPoint{ sourcePoint.x(), sourcePoint.y() }, 2000.0)
            : RoutingNearestNodeResult{};
        if (!snapped.found())
        {
            QMessageBox::warning(&window, QStringLiteral("RouteAnimation"),
                QStringLiteral("No road node was found near the selected point."));
            return;
        }
        const GisShapePoint snappedWorldPoint = wgs84ToWebMercator->transform(
            GisShapePoint(snapped.position.x, snapped.position.y));

        if (!startPoint->has_value() || endPoint->has_value())
        {
            animationTimer->stop();
            *animationProgress = 0.0;
            playButton->setEnabled(false);
            pauseButton->setEnabled(false);
            resetAnimationButton->setEnabled(false);
            animationStatus->setText(QStringLiteral("Animation is waiting for a route."));
            markerOverlay->clearRoute();
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

        GisViewerRouteResult result = viewer->shortestRouteBetweenPoints(
            startPoint->value(), endPoint->value(), RoutingCostMetric::Distance,
            2000.0, 50.0);
        if (!result.succeeded())
        {
            QMessageBox::warning(&window, QStringLiteral("RouteAnimation"), result.errorMessage);
            window.statusBar()->showMessage(QStringLiteral("No connected route found. Click once to choose a new start."));
            return;
        }

        markerOverlay->setRouteGeometry(result.worldGeometry);
        animationTimer->stop();
        *animationProgress = 0.0;
        *routeDistance = result.route.totalDistance;
        *routeTime = result.route.totalTime;
        *animationDurationMs = qBound(5000.0, result.route.totalTime / 60.0 * 1000.0, 45000.0);
        markerOverlay->setVehicleProgress(0.0);
        animationStatus->setText(QStringLiteral("Ready: %1 km • %2 min\nAnimation speed adapts to route length.")
            .arg(*routeDistance / 1000.0, 0, 'f', 2)
            .arg(*routeTime / 60.0, 0, 'f', 1));
        playButton->setEnabled(true);
        pauseButton->setEnabled(false);
        resetAnimationButton->setEnabled(true);
        routeList->clear();
        routeSummary->setText(
            QStringLiteral("%1 km  •  %2 min")
                .arg(result.route.totalDistance / 1000.0, 0, 'f', 2)
                .arg(result.route.totalTime / 60.0, 0, 'f', 1));

        struct RoadStep
        {
            QString name;
            double distance = 0.0;
        };
        QVector<RoadStep> steps;
        const RoutingGraph* graph = viewer->routingGraph();
        if (graph != nullptr)
        {
            for (qsizetype index = 1; index < result.route.nodeIds.size(); ++index)
            {
                const RoutingEdge* edge = graph->findEdge(
                    result.route.nodeIds[index - 1], result.route.nodeIds[index]);
                if (edge == nullptr)
                    continue;

                QString roadName = edge->attributes.value(QStringLiteral("name")).toString().trimmed();
                if (roadName.isEmpty())
                    roadName = QStringLiteral("Unnamed road");

                if (!steps.isEmpty() && steps.last().name.compare(roadName, Qt::CaseInsensitive) == 0)
                    steps.last().distance += edge->distance;
                else
                    steps.append(RoadStep{ roadName, edge->distance });
            }
        }

        for (qsizetype index = 0; index < steps.size(); ++index)
        {
            const RoadStep& step = steps[index];
            const QString distanceText = step.distance >= 1000.0
                ? QStringLiteral("%1 km").arg(step.distance / 1000.0, 0, 'f', 1)
                : QStringLiteral("%1 m").arg(step.distance, 0, 'f', 0);
            routeList->addItem(QStringLiteral("%1. %2\n    %3")
                .arg(index + 1)
                .arg(step.name, distanceText));
        }
        if (routeList->count() == 0)
            routeList->addItem(QStringLiteral("Route has no named road segments."));

        window.statusBar()->showMessage(
            QStringLiteral("Route: %1 km, %2 min | start snap %3 m, end snap %4 m")
                .arg(result.route.totalDistance / 1000.0, 0, 'f', 2)
                .arg(result.route.totalTime / 60.0, 0, 'f', 1)
                .arg(result.fromNode.distance, 0, 'f', 1)
                .arg(result.toNode.distance, 0, 'f', 1));
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
            QMessageBox::critical(&window, QStringLiteral("RouteAnimation"),
                QStringLiteral("Routing graph could not be built."));
            return;
        }
        if (const RoutingGraph* graph = viewer->routingGraph())
            *mainRoadComponent = largestConnectedComponent(*graph);
        viewer->setViewExtent(*stockholmExtent);
        viewer->setActiveTool(GisViewerTool::Route);
        routeButton->setEnabled(true);
        window.statusBar()->showMessage(QStringLiteral("Click the map to choose the start point."));
    });

    return app.exec();
}
