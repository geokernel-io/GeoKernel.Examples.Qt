#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QSplitter>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolBar>

#include <functional>
#include <vector>

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

struct PredicateCase
{
    QString title;
    QString leftLabel;
    QString rightLabel;
    std::unique_ptr<GisShape> left;
    std::unique_ptr<GisShape> right;
    std::function<bool(GisTopology&, const GisShape&, const GisShape&)> evaluate;
};

GisShapePolygon rectangle(double xMin, double yMin, double xMax, double yMax, const QString& label)
{
    GisShapePolygon polygon;
    polygon.parts().append({
        GisShapePoint(xMin, yMin),
        GisShapePoint(xMax, yMin),
        GisShapePoint(xMax, yMax),
        GisShapePoint(xMin, yMax),
        GisShapePoint(xMin, yMin)
    });
    polygon.attributes().insert(QStringLiteral("LABEL"), label);
    return polygon;
}

GisShapePolyline line(std::initializer_list<GisShapePoint> points, const QString& label)
{
    GisShapePolyline polyline;
    QVector<GisShapePoint> part;
    for (const GisShapePoint& point : points)
        part.append(point);
    polyline.parts().append(std::move(part));
    polyline.attributes().insert(QStringLiteral("LABEL"), label);
    return polyline;
}

GisLayerStyle leftStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#BFD7EA"));
    style.setFillOpacity(135);
    style.setLineColor(QStringLiteral("#2F80C2"));
    style.setLineWidth(2.0f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("LABEL"));
    style.setLabelFontSize(10.5f);
    style.setLabelColor(QStringLiteral("#17324D"));
    style.setLabelHaloEnabled(true);
    style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
    style.setLabelHaloWidth(2.0f);
    return style;
}

GisLayerStyle rightStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#F6D6AD"));
    style.setFillOpacity(130);
    style.setLineColor(QStringLiteral("#D95D39"));
    style.setLineWidth(2.0f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("LABEL"));
    style.setLabelFontSize(10.5f);
    style.setLabelColor(QStringLiteral("#4B2415"));
    style.setLabelHaloEnabled(true);
    style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
    style.setLabelHaloWidth(2.0f);
    return style;
}

GisLayerStyle lineLeftStyle()
{
    GisLayerStyle style = leftStyle();
    style.setFillOpacity(0);
    style.setLineWidth(3.0f);
    return style;
}

GisLayerStyle lineRightStyle()
{
    GisLayerStyle style = rightStyle();
    style.setFillOpacity(0);
    style.setLineWidth(3.0f);
    return style;
}

std::unique_ptr<GisShape> cloneOf(const GisShape& shape)
{
    return shape.clone();
}

