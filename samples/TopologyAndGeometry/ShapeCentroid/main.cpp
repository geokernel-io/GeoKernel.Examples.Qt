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

GisShapePolygon sourcePolygon()
{
    GisShapePolygon polygon;
    polygon.parts().append({
        GisShapePoint(-4.4, -2.0),
        GisShapePoint(3.8, -2.0),
        GisShapePoint(3.8, 2.0),
        GisShapePoint(1.0, 2.0),
        GisShapePoint(1.0, -0.4),
        GisShapePoint(-1.1, -0.4),
        GisShapePoint(-1.1, 2.0),
        GisShapePoint(-4.4, 2.0),
        GisShapePoint(-4.4, -2.0)
        });
    polygon.attributes().insert(QStringLiteral("LABEL"), QStringLiteral("polygon"));
    return polygon;
}

std::unique_ptr<GisShapePoint> labeledPoint(const GisShapePoint& point, const QString& label)
{
    auto result = std::make_unique<GisShapePoint>(point.x(), point.y());
    result->attributes().insert(QStringLiteral("LABEL"), label);
    return result;
}

GisLayerStyle polygonStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#BFD7EA"));
    style.setFillOpacity(120);
    style.setLineColor(QStringLiteral("#1F6F8B"));
    style.setLineWidth(2.2f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("LABEL"));
    style.setLabelFontSize(11.0f);
    style.setLabelColor(QStringLiteral("#17324D"));
    style.setLabelHaloEnabled(true);
    style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
    style.setLabelHaloWidth(2.0f);
    return style;
}

GisLayerStyle pointStyle(const QString& fill, const QString& line, float size)
{
    GisLayerStyle style;
    style.setPointColor(fill);
    style.setLineColor(line);
    style.setLineWidth(1.4f);
    style.setPointSize(size);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("LABEL"));
    style.setLabelFontSize(11.0f);
    style.setLabelColor(line);
    style.setLabelHaloEnabled(true);
    style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
    style.setLabelHaloWidth(2.5f);
    return style;
}

GisLayerStyle centroidStyle()
{
    return pointStyle(QStringLiteral("#D95D39"), QStringLiteral("#8F2D1B"), 12.0f);
}

GisLayerStyle labelPointStyle()
{
    return pointStyle(QStringLiteral("#2A9D8F"), QStringLiteral("#145A4B"), 12.0f);
}

QString pointText(const GisShapePoint& point)
{
    if (point.isEmpty())
        return QStringLiteral("(empty)");

    return QStringLiteral("(%1, %2)")
        .arg(point.x(), 0, 'f', 3)
        .arg(point.y(), 0, 'f', 3);
}

QString boolText(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

void renderScene(GisViewer& viewer, QTextEdit& detailsView, QStatusBar& statusBar)
{
    viewer.clearShapes();

    const GisShapePolygon polygon = sourcePolygon();
    const GisShapePoint centroid = polygon.centroid();
    const GisShapePoint labelPoint = polygon.labelPoint();

    viewer.addOwnedShape(polygon.clone(), polygonStyle());
    viewer.addOwnedShape(labeledPoint(centroid, QStringLiteral("centroid()")), centroidStyle());
    viewer.addOwnedShape(labeledPoint(labelPoint, QStringLiteral("labelPoint()")), labelPointStyle());

    QStringList details;
    details << QStringLiteral("GisShapePolygon centroid sample");
    details << QStringLiteral("");
    details << QStringLiteral("centroid()");
    details << QStringLiteral("- Area-weighted polygon center.");
    details << QStringLiteral("- It can fall outside an concave polygon.");
    details << QStringLiteral("- Result: %1").arg(pointText(centroid));
    details << QStringLiteral("- polygon.contains(centroid): %1").arg(boolText(polygon.contains(centroid)));
    details << QStringLiteral("");
    details << QStringLiteral("labelPoint()");
    details << QStringLiteral("- Returns centroid when it is inside.");
    details << QStringLiteral("- Otherwise chooses a safe interior point for labeling.");
    details << QStringLiteral("- Result: %1").arg(pointText(labelPoint));
    details << QStringLiteral("- polygon.contains(labelPoint): %1").arg(boolText(polygon.contains(labelPoint)));
    details << QStringLiteral("");
    details << QStringLiteral("Visual guide:");
    details << QStringLiteral("Blue polygon: source geometry");
    details << QStringLiteral("Orange point: centroid()");
    details << QStringLiteral("Green point: labelPoint()");

    viewer.invalidateRenderCache(true, true);
    viewer.update();
    detailsView.setPlainText(details.join(QStringLiteral("\n")));
    statusBar.showMessage(QStringLiteral("Polygon centroid and label point rendered."));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1040, 680);
    window.setWindowTitle(QStringLiteral("ShapeCentroid"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("GisShapePolygon::centroid() / labelPoint()"), toolbar));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(247, 248, 250));

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
        viewer->setViewExtent(GisExtent(-5.4, -3.0, 4.8, 3.0));
    });

    renderScene(*viewer, *detailsDock, *window.statusBar());

    window.show();
    viewer->setViewExtent(GisExtent(-5.4, -3.0, 4.8, 3.0));

    return app.exec();
}
