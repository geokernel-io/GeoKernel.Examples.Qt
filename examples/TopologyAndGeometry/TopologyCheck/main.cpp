#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QSplitter>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolBar>

#include "Layers/GisLayerStyle.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShape.h"
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

GisShapePolygon validPolygon()
{
    return polygonFromRing({
        GisShapePoint(-5.0, -1.6),
        GisShapePoint(-2.0, -1.6),
        GisShapePoint(-2.0, 1.4),
        GisShapePoint(-5.0, 1.4),
        GisShapePoint(-5.0, -1.6)
        });
}

GisShapePolygon selfIntersectingPolygon()
{
    return polygonFromRing({
        GisShapePoint(0.0, -1.6),
        GisShapePoint(3.3, 1.4),
        GisShapePoint(0.0, 1.4),
        GisShapePoint(3.3, -1.6),
        GisShapePoint(0.0, -1.6)
        });
}

GisLayerStyle polygonStyle(const QString& fill, const QString& line, bool checked = false)
{
    GisLayerStyle style;
    style.setFillColor(fill);
    style.setFillOpacity(checked ? 165 : 125);
    style.setLineColor(line);
    style.setLineWidth(checked ? 4.0f : 2.4f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("LABEL"));
    style.setLabelFontSize(12.0f);
    style.setLabelColor(QStringLiteral("#111111"));
    style.setLabelHaloEnabled(true);
    style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
    style.setLabelHaloWidth(2.5f);
    return style;
}

QString boolText(bool value)
{
    return value ? QStringLiteral("valid") : QStringLiteral("invalid");
}

std::unique_ptr<GisShape> namedShape(const GisShape& shape, const QString& label)
{
    std::unique_ptr<GisShape> copy = shape.clone();
    copy->attributes().insert(QStringLiteral("LABEL"), label);
    return copy;
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

void renderScene(GisViewer& viewer, QTextEdit& detailsView, QStatusBar& statusBar, bool checked)
{
    viewer.clearShapes();

    const GisShapePolygon valid = validPolygon();
    const GisShapePolygon bowTie = selfIntersectingPolygon();

    viewer.addOwnedShape(
        namedShape(valid, QStringLiteral("A - valid polygon")),
        polygonStyle(QStringLiteral("#BFD7EA"), QStringLiteral("#2F80C2")));
    viewer.addOwnedShape(
        namedShape(bowTie, QStringLiteral("B - self-intersecting polygon")),
        polygonStyle(QStringLiteral("#F6D6AD"), QStringLiteral("#D95D39")));

    QStringList details;
    details << QStringLiteral("CheckShape - geometry validation");
    details << QStringLiteral("");
    details << QStringLiteral("This sample compares two polygon rings:");
    details << QStringLiteral("");
    details << QStringLiteral("A - valid polygon");
    details << QStringLiteral("Closed ring, non-zero area, no self-intersection.");
    details << QStringLiteral("Extent: %1").arg(extentText(valid.extent()));
    details << QStringLiteral("");
    details << QStringLiteral("B - self-intersecting polygon");
    details << QStringLiteral("Bow-tie ring crosses itself, so CheckShape must reject it.");
    details << QStringLiteral("Extent: %1").arg(extentText(bowTie.extent()));

    if (!checked)
    {
        details << QStringLiteral("");
        details << QStringLiteral("Click Run CheckShape to validate both polygons.");
        statusBar.showMessage(QStringLiteral("Two polygons are ready. Click Run CheckShape."));
    }
    else
    {
        GisTopology topology;
        const bool validOk = topology.CheckShape(valid);
        const bool bowTieOk = topology.CheckShape(bowTie);

        details << QStringLiteral("");
        details << QStringLiteral("Result:");
        details << QStringLiteral("A - valid polygon: CheckShape = %1").arg(boolText(validOk));
        details << QStringLiteral("B - self-intersecting polygon: CheckShape = %1").arg(boolText(bowTieOk));
        details << QStringLiteral("");
        details << QStringLiteral("Invalid means the geometry should be fixed or rejected before topology operations.");

        viewer.addOwnedShape(
            namedShape(valid, QStringLiteral("A - CheckShape: valid")),
            polygonStyle(QStringLiteral("#CDE7D8"), QStringLiteral("#2A9D8F"), true));
        viewer.addOwnedShape(
            namedShape(bowTie, QStringLiteral("B - CheckShape: invalid")),
            polygonStyle(QStringLiteral("#F4A261"), QStringLiteral("#D62828"), true));
        statusBar.showMessage(QStringLiteral("Topology check completed."));
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
    window.setWindowTitle(QStringLiteral("TopologyCheck"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Operation: CheckShape"), toolbar));
    toolbar->addSeparator();
    QAction* runCheckAction = toolbar->addAction(QStringLiteral("Run CheckShape"));

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
        viewer->setViewExtent(GisExtent(-5.8, -2.7, 5.9, 2.4));
    });

    QObject::connect(runCheckAction, &QAction::triggered, &window, [viewer, detailsDock, &window]
    {
        renderScene(*viewer, *detailsDock, *window.statusBar(), true);
    });

    renderScene(*viewer, *detailsDock, *window.statusBar(), false);

    window.show();
    viewer->setViewExtent(GisExtent(-5.8, -2.7, 5.9, 2.4));

    return app.exec();
}
