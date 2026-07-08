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

QVector<GisShapePolygon> sourcePolygons()
{
    return {
        polygonFromRing({
            GisShapePoint(-4.8, -1.4),
            GisShapePoint(-0.8, -1.4),
            GisShapePoint(-0.8, 1.8),
            GisShapePoint(-4.8, 1.8),
            GisShapePoint(-4.8, -1.4)
        }),
        polygonFromRing({
            GisShapePoint(-2.6, -2.3),
            GisShapePoint(1.2, -2.3),
            GisShapePoint(1.2, 0.6),
            GisShapePoint(-2.6, 0.6),
            GisShapePoint(-2.6, -2.3)
        }),
        polygonFromRing({
            GisShapePoint(0.3, 2.8),
            GisShapePoint(0.9, 1.1),
            GisShapePoint(2.8, 1.1),
            GisShapePoint(1.3, 0.1),
            GisShapePoint(2.1, -1.6),
            GisShapePoint(0.3, -0.6),
            GisShapePoint(-1.5, -1.6),
            GisShapePoint(-0.7, 0.1),
            GisShapePoint(-2.2, 1.1),
            GisShapePoint(-0.3, 1.1),
            GisShapePoint(0.3, 2.8)
        }),
        polygonFromRing({
            GisShapePoint(1.5, -0.2),
            GisShapePoint(4.6, -0.2),
            GisShapePoint(4.6, 2.0),
            GisShapePoint(1.5, 2.0),
            GisShapePoint(1.5, -0.2)
        }),
        polygonFromRing({
            GisShapePoint(2.0, -2.4),
            GisShapePoint(4.8, -1.2),
            GisShapePoint(3.3, 0.7),
            GisShapePoint(2.0, -2.4)
        })
    };
}

GisLayerStyle sourceStyle(int index)
{
    static const QVector<QString> fills = {
        QStringLiteral("#BFD7EA"),
        QStringLiteral("#D8EAC4"),
        QStringLiteral("#F3D6A3"),
        QStringLiteral("#D9C8F0"),
        QStringLiteral("#BFE3D9")
    };
    static const QVector<QString> lines = {
        QStringLiteral("#2F80C2"),
        QStringLiteral("#5B8E3E"),
        QStringLiteral("#B7791F"),
        QStringLiteral("#7048A8"),
        QStringLiteral("#2D6A4F")
    };

    GisLayerStyle style;
    style.setFillColor(fills[index % fills.size()]);
    style.setFillOpacity(110);
    style.setLineColor(lines[index % lines.size()]);
    style.setLineWidth(2.0f);
    return style;
}

GisLayerStyle resultStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#F9C74F"));
    style.setFillOpacity(135);
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
    if (shape.shapeType() == GisShapeType::Point || shape.shapeType() == GisShapeType::MultiPoint)
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

    const QVector<GisShapePolygon> polygons = sourcePolygons();
    for (int i = 0; i < polygons.size(); ++i)
        viewer.addOwnedShape(polygons[i].clone(), sourceStyle(i));

    QStringList details;
    details << QStringLiteral("UnionOnList(shapes)");
    details << QStringLiteral("Source polygons: %1").arg(polygons.size());
    for (int i = 0; i < polygons.size(); ++i)
        details << QStringLiteral("Source %1 extent: %2").arg(i + 1).arg(extentText(polygons[i].extent()));

    if (!showResult)
    {
        details << QStringLiteral("Result: click Run UnionOnList to calculate");
        statusBar.showMessage(QStringLiteral("Source polygons are ready. Click Run UnionOnList."));
    }
    else
    {
        GisTopology::ShapeList shapeList;
        shapeList.reserve(polygons.size());
        for (const GisShapePolygon& polygon : polygons)
            shapeList.append(&polygon);

        GisTopology topology;
        std::unique_ptr<GisShape> result = topology.UnionOnList(shapeList);
        if (result && !result->isEmpty())
        {
            details << QStringLiteral("Result type: %1").arg(shapeTypeText(*result));
            details << QStringLiteral("Result parts: %1").arg(partCount(*result));
            details << QStringLiteral("Result extent: %1").arg(extentText(result->extent()));
            viewer.addOwnedShape(std::move(result), resultStyle());
            statusBar.showMessage(QStringLiteral("UnionOnList result created."));
        }
        else
        {
            details << QStringLiteral("Result: empty");
            statusBar.showMessage(QStringLiteral("UnionOnList returned an empty result."));
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
    window.setWindowTitle(QStringLiteral("UnionOnList"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Operation: UnionOnList(shapes)"), toolbar));
    toolbar->addSeparator();
    QAction* runUnionAction = toolbar->addAction(QStringLiteral("Run UnionOnList"));

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
        viewer->setViewExtent(GisExtent(-5.8, -3.3, 5.8, 4.0));
    });

    QObject::connect(runUnionAction, &QAction::triggered, &window, [viewer, detailsDock, &window]
    {
        renderScene(*viewer, *detailsDock, *window.statusBar(), true);
    });

    renderScene(*viewer, *detailsDock, *window.statusBar(), false);

    window.show();
    viewer->setViewExtent(GisExtent(-5.8, -3.3, 5.8, 4.0));

    return app.exec();
}
