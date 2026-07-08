#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QSplitter>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolBar>

#include <memory>

#include "Layers/GisLayerStyle.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShapePoint.h"
#include "Shapes/GisShapePolygon.h"
#include "Topology/GisTopology.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Core::Topology;
using namespace GeoKernel::Viewer;

GisShapePolygon polygonFromRing(std::initializer_list<GisShapePoint> points)
{
    GisShapePolygon polygon;
    QVector<GisShapePoint> ring;
    for (const GisShapePoint& point : points)
        ring.append(point);
    polygon.parts().append(std::move(ring));
    return polygon;
}

GisShapePolygon leftShape()
{
    return polygonFromRing({
        GisShapePoint(-4.5, -1.6),
        GisShapePoint(-3.1, 1.8),
        GisShapePoint(-1.9, -0.5),
        GisShapePoint(-0.5, 1.4),
        GisShapePoint(-0.1, -1.8),
        GisShapePoint(-2.1, -0.9),
        GisShapePoint(-3.5, -2.0),
        GisShapePoint(-4.5, -1.6)
    });
}

GisShapePolygon rightShape()
{
    return polygonFromRing({
        GisShapePoint(0.9, -1.4),
        GisShapePoint(2.3, -2.0),
        GisShapePoint(4.2, -0.2),
        GisShapePoint(3.4, 2.3),
        GisShapePoint(1.6, 1.3),
        GisShapePoint(0.4, 2.7),
        GisShapePoint(0.9, -1.4)
    });
}

GisLayerStyle leftStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#BFD7EA"));
    style.setFillOpacity(110);
    style.setLineColor(QStringLiteral("#2F80C2"));
    style.setLineWidth(2.2f);
    return style;
}

GisLayerStyle rightStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#CDE7D8"));
    style.setFillOpacity(110);
    style.setLineColor(QStringLiteral("#2D6A4F"));
    style.setLineWidth(2.2f);
    return style;
}

GisLayerStyle hullStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#F9C74F"));
    style.setFillOpacity(105);
    style.setLineColor(QStringLiteral("#D95D39"));
    style.setLineWidth(3.0f);
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

int vertexCount(const GisShapePolygon& polygon)
{
    int count = 0;
    for (const QVector<GisShapePoint>& part : polygon.parts())
        count += part.size();
    return count;
}

void renderScene(
    GisViewer& viewer,
    QTextEdit& detailsView,
    QStatusBar& statusBar,
    bool showHull)
{
    viewer.clearShapes();

    const GisShapePolygon left = leftShape();
    const GisShapePolygon right = rightShape();

    viewer.addOwnedShape(left.clone(), leftStyle());
    viewer.addOwnedShape(right.clone(), rightStyle());

    QStringList details;
    details << QStringLiteral("ConvexHull(left, right)");
    details << QStringLiteral("Source geometry count: 2");
    details << QStringLiteral("Left vertices: %1").arg(vertexCount(left));
    details << QStringLiteral("Right vertices: %1").arg(vertexCount(right));
    details << QStringLiteral("Left extent: %1").arg(extentText(left.extent()));
    details << QStringLiteral("Right extent: %1").arg(extentText(right.extent()));

    if (!showHull)
    {
        details << QStringLiteral("Result: click Run Convex Hull to calculate");
        statusBar.showMessage(QStringLiteral("Two source geometries are ready. Click Run Convex Hull."));
    }
    else
    {
        GisTopology topology;
        std::unique_ptr<GisShapePolygon> hull = topology.ConvexHull(left, right);
        if (hull && !hull->isEmpty())
        {
            details << QStringLiteral("Hull parts: %1").arg(hull->parts().size());
            details << QStringLiteral("Hull vertices: %1").arg(hull->parts().isEmpty() ? 0 : hull->parts().first().size());
            details << QStringLiteral("Hull extent: %1").arg(extentText(hull->extent()));
            viewer.addOwnedShape(std::move(hull), hullStyle());
            statusBar.showMessage(QStringLiteral("Convex hull result created."));
        }
        else
        {
            details << QStringLiteral("Result: empty");
            statusBar.showMessage(QStringLiteral("Convex hull returned an empty result."));
        }
    }

    viewer.invalidateRenderCache(true, true);
    viewer.update();
    detailsView.setPlainText(details.join(QStringLiteral("\n")));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(980, 680);
    window.setWindowTitle(QStringLiteral("ConvexHull Two"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Operation: ConvexHull(left, right)"), toolbar));
    toolbar->addSeparator();
    QAction* runHullAction = toolbar->addAction(QStringLiteral("Run Convex Hull"));

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

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-5.4, -3.1, 5.3, 3.5));
    });

    QObject::connect(runHullAction, &QAction::triggered, &window, [viewer, detailsDock, &window]
    {
        renderScene(*viewer, *detailsDock, *window.statusBar(), true);
    });

    renderScene(*viewer, *detailsDock, *window.statusBar(), false);

    window.show();
    viewer->setViewExtent(GisExtent(-5.4, -3.1, 5.3, 3.5));

    return app.exec();
}
