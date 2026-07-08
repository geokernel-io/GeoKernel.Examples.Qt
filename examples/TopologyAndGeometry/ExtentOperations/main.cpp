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
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Viewer;

GisShapePolygon extentPolygon(const GisExtent& extent, const QString& label)
{
    GisShapePolygon polygon;
    polygon.parts().append({
        GisShapePoint(extent.xMin(), extent.yMin()),
        GisShapePoint(extent.xMax(), extent.yMin()),
        GisShapePoint(extent.xMax(), extent.yMax()),
        GisShapePoint(extent.xMin(), extent.yMax()),
        GisShapePoint(extent.xMin(), extent.yMin())
    });
    polygon.attributes().insert(QStringLiteral("LABEL"), label);
    return polygon;
}

std::unique_ptr<GisShapePoint> labeledPoint(double x, double y, const QString& label)
{
    auto point = std::make_unique<GisShapePoint>(x, y);
    point->attributes().insert(QStringLiteral("LABEL"), label);
    return point;
}

GisLayerStyle extentStyle(const QString& fill, const QString& line, int fillOpacity, float lineWidth)
{
    GisLayerStyle style;
    style.setFillColor(fill);
    style.setFillOpacity(fillOpacity);
    style.setLineColor(line);
    style.setLineWidth(lineWidth);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("LABEL"));
    style.setLabelFontSize(11.0f);
    style.setLabelColor(QStringLiteral("#202124"));
    style.setLabelHaloEnabled(true);
    style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
    style.setLabelHaloWidth(2.0f);
    return style;
}

GisLayerStyle baseStyle()
{
    return extentStyle(QStringLiteral("#BFD7EA"), QStringLiteral("#2F80C2"), 90, 2.2f);
}

GisLayerStyle otherStyle()
{
    return extentStyle(QStringLiteral("#F6D6AD"), QStringLiteral("#D95D39"), 90, 2.2f);
}

GisLayerStyle expandedStyle()
{
    return extentStyle(QStringLiteral("#CDE7D8"), QStringLiteral("#2A9D8F"), 55, 3.0f);
}

GisLayerStyle inflatedStyle()
{
    return extentStyle(QStringLiteral("#E6D5F7"), QStringLiteral("#7B2CBF"), 35, 3.0f);
}

GisLayerStyle insidePointStyle()
{
    GisLayerStyle style;
    style.setPointColor(QStringLiteral("#2A9D8F"));
    style.setPointSize(11.0f);
    style.setLineColor(QStringLiteral("#145A4B"));
    style.setLineWidth(1.0f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("LABEL"));
    style.setLabelFontSize(10.0f);
    style.setLabelColor(QStringLiteral("#145A4B"));
    style.setLabelHaloEnabled(true);
    style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
    style.setLabelHaloWidth(2.0f);
    return style;
}

GisLayerStyle outsidePointStyle()
{
    GisLayerStyle style = insidePointStyle();
    style.setPointColor(QStringLiteral("#C1121F"));
    style.setLineColor(QStringLiteral("#7A0010"));
    style.setLabelColor(QStringLiteral("#7A0010"));
    return style;
}

QString extentText(const GisExtent& extent)
{
    if (extent.isEmpty())
        return QStringLiteral("(empty)");

    return QStringLiteral("(%1, %2) - (%3, %4), w=%5, h=%6")
        .arg(extent.xMin(), 0, 'f', 2)
        .arg(extent.yMin(), 0, 'f', 2)
        .arg(extent.xMax(), 0, 'f', 2)
        .arg(extent.yMax(), 0, 'f', 2)
        .arg(extent.width(), 0, 'f', 2)
        .arg(extent.height(), 0, 'f', 2);
}

QString boolText(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

void renderScene(GisViewer& viewer, QTextEdit& detailsView, QStatusBar& statusBar)
{
    viewer.clearShapes();

    const GisExtent base(-4.4, -1.8, 0.8, 1.8);
    const GisExtent other(-0.8, -0.6, 4.2, 2.6);
    const GisExtent expanded = base.expand(other);
    const GisExtent inflated = base.inflate(0.9, 0.7);
    const GisShapePoint inside(-2.0, 0.4);
    const GisShapePoint outside(2.8, -1.2);

    viewer.addOwnedShape(extentPolygon(expanded, QStringLiteral("A.expand(B)")).clone(), expandedStyle());
    viewer.addOwnedShape(extentPolygon(inflated, QStringLiteral("A.inflate(0.9, 0.7)")).clone(), inflatedStyle());
    viewer.addOwnedShape(extentPolygon(base, QStringLiteral("A")).clone(), baseStyle());
    viewer.addOwnedShape(extentPolygon(other, QStringLiteral("B")).clone(), otherStyle());
    viewer.addOwnedShape(labeledPoint(inside.x(), inside.y(), QStringLiteral("contains: true")), insidePointStyle());
    viewer.addOwnedShape(labeledPoint(outside.x(), outside.y(), QStringLiteral("contains: false")), outsidePointStyle());

    QStringList details;
    details << QStringLiteral("GisExtent operations");
    details << QStringLiteral("");
    details << QStringLiteral("A: %1").arg(extentText(base));
    details << QStringLiteral("B: %1").arg(extentText(other));
    details << QStringLiteral("");
    details << QStringLiteral("A.expand(B): %1").arg(extentText(expanded));
    details << QStringLiteral("A.inflate(0.9, 0.7): %1").arg(extentText(inflated));
    details << QStringLiteral("");
    details << QStringLiteral("A.intersects(B): %1").arg(boolText(base.intersects(other)));
    details << QStringLiteral("A.contains(inside point): %1").arg(boolText(base.contains(inside)));
    details << QStringLiteral("A.contains(outside point): %1").arg(boolText(base.contains(outside)));
    details << QStringLiteral("");
    details << QStringLiteral("Visual guide:");
    details << QStringLiteral("Blue: base extent A");
    details << QStringLiteral("Orange: extent B");
    details << QStringLiteral("Green: A expanded to include B");
    details << QStringLiteral("Purple: A inflated by dx/dy");

    viewer.invalidateRenderCache(true, true);
    viewer.update();
    detailsView.setPlainText(details.join(QStringLiteral("\n")));
    statusBar.showMessage(QStringLiteral("Extent operations rendered."));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1040, 680);
    window.setWindowTitle(QStringLiteral("ExtentOperations"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Operations: expand / inflate / intersects / contains"), toolbar));

    auto* viewer = new GisViewer(&window);

    auto* detailsDock = new QTextEdit(&window);
    detailsDock->setReadOnly(true);
    detailsDock->setMinimumWidth(350);

    auto* splitter = new QSplitter(Qt::Horizontal, &window);
    splitter->addWidget(viewer);
    splitter->addWidget(detailsDock);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({ 690, 350 });
    window.setCentralWidget(splitter);

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-5.8, -3.0, 5.4, 3.6));
    });

    renderScene(*viewer, *detailsDock, *window.statusBar());

    window.show();
    viewer->setViewExtent(GisExtent(-5.8, -3.0, 5.4, 3.6));

    return app.exec();
}
