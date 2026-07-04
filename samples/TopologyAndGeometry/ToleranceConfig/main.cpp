#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolBar>

#include <cmath>
#include <memory>

#include "Layers/GisLayerStyle.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShapePoint.h"
#include "Shapes/GisShapePolygon.h"
#include "Shapes/GisShapePolyline.h"
#include "Topology/GisTopology.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Core::Topology;
using namespace GeoKernel::Viewer;

GisShapePolyline baseline()
{
    GisShapePolyline line;
    line.parts().append({
        GisShapePoint(-4.5, 0.0),
        GisShapePoint(4.5, 0.0)
    });
    line.attributes().insert(QStringLiteral("LABEL"), QStringLiteral("baseline"));
    return line;
}

GisShapePoint testPoint()
{
    GisShapePoint point(0.0, 0.35);
    point.attributes().insert(QStringLiteral("LABEL"), QStringLiteral("test point"));
    return point;
}

GisShapePolygon toleranceCircle(const GisShapePoint& center, double radius)
{
    GisShapePolygon polygon;

    if (radius <= 0.0)
        return polygon;

    QVector<GisShapePoint> ring;
    constexpr int segments = 72;
    ring.reserve(segments + 1);

    for (int i = 0; i <= segments; ++i)
    {
        const double angle = 2.0 * 3.14159265358979323846 * i / segments;
        ring.append(GisShapePoint(
            center.x() + std::cos(angle) * radius,
            center.y() + std::sin(angle) * radius));
    }

    polygon.parts().append(std::move(ring));
    polygon.attributes().insert(QStringLiteral("LABEL"), QStringLiteral("topology tolerance"));
    return polygon;
}

std::unique_ptr<GisShapePoint> labeledPoint(const GisShapePoint& source, const QString& label)
{
    auto point = std::make_unique<GisShapePoint>(source.x(), source.y());
    point->attributes().insert(QStringLiteral("LABEL"), label);
    return point;
}

GisLayerStyle lineStyle()
{
    GisLayerStyle style;
    style.setLineColor(QStringLiteral("#1F6F8B"));
    style.setLineWidth(3.0f);
    style.setPointColor(QStringLiteral("#1F6F8B"));
    style.setPointSize(7.0f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("LABEL"));
    style.setLabelFontSize(11.0f);
    style.setLabelColor(QStringLiteral("#17324D"));
    style.setLabelHaloEnabled(true);
    style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
    style.setLabelHaloWidth(2.0f);
    return style;
}

GisLayerStyle toleranceStyle(bool active)
{
    GisLayerStyle style;
    style.setFillColor(active ? QStringLiteral("#CDE7D8") : QStringLiteral("#F6D6AD"));
    style.setFillOpacity(75);
    style.setLineColor(active ? QStringLiteral("#2A9D8F") : QStringLiteral("#D95D39"));
    style.setLineWidth(2.0f);
    style.setShowLabels(false);
    return style;
}

GisLayerStyle pointStyle(bool active)
{
    GisLayerStyle style;
    style.setPointColor(active ? QStringLiteral("#2A9D8F") : QStringLiteral("#C1121F"));
    style.setLineColor(active ? QStringLiteral("#145A4B") : QStringLiteral("#7A0010"));
    style.setLineWidth(1.3f);
    style.setPointSize(12.0f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("LABEL"));
    style.setLabelFontSize(11.0f);
    style.setLabelColor(active ? QStringLiteral("#145A4B") : QStringLiteral("#7A0010"));
    style.setLabelHaloEnabled(true);
    style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
    style.setLabelHaloWidth(2.5f);
    return style;
}

GisLayerStyle crossingStyle()
{
    GisLayerStyle style = pointStyle(true);
    style.setPointColor(QStringLiteral("#FFD166"));
    style.setLineColor(QStringLiteral("#9A6700"));
    style.setPointSize(15.0f);
    style.setLabelColor(QStringLiteral("#5C3A00"));
    return style;
}

