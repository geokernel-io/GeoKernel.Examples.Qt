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
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <optional>

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
            update();
        }

        void clearRoute()
        {
            m_route.clear();
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
        }

    private:
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
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ShortestRoute"));

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("ShortestRoute"));
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
    auto* routeList = new QListWidget(directionsPanel);
    directionsLayout->addWidget(directionsTitle);
    directionsLayout->addWidget(routeSummary);
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
    auto stockholmExtent = std::make_shared<GisExtent>(GisExtent::empty());

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

    QObject::connect(routeButton, &QPushButton::clicked, &window,
        [viewer, markerOverlay, routeList, routeSummary, &window, startPoint, endPoint]
    {
        markerOverlay->clearRoute();
        routeList->clear();
        routeSummary->setText(QStringLiteral("Select a start and finish point."));
        startPoint->reset();
        endPoint->reset();
        markerOverlay->setRouteMarkers(*startPoint, *endPoint);
        viewer->setActiveTool(GisViewerTool::Route);
        window.statusBar()->showMessage(QStringLiteral("Click the map to choose the start point."));
    });

    QObject::connect(viewer, &GisViewer::mapClicked, &window,
        [viewer, markerOverlay, routeList, routeSummary, &window, routeButton,
         roadLayer, startPoint, endPoint,
         wgs84ToWebMercator]
        (GisViewerTool, const QPointF&, const GisShapePoint& worldPoint, Qt::KeyboardModifiers)
    {
        if (*roadLayer == nullptr)
            return;

        const RoutingNearestNodeResult snapped = viewer->nearestRoutingNode(worldPoint, 2000.0);
        if (!snapped.found())
        {
            QMessageBox::warning(&window, QStringLiteral("ShortestRoute"),
                QStringLiteral("No road node was found near the selected point."));
            return;
        }
        const GisShapePoint snappedWorldPoint = wgs84ToWebMercator->transform(
            GisShapePoint(snapped.position.x, snapped.position.y));

        if (!startPoint->has_value() || endPoint->has_value())
        {
            markerOverlay->clearRoute();
            routeList->clear();
            routeSummary->setText(QStringLiteral("Select the finish point."));
            *startPoint = snappedWorldPoint;
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
            QMessageBox::warning(&window, QStringLiteral("ShortestRoute"), result.errorMessage);
            window.statusBar()->showMessage(QStringLiteral("No connected route found. Click once to choose a new start."));
            return;
        }

        markerOverlay->setRouteGeometry(result.worldGeometry);
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
         wgs84, webMercator, wgs84ToWebMercator]
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
            QMessageBox::critical(&window, QStringLiteral("ShortestRoute"),
                QStringLiteral("Routing graph could not be built."));
            return;
        }
        viewer->setViewExtent(*stockholmExtent);
        viewer->setActiveTool(GisViewerTool::Route);
        routeButton->setEnabled(true);
        window.statusBar()->showMessage(QStringLiteral("Click the map to choose the start point."));
    });

    return app.exec();
}
