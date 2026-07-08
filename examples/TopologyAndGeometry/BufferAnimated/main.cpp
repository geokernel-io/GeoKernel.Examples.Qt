#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolBar>

#include <memory>

#include "Layers/GisLayerStyle.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShape.h"
#include "Shapes/GisShapePoint.h"
#include "Shapes/GisShapePolygon.h"
#include "Shapes/GisShapePolyline.h"
#include "Topology/GisTopology.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Core::Topology;
using namespace GeoKernel::Viewer;

constexpr double MinDistance = 0.35;
constexpr double MaxDistance = 3.00;
constexpr double DistanceStep = 0.08;

GisLayerStyle bufferStyle(double distance)
{
    const double t = (distance - MinDistance) / (MaxDistance - MinDistance);
    const int opacity = 55 + static_cast<int>(t * 90.0);

    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#78B7D0"));
    style.setFillOpacity(opacity);
    style.setLineColor(QStringLiteral("#1E6F8C"));
    style.setLineWidth(2.2f);
    return style;
}

GisLayerStyle pulseRingStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#FFFFFF"));
    style.setFillOpacity(0);
    style.setLineColor(QStringLiteral("#D95D39"));
    style.setLineWidth(1.3f);
    return style;
}

GisLayerStyle pointStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#D95D39"));
    style.setFillOpacity(255);
    style.setLineColor(QStringLiteral("#7A2F1E"));
    style.setLineWidth(1.2f);
    style.setPointColor(QStringLiteral("#D95D39"));
    style.setPointSize(13.0f);
    return style;
}

QString extentText(const GisExtent& extent)
{
    if (extent.isEmpty())
        return QStringLiteral("(empty)");

    return QStringLiteral("(%1, %2) - (%3, %4)")
        .arg(extent.xMin(), 0, 'f', 2)
        .arg(extent.yMin(), 0, 'f', 2)
        .arg(extent.xMax(), 0, 'f', 2)
        .arg(extent.yMax(), 0, 'f', 2);
}

int partCount(const GisShape& shape)
{
    if (const auto* polygon = dynamic_cast<const GisShapePolygon*>(&shape))
        return polygon->parts().size();
    if (const auto* polyline = dynamic_cast<const GisShapePolyline*>(&shape))
        return polyline->parts().size();
    if (dynamic_cast<const GisShapePoint*>(&shape) != nullptr)
        return 1;

    return 0;
}

void renderFrame(
    GisViewer& viewer,
    QTextEdit& detailsView,
    QStatusBar& statusBar,
    QLabel& distanceLabel,
    double distance,
    int frame)
{
    viewer.clearShapes();

    const GisShapePoint sourcePoint(0.0, 0.0);
    GisTopology topology;

    std::unique_ptr<GisShape> buffer = topology.MakeBuffer(sourcePoint, distance, 18);
    if (buffer && !buffer->isEmpty())
    {
        viewer.addOwnedShape(std::move(buffer), bufferStyle(distance));
    }

    const double pulseDistance = std::max(MinDistance, distance - 0.28);
    std::unique_ptr<GisShape> pulseRing = topology.MakeBuffer(sourcePoint, pulseDistance, 18);
    if (pulseRing && !pulseRing->isEmpty())
        viewer.addOwnedShape(std::move(pulseRing), pulseRingStyle());

    viewer.addOwnedShape(std::make_unique<GisShapePoint>(sourcePoint.x(), sourcePoint.y()), pointStyle());
    viewer.invalidateRenderCache(true, true);
    viewer.update();

    distanceLabel.setText(QStringLiteral("%1 units").arg(distance, 0, 'f', 2));

    QStringList details;
    details << QStringLiteral("QTimer animated buffer");
    details << QStringLiteral("Operation: MakeBuffer(point, distance)");
    details << QStringLiteral("Frame: %1").arg(frame);
    details << QStringLiteral("Distance: %1 map units").arg(distance, 0, 'f', 2);
    details << QStringLiteral("Source point: (0.00, 0.00)");

    std::unique_ptr<GisShape> summaryBuffer = topology.MakeBuffer(sourcePoint, distance, 18);
    if (summaryBuffer)
    {
        details << QStringLiteral("Result parts: %1").arg(partCount(*summaryBuffer));
        details << QStringLiteral("Result extent: %1").arg(extentText(summaryBuffer->extent()));
    }

    detailsView.setPlainText(details.join(QStringLiteral("\n")));
    statusBar.showMessage(QStringLiteral("Animated point buffer: frame %1, distance %2")
        .arg(frame)
        .arg(distance, 0, 'f', 2));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(980, 680);
    window.setWindowTitle(QStringLiteral("Buffer Animated"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    auto* timer = new QTimer(&window);
    timer->setInterval(60);

    auto* speedSlider = new QSlider(Qt::Horizontal, toolbar);
    speedSlider->setRange(20, 200);
    speedSlider->setValue(60);
    speedSlider->setFixedWidth(160);

    auto* distanceLabel = new QLabel(toolbar);
    distanceLabel->setMinimumWidth(80);

    QAction* playPauseAction = toolbar->addAction(QStringLiteral("Pause"));
    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Interval:"), toolbar));
    toolbar->addWidget(speedSlider);
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Distance:"), toolbar));
    toolbar->addWidget(distanceLabel);

    auto* viewer = new GisViewer(&window);

    auto* detailsDock = new QTextEdit(&window);
    detailsDock->setReadOnly(true);
    detailsDock->setMinimumWidth(250);

    auto* splitter = new QSplitter(Qt::Horizontal, &window);
    splitter->addWidget(viewer);
    splitter->addWidget(detailsDock);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({ 700, 280 });
    window.setCentralWidget(splitter);

    double distance = MinDistance;
    int direction = 1;
    int frame = 0;

    QObject::connect(timer, &QTimer::timeout, &window, [&]
    {
        distance += DistanceStep * direction;
        if (distance >= MaxDistance)
        {
            distance = MaxDistance;
            direction = -1;
        }
        else if (distance <= MinDistance)
        {
            distance = MinDistance;
            direction = 1;
        }

        ++frame;
        renderFrame(*viewer, *detailsDock, *window.statusBar(), *distanceLabel, distance, frame);
    });

    QObject::connect(speedSlider, &QSlider::valueChanged, timer, [timer](int value)
    {
        timer->setInterval(value);
    });

    QObject::connect(playPauseAction, &QAction::triggered, timer, [timer, playPauseAction]
    {
        if (timer->isActive())
        {
            timer->stop();
            playPauseAction->setText(QStringLiteral("Play"));
        }
        else
        {
            timer->start();
            playPauseAction->setText(QStringLiteral("Pause"));
        }
    });

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-4.2, -3.5, 4.2, 3.5));
    });

    renderFrame(*viewer, *detailsDock, *window.statusBar(), *distanceLabel, distance, frame);

    window.show();
    viewer->setViewExtent(GisExtent(-4.2, -3.5, 4.2, 3.5));
    timer->start();

    return app.exec();
}
