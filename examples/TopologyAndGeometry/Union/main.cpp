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

GisShapePolygon polygonA()
{
    return polygonFromRing({
        GisShapePoint(-4.2, -1.7),
        GisShapePoint(0.8, -1.7),
        GisShapePoint(0.8, 2.2),
        GisShapePoint(-4.2, 2.2),
        GisShapePoint(-4.2, -1.7)
    });
}

GisShapePolygon polygonB()
{
    return polygonFromRing({
        GisShapePoint(1.0, 3.0),
        GisShapePoint(1.7, 1.2),
        GisShapePoint(3.7, 1.2),
        GisShapePoint(2.1, 0.1),
        GisShapePoint(2.8, -1.8),
        GisShapePoint(1.0, -0.7),
        GisShapePoint(-0.8, -1.8),
        GisShapePoint(-0.1, 0.1),
        GisShapePoint(-1.7, 1.2),
        GisShapePoint(0.3, 1.2),
        GisShapePoint(1.0, 3.0)
    });
}

GisLayerStyle sourceAStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#BFD7EA"));
    style.setFillOpacity(135);
    style.setLineColor(QStringLiteral("#2F80C2"));
    style.setLineWidth(2.0f);
    return style;
}

GisLayerStyle sourceBStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#CDE7D8"));
    style.setFillOpacity(135);
    style.setLineColor(QStringLiteral("#2D6A4F"));
    style.setLineWidth(2.0f);
    return style;
}

GisLayerStyle resultStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#F9C74F"));
    style.setFillOpacity(120);
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

QString shapeTypeText(const GisShape& shape)
{
    switch (shape.shapeType())
    {
    case GisShapeType::Point:
    case GisShapeType::MultiPoint:
        return QStringLiteral("point");
    case GisShapeType::Polyline:
    case GisShapeType::PolylineZ:
        return QStringLiteral("polyline");
    case GisShapeType::Polygon:
    case GisShapeType::PolygonZ:
        return QStringLiteral("polygon");
    default:
        return QStringLiteral("unknown");
    }
}

void renderScene(
    GisViewer& viewer,
    QTextEdit& detailsView,
    QStatusBar& statusBar,
    bool showResult)
{
    viewer.clearShapes();

    const GisShapePolygon left = polygonA();
    const GisShapePolygon right = polygonB();

    viewer.addOwnedShape(left.clone(), sourceAStyle());
    viewer.addOwnedShape(right.clone(), sourceBStyle());

    QStringList details;
    details << QStringLiteral("Union(left, right)");
    details << QStringLiteral("Left extent: %1").arg(extentText(left.extent()));
    details << QStringLiteral("Right extent: %1").arg(extentText(right.extent()));

    if (!showResult)
    {
        details << QStringLiteral("Result: click Run Union to calculate");
        statusBar.showMessage(QStringLiteral("Source polygons are ready. Click Run Union."));
    }
    else
    {
        GisTopology topology;
        std::unique_ptr<GisShape> result = topology.Union(left, right);
        if (result && !result->isEmpty())
        {
            details << QStringLiteral("Result type: %1").arg(shapeTypeText(*result));
            details << QStringLiteral("Result parts: %1").arg(partCount(*result));
            details << QStringLiteral("Result extent: %1").arg(extentText(result->extent()));
            viewer.addOwnedShape(std::move(result), resultStyle());
            statusBar.showMessage(QStringLiteral("Union result created."));
        }
        else
        {
            details << QStringLiteral("Result: empty");
            statusBar.showMessage(QStringLiteral("Union returned an empty result."));
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
    window.setWindowTitle(QStringLiteral("Union"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Operation: Union(left, right)"), toolbar));
    toolbar->addSeparator();
    QAction* runUnionAction = toolbar->addAction(QStringLiteral("Run Union"));

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
        viewer->setViewExtent(GisExtent(-5.2, -3.2, 5.2, 4.0));
    });

    QObject::connect(runUnionAction, &QAction::triggered, &window, [viewer, detailsDock, &window]
    {
        renderScene(*viewer, *detailsDock, *window.statusBar(), true);
    });

    renderScene(*viewer, *detailsDock, *window.statusBar(), false);

    window.show();
    viewer->setViewExtent(GisExtent(-5.2, -3.2, 5.2, 4.0));

    return app.exec();
}
