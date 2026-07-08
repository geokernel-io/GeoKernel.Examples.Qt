#include <QAction>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QLabel>
#include <QMainWindow>
#include <QSplitter>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolBar>

#include <memory>
#include <vector>

#include "Layers/GisLayerStyle.h"
#include "Layers/GisLayerVector.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShape.h"
#include "Shapes/GisShapePoint.h"
#include "Shapes/GisShapePolygon.h"
#include "Shapes/GisShapePolyline.h"
#include "Symbology/GisCategorizedSymbolRenderer.h"
#include "Topology/GisTopology.h"
#include "Topology/GisTopologyCombineType.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Core::Symbology;
using namespace GeoKernel::Core::Topology;
using namespace GeoKernel::Viewer;

enum class TopologyOperation
{
    BufferA = 0,
    UnionAB = 1,
    IntersectionAB = 2,
    DifferenceAB = 3,
    SymDifferenceAB = 4,
    ConvexHullAB = 5,
    CrossingsLineB = 6,
    FixInvalidPolygon = 7,
    ArcMakeConnected = 8,
    ArcSplitOnCross = 9,
    PredicateReport = 10
};

struct TopologySampleData final
{
    GisShapePolygon polygonA;
    GisShapePolygon polygonB;
    GisShapePolyline diagonalLine;
    GisShapePolygon invalidPolygon;
    GisShapePolyline arcA;
    GisShapePolyline arcB;
    GisShapePolyline splitArc;
    GisShapePolyline splitCutter;
};

