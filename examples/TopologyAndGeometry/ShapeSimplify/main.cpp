#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QSlider>
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
#include "Topology/GisTopology.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Viewer;

GisShapePolygon sourcePolygon()
{
    GisShapePolygon polygon;
    polygon.parts().append({
        GisShapePoint(-5.8, -1.8),
        GisShapePoint(-5.4, -0.6),
        GisShapePoint(-4.9, 0.2),
        GisShapePoint(-4.2, 1.0),
        GisShapePoint(-3.5, 1.6),
        GisShapePoint(-2.7, 1.9),
        GisShapePoint(-2.0, 1.5),
        GisShapePoint(-1.2, 2.1),
        GisShapePoint(-0.3, 1.7),
        GisShapePoint(0.5, 2.0),
        GisShapePoint(1.4, 1.2),
        GisShapePoint(2.2, 1.4),
        GisShapePoint(3.0, 0.6),
        GisShapePoint(3.8, 0.9),
        GisShapePoint(4.7, 0.1),
        GisShapePoint(5.2, -0.9),
        GisShapePoint(4.2, -1.4),
        GisShapePoint(3.1, -1.1),
        GisShapePoint(2.1, -1.8),
        GisShapePoint(1.1, -1.3),
        GisShapePoint(0.1, -2.0),
        GisShapePoint(-0.9, -1.5),
        GisShapePoint(-1.9, -2.1),
        GisShapePoint(-2.8, -1.5),
        GisShapePoint(-3.8, -1.9),
        GisShapePoint(-4.7, -1.2),
        GisShapePoint(-5.8, -1.8)
    });
    polygon.attributes().insert(QStringLiteral("LABEL"), QStringLiteral("source"));
    return polygon;
}

GisLayerStyle sourceStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#BFD7EA"));
    style.setFillOpacity(80);
    style.setLineColor(QStringLiteral("#6C757D"));
    style.setLineWidth(2.0f);
    style.setPointColor(QStringLiteral("#2F80C2"));
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

GisLayerStyle resultStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#F6D6AD"));
    style.setFillOpacity(150);
    style.setLineColor(QStringLiteral("#D95D39"));
    style.setLineWidth(4.0f);
    style.setPointColor(QStringLiteral("#C1121F"));
    style.setPointSize(10.0f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("LABEL"));
    style.setLabelFontSize(11.0f);
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

int vertexCount(const GisShapePolygon& polygon)
{
    int total = 0;
    for (const QVector<GisShapePoint>& part : polygon.parts())
        total += part.size();
    return total;
}

int vertexCount(const GisShape& shape)
{
    if (const auto* polygon = dynamic_cast<const GisShapePolygon*>(&shape))
        return vertexCount(*polygon);
    return 0;
}

QString partSummary(const GisShape& shape)
{
    const auto* polygon = dynamic_cast<const GisShapePolygon*>(&shape);
    if (polygon == nullptr)
        return QStringLiteral("-");

    QStringList rows;
    for (int i = 0; i < polygon->parts().size(); ++i)
        rows << QStringLiteral("part %1: %2 vertices").arg(i + 1).arg(polygon->parts()[i].size());
    return rows.join(QStringLiteral("\n"));
}

std::unique_ptr<GisShape> namedShape(std::unique_ptr<GisShape> shape, const QString& label)
{
    if (shape)
        shape->attributes().insert(QStringLiteral("LABEL"), label);
    return shape;
}

void renderScene(
    GisViewer& viewer,
    QTextEdit& detailsView,
    QStatusBar& statusBar,
    double tolerance)
{
    viewer.clearShapes();

    const GisShapePolygon source = sourcePolygon();
    viewer.addOwnedShape(
        namedShape(source.clone(), QStringLiteral("source: %1 vertices").arg(vertexCount(source))),
        sourceStyle());

    std::unique_ptr<GisShape> simplified = source.simplify(tolerance);

    QStringList details;
    details << QStringLiteral("shape.simplify(tolerance)");
    details << QStringLiteral("Algorithm: Douglas-Peucker");
    details << QStringLiteral("");
    details << QStringLiteral("Tolerance: %1 map units").arg(tolerance, 0, 'f', 2);
    details << QStringLiteral("Source polygon vertices: %1").arg(vertexCount(source));
    details << QStringLiteral("Source extent: %1").arg(extentText(source.extent()));
    details << QStringLiteral("Source parts:");
    details << partSummary(source);

    if (simplified && !simplified->isEmpty())
    {
        details << QStringLiteral("");
        details << QStringLiteral("Simplified polygon vertices: %1").arg(vertexCount(*simplified));
        details << QStringLiteral("Removed vertices: %1").arg(vertexCount(source) - vertexCount(*simplified));
        details << QStringLiteral("Simplified extent: %1").arg(extentText(simplified->extent()));
        details << QStringLiteral("Simplified parts:");
        details << partSummary(*simplified);

        viewer.addOwnedShape(
            namedShape(std::move(simplified), QStringLiteral("simplified: tolerance %1").arg(tolerance, 0, 'f', 2)),
            resultStyle());
    }
    else
    {
        details << QStringLiteral("");
        details << QStringLiteral("Simplified result: empty");
    }

    viewer.invalidateRenderCache(true, true);
    viewer.update();
    detailsView.setPlainText(details.join(QStringLiteral("\n")));
    statusBar.showMessage(QStringLiteral("Simplify applied with tolerance %1.").arg(tolerance, 0, 'f', 2));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1040, 680);
    window.setWindowTitle(QStringLiteral("ShapeSimplify"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Tolerance:"), toolbar));

    auto* toleranceSlider = new QSlider(Qt::Horizontal, toolbar);
    toleranceSlider->setRange(0, 200);
    toleranceSlider->setSingleStep(5);
    toleranceSlider->setPageStep(25);
    toleranceSlider->setFixedWidth(180);
    toleranceSlider->setValue(45);
    toolbar->addWidget(toleranceSlider);

    auto* toleranceValueLabel = new QLabel(QStringLiteral("0.45 units"), toolbar);
    toleranceValueLabel->setMinimumWidth(70);
    toolbar->addWidget(toleranceValueLabel);

    auto* viewer = new GisViewer(&window);

    auto* detailsDock = new QTextEdit(&window);
    detailsDock->setReadOnly(true);
    detailsDock->setMinimumWidth(320);

    auto* splitter = new QSplitter(Qt::Horizontal, &window);
    splitter->addWidget(viewer);
    splitter->addWidget(detailsDock);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({ 700, 340 });
    window.setCentralWidget(splitter);

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-7.2, -3.0, 6.8, 3.1));
    });

    QObject::connect(toleranceSlider, &QSlider::valueChanged, &window, [viewer, detailsDock, toleranceSlider, toleranceValueLabel, &window]
    {
        const double tolerance = toleranceSlider->value() / 100.0;
        toleranceValueLabel->setText(QStringLiteral("%1 units").arg(tolerance, 0, 'f', 2));
        renderScene(*viewer, *detailsDock, *window.statusBar(), tolerance);
    });

    renderScene(*viewer, *detailsDock, *window.statusBar(), toleranceSlider->value() / 100.0);

    window.show();
    viewer->setViewExtent(GisExtent(-7.2, -3.0, 6.8, 3.1));

    return app.exec();
}
