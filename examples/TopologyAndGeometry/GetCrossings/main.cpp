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

GisShapePolyline polylineFromPoints(std::initializer_list<GisShapePoint> points, const QString& label)
{
    GisShapePolyline polyline;
    QVector<GisShapePoint> part;
    for (const GisShapePoint& point : points)
        part.append(point);
    polyline.parts().append(std::move(part));
    polyline.attributes().insert(QStringLiteral("LABEL"), label);
    return polyline;
}

GisShapePolyline leftLine()
{
    return polylineFromPoints({
        GisShapePoint(-6.0, -2.2),
        GisShapePoint(-4.2, 1.6),
        GisShapePoint(-2.0, -0.5),
        GisShapePoint(0.2, 2.1),
        GisShapePoint(2.4, -0.7),
        GisShapePoint(5.8, 2.2)
    }, QStringLiteral("Left polyline"));
}

GisShapePolyline rightLine()
{
    return polylineFromPoints({
        GisShapePoint(-6.2, 1.9),
        GisShapePoint(-3.8, -1.6),
        GisShapePoint(-1.4, 1.5),
        GisShapePoint(1.2, -1.9),
        GisShapePoint(3.2, 1.3),
        GisShapePoint(5.8, -1.2)
    }, QStringLiteral("Right polyline"));
}

GisLayerStyle lineStyle(const QString& color, float width)
{
    GisLayerStyle style;
    style.setFillOpacity(0);
    style.setLineColor(color);
    style.setLineWidth(width);
    style.setPointColor(color);
    style.setPointSize(7.0f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("LABEL"));
    style.setLabelFontSize(11.0f);
    style.setLabelColor(QStringLiteral("#202124"));
    style.setLabelHaloEnabled(true);
    style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
    style.setLabelHaloWidth(2.0f);
    return style;
}

GisLayerStyle leftStyle()
{
    return lineStyle(QStringLiteral("#2F80C2"), 3.0f);
}

GisLayerStyle rightStyle()
{
    return lineStyle(QStringLiteral("#D95D39"), 3.0f);
}

GisLayerStyle crossingStyle()
{
    GisLayerStyle style;
    style.setPointColor(QStringLiteral("#C1121F"));
    style.setPointSize(12.0f);
    style.setLineColor(QStringLiteral("#7A0010"));
    style.setLineWidth(1.0f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("LABEL"));
    style.setLabelFontSize(10.0f);
    style.setLabelColor(QStringLiteral("#111111"));
    style.setLabelHaloEnabled(true);
    style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
    style.setLabelHaloWidth(2.0f);
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

int vertexCount(const GisShapePolyline& polyline)
{
    int total = 0;
    for (const QVector<GisShapePoint>& part : polyline.parts())
        total += part.size();
    return total;
}

std::unique_ptr<GisShapePoint> crossingPoint(const GisShapePoint& source, int index)
{
    auto point = std::make_unique<GisShapePoint>(source.x(), source.y());
    point->attributes().insert(QStringLiteral("LABEL"), QStringLiteral("P%1").arg(index + 1));
    return point;
}

void renderScene(
    GisViewer& viewer,
    QTextEdit& detailsView,
    QStatusBar& statusBar,
    bool showResult)
{
    viewer.clearShapes();

    const GisShapePolyline left = leftLine();
    const GisShapePolyline right = rightLine();

    viewer.addOwnedShape(left.clone(), leftStyle());
    viewer.addOwnedShape(right.clone(), rightStyle());

    QStringList details;
    details << QStringLiteral("GetCrossings(left, right)");
    details << QStringLiteral("The two polylines are arranged to cross at multiple segment intersections.");
    details << QStringLiteral("");
    details << QStringLiteral("Left vertices: %1").arg(vertexCount(left));
    details << QStringLiteral("Right vertices: %1").arg(vertexCount(right));
    details << QStringLiteral("Left extent: %1").arg(extentText(left.extent()));
    details << QStringLiteral("Right extent: %1").arg(extentText(right.extent()));

    if (!showResult)
    {
        details << QStringLiteral("");
        details << QStringLiteral("Click Run GetCrossings to calculate intersection points.");
        statusBar.showMessage(QStringLiteral("Source polylines are ready. Click Run GetCrossings."));
    }
    else
    {
        GisTopology topology;
        const QVector<GisShapePoint> crossings = topology.GetCrossings(left, right);

        details << QStringLiteral("");
        details << QStringLiteral("Crossing count: %1").arg(crossings.size());

        for (int i = 0; i < crossings.size(); ++i)
        {
            details << QStringLiteral("P%1: (%2, %3)")
                .arg(i + 1)
                .arg(crossings[i].x(), 0, 'f', 3)
                .arg(crossings[i].y(), 0, 'f', 3);
            viewer.addOwnedShape(crossingPoint(crossings[i], i), crossingStyle());
        }

        statusBar.showMessage(QStringLiteral("GetCrossings found %1 point(s).").arg(crossings.size()));
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
    window.setWindowTitle(QStringLiteral("GetCrossings"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Operation: GetCrossings(left, right)"), toolbar));
    toolbar->addSeparator();
    QAction* runAction = toolbar->addAction(QStringLiteral("Run GetCrossings"));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(247, 248, 250));

    auto* detailsDock = new QTextEdit(&window);
    detailsDock->setReadOnly(true);
    detailsDock->setMinimumWidth(300);

    auto* splitter = new QSplitter(Qt::Horizontal, &window);
    splitter->addWidget(viewer);
    splitter->addWidget(detailsDock);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({ 680, 300 });
    window.setCentralWidget(splitter);

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-7.0, -3.2, 6.8, 3.2));
    });

    QObject::connect(runAction, &QAction::triggered, &window, [viewer, detailsDock, &window]
    {
        renderScene(*viewer, *detailsDock, *window.statusBar(), true);
    });

    renderScene(*viewer, *detailsDock, *window.statusBar(), false);

    window.show();
    viewer->setViewExtent(GisExtent(-7.0, -3.2, 6.8, 3.2));

    return app.exec();
}
