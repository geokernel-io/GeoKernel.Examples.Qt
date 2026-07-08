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
#include "Shapes/GisShapePolyline.h"
#include "Topology/GisTopology.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Core::Topology;
using namespace GeoKernel::Viewer;

GisShapePolyline polylineFromPoints(std::initializer_list<GisShapePoint> points)
{
    GisShapePolyline polyline;
    QVector<GisShapePoint> part;
    for (const GisShapePoint& point : points)
        part.append(point);
    polyline.parts().append(std::move(part));
    return polyline;
}

GisLayerStyle lineStyle(const QString& color, float width)
{
    GisLayerStyle style;
    style.setFillOpacity(0);
    style.setLineColor(color);
    style.setLineWidth(width);
    style.setPointColor(color);
    style.setPointSize(width + 4.0f);
    return style;
}

GisLayerStyle sourceStyle()
{
    return lineStyle(QStringLiteral("#6C757D"), 2.0f);
}

GisLayerStyle queryStyle()
{
    return lineStyle(QStringLiteral("#2F80C2"), 3.0f);
}

GisLayerStyle resultStyle(int index = 0)
{
    static const QVector<QString> colors = {
        QStringLiteral("#D95D39"),
        QStringLiteral("#2A9D8F"),
        QStringLiteral("#7B2CBF")
    };
    return lineStyle(colors[index % colors.size()], 4.0f);
}

