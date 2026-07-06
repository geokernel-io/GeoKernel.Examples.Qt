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

GisShapePolygon polygonFromRing(std::initializer_list<GisShapePoint> points)
{
    GisShapePolygon polygon;
    QVector<GisShapePoint> ring;
    for (const GisShapePoint& point : points)
        ring.append(point);
    polygon.parts().append(std::move(ring));
    return polygon;
}

GisShapePolygon sourceShape()
{
    return polygonFromRing({
        GisShapePoint(-4.4, -1.6),
        GisShapePoint(-3.4, 1.6),
        GisShapePoint(-1.9, -0.7),
        GisShapePoint(-0.4, 2.3),
        GisShapePoint(0.8, -1.2),
        GisShapePoint(2.0, 1.7),
        GisShapePoint(3.9, -0.5),
        GisShapePoint(2.5, -2.1),
        GisShapePoint(0.4, -0.2),
        GisShapePoint(-1.2, -2.0),
        GisShapePoint(-2.7, 0.0),
        GisShapePoint(-4.4, -1.6)
    });
}

GisLayerStyle sourceStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#BFD7EA"));
    style.setFillOpacity(100);
    style.setLineColor(QStringLiteral("#1F6F9F"));
    style.setLineWidth(2.4f);
    return style;
}

GisLayerStyle hullStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#F9C74F"));
    style.setFillOpacity(115);
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

    const GisShapePolygon source = sourceShape();
    viewer.addOwnedShape(source.clone(), sourceStyle());

    QStringList details;
    details << QStringLiteral("ConvexHull(shape)");
    details << QStringLiteral("Source type: polygon");
    details << QStringLiteral("Source geometry count: 1");
    details << QStringLiteral("Source vertices: %1").arg(vertexCount(source));
    details << QStringLiteral("Source extent: %1").arg(extentText(source.extent()));

    if (!showHull)
    {
        details << QStringLiteral("Result: click Run Convex Hull to calculate");
        statusBar.showMessage(QStringLiteral("Source geometry is ready. Click Run Convex Hull."));
    }
    else
    {
        GisTopology topology;
        std::unique_ptr<GisShapePolygon> hull = topology.ConvexHull(source);
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
    window.setWindowTitle(QStringLiteral("ConvexHull Shape"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Operation: ConvexHull(shape)"), toolbar));
    toolbar->addSeparator();
    QAction* runHullAction = toolbar->addAction(QStringLiteral("Run Convex Hull"));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(247, 248, 250));

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
        viewer->setViewExtent(GisExtent(-5.3, -3.1, 5.2, 3.4));
    });

    QObject::connect(runHullAction, &QAction::triggered, &window, [viewer, detailsDock, &window]
    {
        renderScene(*viewer, *detailsDock, *window.statusBar(), true);
    });

    renderScene(*viewer, *detailsDock, *window.statusBar(), false);

    window.show();
    viewer->setViewExtent(GisExtent(-5.3, -3.1, 5.2, 3.4));

    return app.exec();
}