QString boolText(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

void renderScene(
    GisViewer& viewer,
    QTextEdit& detailsView,
    QStatusBar& statusBar,
    double tolerance)
{
    viewer.clearShapes();

    const GisShapePolyline line = baseline();
    const GisShapePoint point = testPoint();

    GisTopology topology;
    topology.SetTolerance(tolerance);

    const QVector<GisShapePoint> crossings = topology.GetCrossings(line, point);
    const bool intersects = topology.Intersect(line, point);
    const bool active = !crossings.isEmpty() || intersects;

    viewer.addOwnedShape(toleranceCircle(point, tolerance).clone(), toleranceStyle(active));
    viewer.addOwnedShape(line.clone(), lineStyle());
    viewer.addOwnedShape(point.clone(), pointStyle(active));

    for (int i = 0; i < crossings.size(); ++i)
    {
        viewer.addOwnedShape(
            labeledPoint(crossings[i], QStringLiteral("accepted by tolerance")),
            crossingStyle());
    }

    QStringList details;
    details << QStringLiteral("GisTopology::SetTolerance");
    details << QStringLiteral("");
    details << QStringLiteral("Scenario:");
    details << QStringLiteral("- Baseline is y = 0.");
    details << QStringLiteral("- Test point is at (0.00, 0.35).");
    details << QStringLiteral("- Point-to-line distance is 0.35 map units.");
    details << QStringLiteral("");
    details << QStringLiteral("Configured tolerance: %1").arg(topology.Tolerance(), 0, 'f', 2);
    details << QStringLiteral("GetCrossings(line, point): %1 point(s)").arg(crossings.size());
    details << QStringLiteral("Intersect(line, point): %1").arg(boolText(intersects));
    details << QStringLiteral("");

    if (active)
    {
        details << QStringLiteral("Result:");
        details << QStringLiteral("The point is accepted as touching/intersecting the line within tolerance.");
    }
    else
    {
        details << QStringLiteral("Result:");
        details << QStringLiteral("The point is outside the configured tolerance.");
    }

    details << QStringLiteral("");
    details << QStringLiteral("Visual guide:");
    details << QStringLiteral("Circle: current tolerance radius around the point");
    details << QStringLiteral("Green: tolerance reaches the line");
    details << QStringLiteral("Orange/red: tolerance is too small");

    viewer.invalidateRenderCache(true, true);
    viewer.update();
    detailsView.setPlainText(details.join(QStringLiteral("\n")));
    statusBar.showMessage(QStringLiteral("Topology tolerance: %1 map units.").arg(tolerance, 0, 'f', 2));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1040, 680);
    window.setWindowTitle(QStringLiteral("ToleranceConfig"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Tolerance:"), toolbar));

    auto* toleranceSlider = new QSlider(Qt::Horizontal, toolbar);
    toleranceSlider->setRange(0, 100);
    toleranceSlider->setSingleStep(1);
    toleranceSlider->setPageStep(10);
    toleranceSlider->setFixedWidth(180);
    toleranceSlider->setValue(25);
    toolbar->addWidget(toleranceSlider);

    auto* toleranceValueLabel = new QLabel(QStringLiteral("0.25 units"), toolbar);
    toleranceValueLabel->setMinimumWidth(70);
    toolbar->addWidget(toleranceValueLabel);

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(247, 248, 250));

    auto* detailsDock = new QTextEdit(&window);
    detailsDock->setReadOnly(true);
    detailsDock->setMinimumWidth(350);

    auto* splitter = new QSplitter(Qt::Horizontal, &window);
    splitter->addWidget(viewer);
    splitter->addWidget(detailsDock);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({ 690, 350 });
    window.setCentralWidget(splitter);

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-5.2, -1.8, 5.2, 2.4));
    });

    QObject::connect(toleranceSlider, &QSlider::valueChanged, &window, [viewer, detailsDock, toleranceSlider, toleranceValueLabel, &window]
    {
        const double tolerance = toleranceSlider->value() / 100.0;
        toleranceValueLabel->setText(QStringLiteral("%1 units").arg(tolerance, 0, 'f', 2));
        renderScene(*viewer, *detailsDock, *window.statusBar(), tolerance);
    });

    renderScene(*viewer, *detailsDock, *window.statusBar(), toleranceSlider->value() / 100.0);

    window.show();
    viewer->setViewExtent(GisExtent(-5.2, -1.8, 5.2, 2.4));

    return app.exec();
}
