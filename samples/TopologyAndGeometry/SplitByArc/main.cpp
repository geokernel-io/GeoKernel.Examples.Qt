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

GisShapePolygon sourcePolygon()
{
    return polygonFromRing({
        GisShapePoint(-4.0, -2.0),
        GisShapePoint(3.8, -2.0),
        GisShapePoint(4.5, 0.5),
        GisShapePoint(2.5, 2.4),
        GisShapePoint(-1.5, 2.1),
        GisShapePoint(-4.4, 0.6),
        GisShapePoint(-4.0, -2.0)
    });
}

GisShapePolyline splitArc()
{
    GisShapePolyline arc;
    arc.parts().append({
        GisShapePoint(-5.2, 1.4),
        GisShapePoint(-1.8, 0.7),
        GisShapePoint(0.2, -0.2),
        GisShapePoint(2.0, -0.6),
        GisShapePoint(5.1, -1.0)
    });
    return arc;
}

GisLayerStyle polygonStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#BFD7EA"));
    style.setFillOpacity(115);
    style.setLineColor(QStringLiteral("#2F80C2"));
    style.setLineWidth(2.2f);
    return style;
}

GisLayerStyle arcStyle()
{
    GisLayerStyle style;
    style.setLineColor(QStringLiteral("#2D3436"));
    style.setLineWidth(2.8f);
    return style;
}

GisLayerStyle resultStyle(int index)
{
    static const QVector<QString> fills = {
        QStringLiteral("#F9C74F"),
        QStringLiteral("#A7D8F0"),
        QStringLiteral("#CDE7D8")
    };

    GisLayerStyle style;
    style.setFillColor(fills[index % fills.size()]);
    style.setFillOpacity(155);
    style.setLineColor(QStringLiteral("#D95D39"));
    style.setLineWidth(2.8f);
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
    return 0;
}

void renderScene(
    GisViewer& viewer,
    QTextEdit& detailsView,
    QStatusBar& statusBar,
    bool showResult)
{
    viewer.clearShapes();

    const GisShapePolygon polygon = sourcePolygon();
    const GisShapePolyline arc = splitArc();

    viewer.addOwnedShape(polygon.clone(), polygonStyle());
    viewer.addOwnedShape(arc.clone(), arcStyle());

    QStringList details;
    details << QStringLiteral("SplitByArc(polygon, line)");
    details << QStringLiteral("Source polygon parts: %1").arg(polygon.parts().size());
    details << QStringLiteral("Split arc parts: %1").arg(arc.parts().size());
    details << QStringLiteral("Polygon extent: %1").arg(extentText(polygon.extent()));
    details << QStringLiteral("Arc extent: %1").arg(extentText(arc.extent()));

    if (!showResult)
    {
        details << QStringLiteral("Result: click Run SplitByArc to calculate");
        statusBar.showMessage(QStringLiteral("Source polygon and split arc are ready. Click Run SplitByArc."));
    }
    else
    {
        GisTopology topology;
        GisTopology::OwnedShapeList pieces = topology.SplitByArc(polygon, arc);
        details << QStringLiteral("Result shapes: %1").arg(static_cast<int>(pieces.size()));

        int index = 0;
        for (std::unique_ptr<GisShape>& piece : pieces)
        {
            if (!piece || piece->isEmpty())
                continue;

            details << QStringLiteral("Piece %1 parts: %2 extent: %3")
                .arg(index + 1)
                .arg(partCount(*piece))
                .arg(extentText(piece->extent()));

            viewer.addOwnedShape(std::move(piece), resultStyle(index));
            ++index;
        }

        viewer.addOwnedShape(arc.clone(), arcStyle());
        statusBar.showMessage(QStringLiteral("SplitByArc result created."));
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
    window.setWindowTitle(QStringLiteral("SplitByArc"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Operation: SplitByArc(polygon, line)"), toolbar));
    toolbar->addSeparator();
    QAction* runSplitAction = toolbar->addAction(QStringLiteral("Run SplitByArc"));

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
        viewer->setViewExtent(GisExtent(-5.7, -3.0, 5.7, 3.2));
    });

    QObject::connect(runSplitAction, &QAction::triggered, &window, [viewer, detailsDock, &window]
    {
        renderScene(*viewer, *detailsDock, *window.statusBar(), true);
    });

    renderScene(*viewer, *detailsDock, *window.statusBar(), false);

    window.show();
    viewer->setViewExtent(GisExtent(-5.7, -3.0, 5.7, 3.2));

    return app.exec();
}