std::vector<PredicateCase> cases()
{
    std::vector<PredicateCase> result;

    result.push_back(PredicateCase{
        QStringLiteral("Contains(left, right)"),
        QStringLiteral("contains A"),
        QStringLiteral("inside B"),
        cloneOf(rectangle(-8.2, 3.5, -4.8, 6.3, QStringLiteral("A"))),
        cloneOf(rectangle(-7.4, 4.1, -5.7, 5.5, QStringLiteral("B"))),
        [](GisTopology& topology, const GisShape& left, const GisShape& right) { return topology.Contains(left, right); }
    });

    result.push_back(PredicateCase{
        QStringLiteral("Within(left, right)"),
        QStringLiteral("inside A"),
        QStringLiteral("contains B"),
        cloneOf(rectangle(-2.8, 4.1, -1.1, 5.5, QStringLiteral("A"))),
        cloneOf(rectangle(-3.6, 3.5, -0.2, 6.3, QStringLiteral("B"))),
        [](GisTopology& topology, const GisShape& left, const GisShape& right) { return topology.Within(left, right); }
    });

    result.push_back(PredicateCase{
        QStringLiteral("Touches(left, right)"),
        QStringLiteral("touch A"),
        QStringLiteral("touch B"),
        cloneOf(rectangle(1.2, 3.6, 3.4, 6.1, QStringLiteral("A"))),
        cloneOf(rectangle(3.4, 3.6, 5.6, 6.1, QStringLiteral("B"))),
        [](GisTopology& topology, const GisShape& left, const GisShape& right) { return topology.Touch(left, right); }
    });

    result.push_back(PredicateCase{
        QStringLiteral("Overlaps(left, right)"),
        QStringLiteral("overlap A"),
        QStringLiteral("overlap B"),
        cloneOf(rectangle(-8.2, -2.0, -5.0, 0.8, QStringLiteral("A"))),
        cloneOf(rectangle(-6.3, -0.8, -3.1, 2.0, QStringLiteral("B"))),
        [](GisTopology& topology, const GisShape& left, const GisShape& right) { return topology.Overlap(left, right); }
    });

    result.push_back(PredicateCase{
        QStringLiteral("Cross(left, right)"),
        QStringLiteral("cross A"),
        QStringLiteral("cross B"),
        cloneOf(line({ GisShapePoint(-2.9, -1.7), GisShapePoint(0.4, 1.6) }, QStringLiteral("A"))),
        cloneOf(line({ GisShapePoint(-2.9, 1.6), GisShapePoint(0.4, -1.7) }, QStringLiteral("B"))),
        [](GisTopology& topology, const GisShape& left, const GisShape& right) { return topology.Cross(left, right); }
    });

    result.push_back(PredicateCase{
        QStringLiteral("Disjoint(left, right)"),
        QStringLiteral("far A"),
        QStringLiteral("far B"),
        cloneOf(rectangle(1.4, -2.0, 3.0, -0.2, QStringLiteral("A"))),
        cloneOf(rectangle(4.2, 0.2, 5.8, 2.0, QStringLiteral("B"))),
        [](GisTopology& topology, const GisShape& left, const GisShape& right) { return topology.Disjoint(left, right); }
    });

    return result;
}

QString boolText(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
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

void renderScene(GisViewer& viewer, QTextEdit& detailsView, QStatusBar& statusBar)
{
    viewer.clearShapes();

    GisTopology topology;
    QStringList details;
    details << QStringLiteral("Spatial predicate examples");
    details << QStringLiteral("Each pair is arranged so the named predicate should evaluate to true.");
    details << QStringLiteral("");

    const std::vector<PredicateCase> allCases = cases();
    for (const PredicateCase& predicateCase : allCases)
    {
        const bool isPolylinePair =
            dynamic_cast<const GisShapePolyline*>(predicateCase.left.get()) != nullptr ||
            dynamic_cast<const GisShapePolyline*>(predicateCase.right.get()) != nullptr;

        viewer.addOwnedShape(predicateCase.left->clone(), isPolylinePair ? lineLeftStyle() : leftStyle());
        viewer.addOwnedShape(predicateCase.right->clone(), isPolylinePair ? lineRightStyle() : rightStyle());

        details << predicateCase.title;
        details << QStringLiteral("  result: %1").arg(boolText(predicateCase.evaluate(topology, *predicateCase.left, *predicateCase.right)));
        details << QStringLiteral("  left extent: %1").arg(extentText(predicateCase.left->extent()));
        details << QStringLiteral("  right extent: %1").arg(extentText(predicateCase.right->extent()));
        details << QStringLiteral("");
    }

    viewer.invalidateRenderCache(true, true);
    viewer.update();
    detailsView.setPlainText(details.join(QStringLiteral("\n")));
    statusBar.showMessage(QStringLiteral("Spatial predicates evaluated."));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1040, 720);
    window.setWindowTitle(QStringLiteral("SpatialPredicates"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Predicates: Contains / Within / Touches / Overlaps / Cross / Disjoint"), toolbar));

    auto* viewer = new GisViewer(&window);

    auto* detailsDock = new QTextEdit(&window);
    detailsDock->setReadOnly(true);
    detailsDock->setMinimumWidth(360);

    auto* splitter = new QSplitter(Qt::Horizontal, &window);
    splitter->addWidget(viewer);
    splitter->addWidget(detailsDock);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({ 680, 360 });
    window.setCentralWidget(splitter);

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-9.2, -3.2, 6.8, 7.2));
    });

    renderScene(*viewer, *detailsDock, *window.statusBar());

    window.show();
    viewer->setViewExtent(GisExtent(-9.2, -3.2, 6.8, 7.2));

    return app.exec();
}
