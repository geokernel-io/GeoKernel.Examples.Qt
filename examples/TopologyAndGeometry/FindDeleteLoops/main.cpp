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
#include "Topology/GisTopology.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Core::Topology;
using namespace GeoKernel::Viewer;

GisShapePolygon sourcePolygon()
{
    GisShapePolygon polygon;

    polygon.parts().append({
        GisShapePoint(-5.0, -1.7),
        GisShapePoint(-1.7, -1.7),
        GisShapePoint(-1.7, 1.6),
        GisShapePoint(-5.0, 1.6),
        GisShapePoint(-5.0, -1.7)
    });

    polygon.parts().append({
        GisShapePoint(0.4, -1.7),
        GisShapePoint(4.5, 1.6),
        GisShapePoint(0.4, 1.6),
        GisShapePoint(4.5, -1.7),
        GisShapePoint(0.4, -1.7)
    });

    polygon.attributes().insert(QStringLiteral("LABEL"), QStringLiteral("source: valid part + loop"));
    return polygon;
}

GisLayerStyle sourceStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#F6D6AD"));
    style.setFillOpacity(115);
    style.setLineColor(QStringLiteral("#D95D39"));
    style.setLineWidth(2.4f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("LABEL"));
    style.setLabelFontSize(11.5f);
    style.setLabelColor(QStringLiteral("#111111"));
    style.setLabelHaloEnabled(true);
    style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
    style.setLabelHaloWidth(2.0f);
    return style;
}

GisLayerStyle resultStyle()
{
    GisLayerStyle style = sourceStyle();
    style.setFillColor(QStringLiteral("#CDE7D8"));
    style.setFillOpacity(170);
    style.setLineColor(QStringLiteral("#2A9D8F"));
    style.setLineWidth(4.0f);
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

int partCount(const GisShape& shape)
{
    if (const auto* polygon = dynamic_cast<const GisShapePolygon*>(&shape))
        return polygon->parts().size();
    return 0;
}

int vertexCount(const GisShape& shape)
{
    const auto* polygon = dynamic_cast<const GisShapePolygon*>(&shape);
    if (polygon == nullptr)
        return 0;

    int total = 0;
    for (const QVector<GisShapePoint>& part : polygon->parts())
        total += part.size();
    return total;
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
    bool showResult)
{
    viewer.clearShapes();

    const GisShapePolygon source = sourcePolygon();
    viewer.addOwnedShape(
        namedShape(source.clone(), QStringLiteral("source: one valid part, one self-intersecting loop")),
        sourceStyle());

    QStringList details;
    details << QStringLiteral("FindAndDeleteLoops - remove self-intersecting polygon parts");
    details << QStringLiteral("");
    details << QStringLiteral("Source geometry:");
    details << QStringLiteral("- left part is a normal valid rectangle");
    details << QStringLiteral("- right part is a bow-tie loop that crosses itself");
    details << QStringLiteral("");
    details << QStringLiteral("Source parts: %1").arg(partCount(source));
    details << QStringLiteral("Source vertices: %1").arg(vertexCount(source));
    details << QStringLiteral("Source extent: %1").arg(extentText(source.extent()));
    details << QStringLiteral("Source part details:");
    details << partSummary(source);

    if (!showResult)
    {
        details << QStringLiteral("");
        details << QStringLiteral("Click Run FindAndDeleteLoops to remove the self-intersecting part.");
        statusBar.showMessage(QStringLiteral("Source polygon is ready. Click Run FindAndDeleteLoops."));
    }
    else
    {
        GisTopology topology;
        std::unique_ptr<GisShape> result = topology.FindAndDeleteLoops(source);
        if (result)
        {
            details << QStringLiteral("");
            details << QStringLiteral("Result:");
            details << QStringLiteral("Result parts: %1").arg(partCount(*result));
            details << QStringLiteral("Result vertices: %1").arg(vertexCount(*result));
            details << QStringLiteral("Result extent: %1").arg(extentText(result->extent()));
            details << QStringLiteral("Result part details:");
            details << partSummary(*result);
            details << QStringLiteral("");
            details << QStringLiteral("The self-intersecting bow-tie part is removed; the valid part remains.");

            viewer.addOwnedShape(
                namedShape(std::move(result), QStringLiteral("result: loop removed")),
                resultStyle());
        }

        statusBar.showMessage(QStringLiteral("FindAndDeleteLoops result created."));
    }

    viewer.invalidateRenderCache(true, true);
    viewer.update();
    detailsView.setPlainText(details.join(QStringLiteral("\n")));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1040, 680);
    window.setWindowTitle(QStringLiteral("FindDeleteLoops"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Operation: FindAndDeleteLoops"), toolbar));
    toolbar->addSeparator();
    QAction* runAction = toolbar->addAction(QStringLiteral("Run FindAndDeleteLoops"));

    auto* viewer = new GisViewer(&window);    

    auto* detailsDock = new QTextEdit(&window);
    detailsDock->setReadOnly(true);
    detailsDock->setMinimumWidth(330);

    auto* splitter = new QSplitter(Qt::Horizontal, &window);
    splitter->addWidget(viewer);
    splitter->addWidget(detailsDock);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({ 700, 340 });
    window.setCentralWidget(splitter);

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-5.7, -2.8, 5.2, 2.6));
    });

    QObject::connect(runAction, &QAction::triggered, &window, [viewer, detailsDock, &window]
    {
        renderScene(*viewer, *detailsDock, *window.statusBar(), true);
    });

    renderScene(*viewer, *detailsDock, *window.statusBar(), false);

    window.show();
    viewer->setViewExtent(GisExtent(-5.7, -2.8, 5.2, 2.6));

    return app.exec();
}
