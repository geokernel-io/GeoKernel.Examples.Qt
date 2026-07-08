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

GisShapePolygon polygonA()
{
    GisShapePolygon polygon = polygonFromRing({
        GisShapePoint(-4.0, -1.4),
        GisShapePoint(0.7, -1.4),
        GisShapePoint(0.7, 2.0),
        GisShapePoint(-4.0, 2.0),
        GisShapePoint(-4.0, -1.4)
    });
    polygon.attributes().insert(QStringLiteral("LABEL"), QStringLiteral("Polygon A"));
    return polygon;
}

GisShapePolygon polygonB()
{
    GisShapePolygon polygon = polygonFromRing({
        GisShapePoint(-1.0, -2.1),
        GisShapePoint(3.9, -2.1),
        GisShapePoint(3.9, 1.3),
        GisShapePoint(-1.0, 1.3),
        GisShapePoint(-1.0, -2.1)
    });
    polygon.attributes().insert(QStringLiteral("LABEL"), QStringLiteral("Polygon B"));
    return polygon;
}

GisLayerStyle styleA()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#BFD7EA"));
    style.setFillOpacity(140);
    style.setLineColor(QStringLiteral("#2F80C2"));
    style.setLineWidth(2.2f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("LABEL"));
    style.setLabelFontSize(12.0f);
    style.setLabelColor(QStringLiteral("#17324D"));
    style.setLabelHaloEnabled(true);
    style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
    style.setLabelHaloWidth(2.0f);
    return style;
}

GisLayerStyle styleB()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#F6D6AD"));
    style.setFillOpacity(135);
    style.setLineColor(QStringLiteral("#D95D39"));
    style.setLineWidth(2.2f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("LABEL"));
    style.setLabelFontSize(12.0f);
    style.setLabelColor(QStringLiteral("#4B2415"));
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

void appendPatternMatch(
    QStringList& details,
    GisTopology& topology,
    const GisShape& left,
    const GisShape& right,
    const QString& name,
    const char* pattern)
{
    details << QStringLiteral("%1 (%2): %3")
        .arg(name)
        .arg(QString::fromLatin1(pattern))
        .arg(topology.Relate(left, right, QString::fromLatin1(pattern)) ? QStringLiteral("true") : QStringLiteral("false"));
}

void renderScene(
    GisViewer& viewer,
    QTextEdit& detailsView,
    QStatusBar& statusBar,
    bool showRelate)
{
    viewer.clearShapes();

    const GisShapePolygon left = polygonA();
    const GisShapePolygon right = polygonB();

    viewer.addOwnedShape(left.clone(), styleA());
    viewer.addOwnedShape(right.clone(), styleB());

    QStringList details;
    details << QStringLiteral("Relate(left, right)");
    details << QStringLiteral("DE-9IM style relation string returned by GisTopology::Relate.");
    details << QStringLiteral("");
    details << QStringLiteral("Polygon A extent: %1").arg(extentText(left.extent()));
    details << QStringLiteral("Polygon B extent: %1").arg(extentText(right.extent()));

    if (!showRelate)
    {
        details << QStringLiteral("");
        details << QStringLiteral("Click Run Relate to calculate the relation matrix.");
        statusBar.showMessage(QStringLiteral("Source polygons are ready. Click Run Relate."));
    }
    else
    {
        GisTopology topology;
        const QString matrix = topology.Relate(left, right);

        details << QStringLiteral("");
        details << QStringLiteral("Relate matrix: %1").arg(matrix);
        details << QStringLiteral("");
        details << QStringLiteral("Pattern matches:");
        appendPatternMatch(details, topology, left, right, QStringLiteral("EQUALITY"), RELATE_EQUALITY);
        appendPatternMatch(details, topology, left, right, QStringLiteral("DISJOINT"), RELATE_DISJOINT);
        appendPatternMatch(details, topology, left, right, QStringLiteral("INTERSECT"), RELATE_INTERSECT);
        appendPatternMatch(details, topology, left, right, QStringLiteral("WITHIN"), RELATE_WITHIN);
        appendPatternMatch(details, topology, left, right, QStringLiteral("CONTAINS"), RELATE_CONTAINS);
        appendPatternMatch(details, topology, left, right, QStringLiteral("TOUCH"), RELATE_TOUCH);
        appendPatternMatch(details, topology, left, right, QStringLiteral("CROSS"), RELATE_CROSS);
        appendPatternMatch(details, topology, left, right, QStringLiteral("OVERLAP"), RELATE_OVERLAP);

        statusBar.showMessage(QStringLiteral("Relate matrix calculated: %1").arg(matrix));
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
    window.setWindowTitle(QStringLiteral("SpatialRelate"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Operation: Relate(left, right)"), toolbar));
    toolbar->addSeparator();
    QAction* runRelateAction = toolbar->addAction(QStringLiteral("Run Relate"));

    auto* viewer = new GisViewer(&window);

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
        viewer->setViewExtent(GisExtent(-5.1, -3.0, 5.0, 3.0));
    });

    QObject::connect(runRelateAction, &QAction::triggered, &window, [viewer, detailsDock, &window]
    {
        renderScene(*viewer, *detailsDock, *window.statusBar(), true);
    });

    renderScene(*viewer, *detailsDock, *window.statusBar(), false);

    window.show();
    viewer->setViewExtent(GisExtent(-5.1, -3.0, 5.0, 3.0));

    return app.exec();
}