QString boolText(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

QString shapeTypeText(GisShapeType type)
{
    switch (type)
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

GisShapePolygon polygon(std::initializer_list<GisShapePoint> points)
{
    GisShapePolygon shape;
    QVector<GisShapePoint> part;
    for (const GisShapePoint& point : points)
        part.append(point);
    shape.parts().append(std::move(part));
    return shape;
}

GisShapePolyline polyline(std::initializer_list<GisShapePoint> points)
{
    GisShapePolyline shape;
    QVector<GisShapePoint> part;
    for (const GisShapePoint& point : points)
        part.append(point);
    shape.parts().append(std::move(part));
    return shape;
}

TopologySampleData createSampleData()
{
    TopologySampleData data;
    data.polygonA = polygon({
        GisShapePoint(-5.0, -2.0),
        GisShapePoint(1.0, -2.0),
        GisShapePoint(1.0, 3.0),
        GisShapePoint(-5.0, 3.0),
        GisShapePoint(-5.0, -2.0)
        });

    data.polygonB = polygon({
        GisShapePoint(-1.0, -1.0),
        GisShapePoint(5.0, -1.0),
        GisShapePoint(5.0, 4.0),
        GisShapePoint(-1.0, 4.0),
        GisShapePoint(-1.0, -1.0)
        });

    data.diagonalLine = polyline({
        GisShapePoint(-6.0, -3.0),
        GisShapePoint(6.0, 4.0)
        });

    data.invalidPolygon = polygon({
        GisShapePoint(3.0, -6.4),
        GisShapePoint(6.2, -3.2),
        GisShapePoint(3.0, -3.2),
        GisShapePoint(6.2, -6.4),
        GisShapePoint(3.0, -6.4)
        });

    data.arcA = polyline({
        GisShapePoint(-6.0, -5.5),
        GisShapePoint(-4.4, -4.4),
        GisShapePoint(-2.7, -5.4)
        });

    data.arcB = polyline({
        GisShapePoint(-2.7, -5.4),
        GisShapePoint(-0.7, -4.2),
        GisShapePoint(1.5, -5.3)
        });

    data.splitArc = polyline({
        GisShapePoint(-5.7, -6.7),
        GisShapePoint(2.2, -4.1)
        });

    data.splitCutter = polyline({
        GisShapePoint(-2.0, -7.1),
        GisShapePoint(-2.0, -3.7)
        });

    return data;
}

GisLayerStyle style(const QString& fill, int fillOpacity, const QString& line, float lineWidth)
{
    GisLayerStyle value;
    value.setFillColor(fill);
    value.setFillOpacity(fillOpacity);
    value.setLineColor(line);
    value.setLineWidth(lineWidth);
    value.setPointColor(line);
    value.setPointSize(9.0f);
    return value;
}

GisLayerStyle resultStyle()
{
    GisLayerStyle value = style(QStringLiteral("#F9C74F"), 155, QStringLiteral("#D95D39"), 3.0f);
    value.setPointColor(QStringLiteral("#D95D39"));
    value.setPointSize(12.0f);
    return value;
}

void addSourceShape(GisLayerVector& layer, const GisShape& shape, const QString& kind)
{
    std::unique_ptr<GisShape> clone = shape.clone();
    if (!clone)
        return;

    clone->attributes().insert(QStringLiteral("kind"), kind);
    layer.addShape(std::move(clone));
}

void populateSourceLayer(GisLayerVector& layer, const TopologySampleData& data)
{
    layer.setName(QStringLiteral("Topology source shapes"));

    auto renderer = std::make_unique<GisCategorizedSymbolRenderer>(QStringLiteral("kind"));
    renderer->setDefaultStyle(style(QStringLiteral("#DCE8E4"), 180, QStringLiteral("#536B68"), 1.0f));
    renderer->addClass({ QStringLiteral("polygon_a"), style(QStringLiteral("#BFD7EA"), 165, QStringLiteral("#2F80C2"), 2.0f), QStringLiteral("Polygon A") });
    renderer->addClass({ QStringLiteral("polygon_b"), style(QStringLiteral("#CDE7D8"), 165, QStringLiteral("#2D6A4F"), 2.0f), QStringLiteral("Polygon B") });
    renderer->addClass({ QStringLiteral("line"), style(QStringLiteral("#FFFFFF"), 0, QStringLiteral("#2F2F2F"), 2.0f), QStringLiteral("Line") });
    renderer->addClass({ QStringLiteral("invalid"), style(QStringLiteral("#F8D7DA"), 115, QStringLiteral("#B23A48"), 2.0f), QStringLiteral("Invalid polygon") });
    renderer->addClass({ QStringLiteral("arc"), style(QStringLiteral("#FFFFFF"), 0, QStringLiteral("#6C4AB6"), 2.0f), QStringLiteral("Arc") });

    layer.style() = style(QStringLiteral("#DCE8E4"), 180, QStringLiteral("#536B68"), 1.0f);
    layer.setSymbolRenderer(std::move(renderer));

    addSourceShape(layer, data.polygonA, QStringLiteral("polygon_a"));
    addSourceShape(layer, data.polygonB, QStringLiteral("polygon_b"));
    addSourceShape(layer, data.diagonalLine, QStringLiteral("line"));
    addSourceShape(layer, data.invalidPolygon, QStringLiteral("invalid"));
    addSourceShape(layer, data.arcA, QStringLiteral("arc"));
    addSourceShape(layer, data.arcB, QStringLiteral("arc"));
    addSourceShape(layer, data.splitArc, QStringLiteral("arc"));
    addSourceShape(layer, data.splitCutter, QStringLiteral("line"));
    layer.open();
}

void appendShapeSummary(QStringList& details, const QString& label, const GisShape* shape)
{
    if (shape == nullptr)
    {
        details << QStringLiteral("%1: null").arg(label);
        return;
    }

    details << QStringLiteral("%1: %2, parts=%3, extent=%4")
        .arg(label, shapeTypeText(shape->shapeType()))
        .arg(partCount(*shape))
        .arg(extentText(shape->extent()));
}

void addResultShape(std::vector<std::unique_ptr<GisShape>>& results, std::unique_ptr<GisShape> shape, QStringList& details, const QString& label)
{
    appendShapeSummary(details, label, shape.get());
    if (shape && !shape->isEmpty())
        results.push_back(std::move(shape));
}

QStringList predicateReport(GisTopology& topology, const TopologySampleData& data)
{
    QStringList details;
    topology.RelatePrepare(data.polygonA);

    details << QStringLiteral("Predicate report for Polygon A and Polygon B");
    details << QStringLiteral("Relate matrix: %1").arg(topology.Relate(data.polygonA, data.polygonB));
    details << QStringLiteral("Equality: %1").arg(boolText(topology.Equality(data.polygonA, data.polygonB)));
    details << QStringLiteral("Disjoint: %1").arg(boolText(topology.Disjoint(data.polygonA, data.polygonB)));
    details << QStringLiteral("Intersect: %1").arg(boolText(topology.Intersect(data.polygonA, data.polygonB)));
    details << QStringLiteral("Touch: %1").arg(boolText(topology.Touch(data.polygonA, data.polygonB)));
    details << QStringLiteral("Cross: %1").arg(boolText(topology.Cross(data.polygonA, data.polygonB)));
    details << QStringLiteral("Within A in B: %1").arg(boolText(topology.Within(data.polygonA, data.polygonB)));
    details << QStringLiteral("Contains A contains B: %1").arg(boolText(topology.Contains(data.polygonA, data.polygonB)));
    details << QStringLiteral("Overlap: %1").arg(boolText(topology.Overlap(data.polygonA, data.polygonB)));
    details << QStringLiteral("CheckShape(A): %1").arg(boolText(topology.CheckShape(data.polygonA)));
    details << QStringLiteral("CheckShape(bow-tie): %1").arg(boolText(topology.CheckShape(data.invalidPolygon)));
    details << QStringLiteral("Line/B crossings: %1").arg(topology.GetCrossings(data.diagonalLine, data.polygonB).size());

    GisTopology::ShapeList shapeList;
    shapeList.append(&data.polygonA);
    shapeList.append(&data.polygonB);
    details << QStringLiteral("FindSameOnList(A): %1").arg(topology.FindSameOnList(shapeList, data.polygonA));

    int arcIndex = -1;
    GisTopology::PolylineList arcs;
    arcs.append(&data.arcA);
    arcs.append(&data.arcB);
    topology.ArcFind(data.arcB, arcs, arcIndex);
    details << QStringLiteral("ArcFind(Arc B): %1").arg(arcIndex);
    details << QStringLiteral("SplitByArc(A, line): %1 shape(s)").arg(topology.SplitByArc(data.polygonA, data.diagonalLine).size());
    return details;
}

void renderOperation(GisViewer& viewer, QTextEdit& detailsView, QStatusBar& statusBar, const TopologySampleData& data, TopologyOperation operation)
{
    viewer.clearShapes();

    GisTopology topology;
    topology.SetTolerance(1e-9);

    QStringList details;
    std::vector<std::unique_ptr<GisShape>> results;

    switch (operation)
    {
        case TopologyOperation::BufferA:
            details << QStringLiteral("MakeBuffer(Polygon A, 0.75)");
            addResultShape(results, topology.MakeBuffer(data.polygonA, 0.75), details, QStringLiteral("Buffer"));
        break;

        case TopologyOperation::UnionAB:
            details << QStringLiteral("Combine(Polygon A, Polygon B, Union)");
            addResultShape(results, topology.Combine(data.polygonA, data.polygonB, GisTopologyCombineType::Union), details, QStringLiteral("Union"));
        break;

        case TopologyOperation::IntersectionAB:
            details << QStringLiteral("Intersection(Polygon A, Polygon B)");
            addResultShape(results, topology.Intersection(data.polygonA, data.polygonB), details, QStringLiteral("Intersection"));
        break;

        case TopologyOperation::DifferenceAB:
            details << QStringLiteral("Difference(Polygon A, Polygon B)");
            addResultShape(results, topology.Difference(data.polygonA, data.polygonB), details, QStringLiteral("Difference"));
        break;

        case TopologyOperation::SymDifferenceAB:
            details << QStringLiteral("SymmetricalDifference(Polygon A, Polygon B)");
            addResultShape(results, topology.SymmetricalDifference(data.polygonA, data.polygonB), details, QStringLiteral("Symmetrical difference"));
        break;

        case TopologyOperation::ConvexHullAB:
            details << QStringLiteral("ConvexHull(Polygon A, Polygon B)");
            addResultShape(results, topology.ConvexHull(data.polygonA, data.polygonB), details, QStringLiteral("Convex hull"));
        break;

        case TopologyOperation::CrossingsLineB:
        {
            details << QStringLiteral("GetCrossings(Line, Polygon B)");
            const QVector<GisShapePoint> crossings = topology.GetCrossings(data.diagonalLine, data.polygonB);
            details << QStringLiteral("Crossings: %1").arg(crossings.size());
            for (int i = 0; i < crossings.size(); ++i)
            {
                auto point = std::make_unique<GisShapePoint>(crossings[i].x(), crossings[i].y());
                appendShapeSummary(details, QStringLiteral("Crossing %1").arg(i + 1), point.get());
                results.push_back(std::move(point));
            }
            break;
        }

        case TopologyOperation::FixInvalidPolygon:
            details << QStringLiteral("FixShape(bow-tie polygon)");
            details << QStringLiteral("Check before: %1").arg(boolText(topology.CheckShape(data.invalidPolygon)));
            {
                std::unique_ptr<GisShape> fixed = topology.FixShape(data.invalidPolygon);
                details << QStringLiteral("Check after: %1").arg(fixed ? boolText(topology.CheckShape(*fixed)) : QStringLiteral("false"));
                addResultShape(results, std::move(fixed), details, QStringLiteral("Fixed shape"));
            }
        break;

        case TopologyOperation::ArcMakeConnected:
        {
            details << QStringLiteral("ArcMakeConnected(Arc A, [Arc B])");
            GisTopology::PolylineList arcs;
            arcs.append(&data.arcB);
            addResultShape(results, topology.ArcMakeConnected(data.arcA, arcs), details, QStringLiteral("Connected arc"));
            break;
        }

        case TopologyOperation::ArcSplitOnCross:
        {
            details << QStringLiteral("ArcSplitOnCross(split arc, [vertical cutter])");
            GisTopology::PolylineList cutters;
            cutters.append(&data.splitCutter);
            addResultShape(results, topology.ArcSplitOnCross(data.splitArc, cutters), details, QStringLiteral("Split arc"));
            break;
        }

        case TopologyOperation::PredicateReport:
            details = predicateReport(topology, data);
        break;

        default:
            break;
    }

    const GisLayerStyle overlayStyle = resultStyle();
    for (auto& result : results)
        viewer.addOwnedShape(std::move(result), overlayStyle);

    viewer.invalidateRenderCache(true, true);
    viewer.update();

    detailsView.setPlainText(details.join(QStringLiteral("\n")));
    statusBar.showMessage(QStringLiteral("Topology operation result shapes: %1").arg(results.size()));
}

TopologyOperation currentOperation(const QComboBox& combo)
{
    return static_cast<TopologyOperation>(combo.currentData().toInt());
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1120, 760);
    window.setWindowTitle(QStringLiteral("Topology"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    auto* operationCombo = new QComboBox(toolbar);
    operationCombo->addItem(QStringLiteral("Buffer A"), static_cast<int>(TopologyOperation::BufferA));
    operationCombo->addItem(QStringLiteral("Union A + B"), static_cast<int>(TopologyOperation::UnionAB));
    operationCombo->addItem(QStringLiteral("Intersection A / B"), static_cast<int>(TopologyOperation::IntersectionAB));
    operationCombo->addItem(QStringLiteral("Difference A - B"), static_cast<int>(TopologyOperation::DifferenceAB));
    operationCombo->addItem(QStringLiteral("Sym Difference A / B"), static_cast<int>(TopologyOperation::SymDifferenceAB));
    operationCombo->addItem(QStringLiteral("Convex Hull A + B"), static_cast<int>(TopologyOperation::ConvexHullAB));
    operationCombo->addItem(QStringLiteral("Crossings Line / B"), static_cast<int>(TopologyOperation::CrossingsLineB));
    operationCombo->addItem(QStringLiteral("Fix Invalid Polygon"), static_cast<int>(TopologyOperation::FixInvalidPolygon));
    operationCombo->addItem(QStringLiteral("Arc Make Connected"), static_cast<int>(TopologyOperation::ArcMakeConnected));
    operationCombo->addItem(QStringLiteral("Arc Split On Cross"), static_cast<int>(TopologyOperation::ArcSplitOnCross));
    operationCombo->addItem(QStringLiteral("Predicate Report"), static_cast<int>(TopologyOperation::PredicateReport));
    operationCombo->setMinimumWidth(240);

    QAction* panAction = toolbar->addAction(QStringLiteral("Pan"));
    QAction* zoomRectAction = toolbar->addAction(QStringLiteral("Zoom Rect"));
    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Operation:"), toolbar));
    toolbar->addWidget(operationCombo);

    auto* splitter = new QSplitter(Qt::Horizontal, &window);
    auto* viewer = new GisViewer(splitter);    
    viewer->setActiveTool(GisViewerTool::Pan);

    auto* detailsView = new QTextEdit(splitter);
    detailsView->setReadOnly(true);
    detailsView->setMinimumWidth(290);

    splitter->addWidget(viewer);
    splitter->addWidget(detailsView);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({ 820, 300 });
    window.setCentralWidget(splitter);

    const TopologySampleData data = createSampleData();
    GisLayerVector sourceLayer;
    populateSourceLayer(sourceLayer, data);
    viewer->addLayer(sourceLayer);

    QObject::connect(panAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setActiveTool(GisViewerTool::Pan);
    });
    QObject::connect(zoomRectAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setActiveTool(GisViewerTool::ZoomBox);
    });
    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);

    QObject::connect(operationCombo, qOverload<int>(&QComboBox::currentIndexChanged), &window, [&]
    {
        renderOperation(*viewer, *detailsView, *window.statusBar(), data, currentOperation(*operationCombo));
    });

    renderOperation(*viewer, *detailsView, *window.statusBar(), data, currentOperation(*operationCombo));

    window.show();
    viewer->setViewExtent(GisExtent(-7.3, -7.4, 7.0, 5.0));

    return app.exec();
}