GisLayerStyle cutterStyle()
{
    return lineStyle(QStringLiteral("#212529"), 2.6f);
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

int vertexCount(const GisShapePolyline& polyline)
{
    int total = 0;
    for (const QVector<GisShapePoint>& part : polyline.parts())
        total += part.size();
    return total;
}

void addPolylineParts(GisViewer& viewer, const GisShapePolyline& polyline, int styleOffset = 0)
{
    for (int i = 0; i < polyline.parts().size(); ++i)
    {
        auto partShape = std::make_unique<GisShapePolyline>();
        partShape->parts().append(polyline.parts()[i]);
        viewer.addOwnedShape(std::move(partShape), resultStyle(styleOffset + i));
    }
}

void renderScene(
    GisViewer& viewer,
    QTextEdit& detailsView,
    QStatusBar& statusBar,
    bool showResults)
{
    viewer.clearShapes();

    const GisShapePolyline findQuery = polylineFromPoints({
        GisShapePoint(-5.2, 2.2),
        GisShapePoint(-3.2, 2.2)
    });
    const GisShapePolyline findCandidateA = polylineFromPoints({
        GisShapePoint(-1.8, 2.7),
        GisShapePoint(0.4, 2.7)
    });
    const GisShapePolyline findCandidateB = polylineFromPoints({
        GisShapePoint(-5.2, 2.2),
        GisShapePoint(-3.2, 2.2)
    });

    const GisShapePolyline connectBase = polylineFromPoints({
        GisShapePoint(-5.2, 0.2),
        GisShapePoint(-3.6, 0.2),
        GisShapePoint(-2.6, 0.8)
    });
    const GisShapePolyline connectContinuation = polylineFromPoints({
        GisShapePoint(-2.6, 0.8),
        GisShapePoint(-1.1, 0.1),
        GisShapePoint(0.4, 0.4)
    });

    const GisShapePolyline splitArc = polylineFromPoints({
        GisShapePoint(-5.2, -2.0),
        GisShapePoint(-1.0, -2.0)
    });
    const GisShapePolyline splitCutter = polylineFromPoints({
        GisShapePoint(-3.1, -3.0),
        GisShapePoint(-3.1, -1.0)
    });

    viewer.addOwnedShape(findCandidateA.clone(), sourceStyle());
    viewer.addOwnedShape(findCandidateB.clone(), sourceStyle());
    viewer.addOwnedShape(findQuery.clone(), queryStyle());
    viewer.addOwnedShape(connectBase.clone(), sourceStyle());
    viewer.addOwnedShape(connectContinuation.clone(), sourceStyle());
    viewer.addOwnedShape(splitArc.clone(), queryStyle());
    viewer.addOwnedShape(splitCutter.clone(), cutterStyle());

    QStringList details;
    details << QStringLiteral("ArcFind / ArcMakeConnected / ArcSplitOnCross");
    details << QStringLiteral("");
    details << QStringLiteral("1. ArcFind");
    details << QStringLiteral("Query arc extent: %1").arg(extentText(findQuery.extent()));
    details << QStringLiteral("Candidate count: 2");
    details << QStringLiteral("");
    details << QStringLiteral("2. ArcMakeConnected");
    details << QStringLiteral("Base vertices: %1").arg(vertexCount(connectBase));
    details << QStringLiteral("Continuation vertices: %1").arg(vertexCount(connectContinuation));
    details << QStringLiteral("");
    details << QStringLiteral("3. ArcSplitOnCross");
    details << QStringLiteral("Split arc vertices: %1").arg(vertexCount(splitArc));
    details << QStringLiteral("Cutter vertices: %1").arg(vertexCount(splitCutter));

    if (!showResults)
    {
        details << QStringLiteral("");
        details << QStringLiteral("Result: click Run Arc Operations to calculate");
        statusBar.showMessage(QStringLiteral("Source arcs are ready. Click Run Arc Operations."));
    }
    else
    {
        GisTopology topology;

        GisTopology::PolylineList findList{ &findCandidateA, &findCandidateB };
        int foundIndex = -1;
        const bool found = topology.ArcFind(findQuery, findList, foundIndex);
        details << QStringLiteral("");
        details << QStringLiteral("ArcFind result: %1, index: %2")
            .arg(found ? QStringLiteral("found") : QStringLiteral("not found"))
            .arg(foundIndex);
        if (found && foundIndex >= 0 && foundIndex < findList.size())
            viewer.addOwnedShape(findList[foundIndex]->clone(), resultStyle(0));

        GisTopology::PolylineList connectList{ &connectContinuation };
        std::unique_ptr<GisShapePolyline> connected = topology.ArcMakeConnected(connectBase, connectList);
        details << QStringLiteral("ArcMakeConnected result parts: %1, vertices: %2")
            .arg(connected ? connected->parts().size() : 0)
            .arg(connected ? vertexCount(*connected) : 0);
        if (connected)
            viewer.addOwnedShape(std::move(connected), resultStyle(1));

        GisTopology::PolylineList splitList{ &splitCutter };
        std::unique_ptr<GisShapePolyline> split = topology.ArcSplitOnCross(splitArc, splitList);
        details << QStringLiteral("ArcSplitOnCross result parts: %1")
            .arg(split ? split->parts().size() : 0);
        if (split)
            addPolylineParts(viewer, *split, 2);

        statusBar.showMessage(QStringLiteral("Arc operations calculated."));
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
    window.setWindowTitle(QStringLiteral("ArcOperations"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Operations: ArcFind / ArcMakeConnected / ArcSplitOnCross"), toolbar));
    toolbar->addSeparator();
    QAction* runAction = toolbar->addAction(QStringLiteral("Run Arc Operations"));

    auto* viewer = new GisViewer(&window);

    auto* detailsDock = new QTextEdit(&window);
    detailsDock->setReadOnly(true);
    detailsDock->setMinimumWidth(280);

    auto* splitter = new QSplitter(Qt::Horizontal, &window);
    splitter->addWidget(viewer);
    splitter->addWidget(detailsDock);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({ 690, 290 });
    window.setCentralWidget(splitter);

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-5.8, -3.3, 1.0, 3.2));
    });

    QObject::connect(runAction, &QAction::triggered, &window, [viewer, detailsDock, &window]
    {
        renderScene(*viewer, *detailsDock, *window.statusBar(), true);
    });

    renderScene(*viewer, *detailsDock, *window.statusBar(), false);

    window.show();
    viewer->setViewExtent(GisExtent(-5.8, -3.3, 1.0, 3.2));

    return app.exec();
}
