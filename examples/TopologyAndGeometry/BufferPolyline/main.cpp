#include <QApplication>
#include <QDoubleSpinBox>
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

GisShapePolyline sourcePolyline()
{
    GisShapePolyline polyline;
    QVector<GisShapePoint> part;
    part.append(GisShapePoint(-4.6, -1.5));
    part.append(GisShapePoint(-2.8, 0.4));
    part.append(GisShapePoint(-1.0, -0.8));
    part.append(GisShapePoint(0.7, 1.2));
    part.append(GisShapePoint(2.5, 0.1));
    part.append(GisShapePoint(4.4, 1.6));
    polyline.parts().append(std::move(part));
    return polyline;
}

GisLayerStyle corridorStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#F9C74F"));
    style.setFillOpacity(105);
    style.setLineColor(QStringLiteral("#D95D39"));
    style.setLineWidth(2.0f);
    return style;
}

GisLayerStyle lineStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#FFFFFF"));
    style.setFillOpacity(0);
    style.setLineColor(QStringLiteral("#1E5678"));
    style.setLineWidth(3.0f);
    style.setPointColor(QStringLiteral("#1E5678"));
    style.setPointSize(8.0f);
    return style;
}

QString shapeTypeText(const GisShape& shape)
{
    switch (shape.shapeType())
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

int vertexCount(const GisShapePolyline& polyline)
{
    int count = 0;
    for (const QVector<GisShapePoint>& part : polyline.parts())
        count += part.size();
    return count;
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

void updateBuffer(
    GisViewer& viewer,
    QTextEdit& detailsView,
    QStatusBar& statusBar,
    double distance)
{
    viewer.clearShapes();

    const GisShapePolyline source = sourcePolyline();

    GisTopology topology;
    std::unique_ptr<GisShape> buffer = topology.MakeBuffer(source, distance, 12);
    if (!buffer || buffer->isEmpty())
    {
        viewer.addOwnedShape(source.clone(), lineStyle());
        viewer.invalidateRenderCache(true, true);
        viewer.update();

        detailsView.setPlainText(QStringLiteral("MakeBuffer(polyline, %1) returned an empty shape.").arg(distance, 0, 'f', 2));
        statusBar.showMessage(QStringLiteral("Empty buffer result"));
        return;
    }

    QStringList details;
    details << QStringLiteral("MakeBuffer(polyline, distance)");
    details << QStringLiteral("Source parts: %1").arg(source.parts().size());
    details << QStringLiteral("Source vertices: %1").arg(vertexCount(source));
    details << QStringLiteral("Distance: %1 map units").arg(distance, 0, 'f', 2);
    details << QStringLiteral("Result type: %1").arg(shapeTypeText(*buffer));
    details << QStringLiteral("Result parts: %1").arg(partCount(*buffer));
    details << QStringLiteral("Result extent: %1").arg(extentText(buffer->extent()));

    viewer.addOwnedShape(std::move(buffer), corridorStyle());
    viewer.addOwnedShape(source.clone(), lineStyle());
    viewer.invalidateRenderCache(true, true);
    viewer.update();

    detailsView.setPlainText(details.join(QStringLiteral("\n")));
    statusBar.showMessage(QStringLiteral("Polyline buffer distance: %1 map units").arg(distance, 0, 'f', 2));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(980, 680);
    window.setWindowTitle(QStringLiteral("Buffer Polyline"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    auto* distanceSpin = new QDoubleSpinBox(toolbar);
    distanceSpin->setDecimals(2);
    distanceSpin->setMinimum(0.10);
    distanceSpin->setMaximum(2.00);
    distanceSpin->setSingleStep(0.10);
    distanceSpin->setValue(0.55);
    distanceSpin->setSuffix(QStringLiteral(" units"));
    distanceSpin->setMinimumWidth(120);

    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Distance:"), toolbar));
    toolbar->addWidget(distanceSpin);

    auto* viewer = new GisViewer(&window);

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
        viewer->setViewExtent(GisExtent(-5.8, -3.2, 5.8, 3.4));
    });

    QObject::connect(distanceSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), &window, [&]
    {
        updateBuffer(*viewer, *detailsDock, *window.statusBar(), distanceSpin->value());
    });

    updateBuffer(*viewer, *detailsDock, *window.statusBar(), distanceSpin->value());

    window.show();
    viewer->setViewExtent(GisExtent(-5.8, -3.2, 5.8, 3.4));

    return app.exec();
}
