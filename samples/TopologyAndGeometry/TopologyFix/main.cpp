#include <QApplication>
#include <QComboBox>
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
#include "Shapes/GisShapePolyline.h"
#include "Topology/GisTopology.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Core::Topology;
using namespace GeoKernel::Viewer;

enum class FixOperation
{
    Source,
    FixShape,
    FixShapeEx,
    ClearShape
};

GisShapePolyline messyPolyline()
{
    GisShapePolyline polyline;
    polyline.parts().append({
        GisShapePoint(-5.2, -1.3),
        GisShapePoint(-4.0, -0.2),
        GisShapePoint(-4.0, -0.2),
        GisShapePoint(-2.6, -1.1),
        GisShapePoint(-1.2, 0.5),
        GisShapePoint(-1.2, 0.5),
        GisShapePoint(0.4, 0.1)
    });
    polyline.parts().append({
        GisShapePoint(1.5, 1.0)
    });
    polyline.parts().append({
        GisShapePoint(2.8, -0.8),
        GisShapePoint(2.8, -0.8)
    });
    polyline.parts().append({
        GisShapePoint(3.7, -1.1),
        GisShapePoint(4.8, 0.3),
        GisShapePoint(5.4, -0.9)
    });
    polyline.attributes().insert(QStringLiteral("LABEL"), QStringLiteral("messy source"));
    return polyline;
}

GisLayerStyle sourceStyle()
{
    GisLayerStyle style;
    style.setFillOpacity(0);
    style.setLineColor(QStringLiteral("#6C757D"));
    style.setLineWidth(2.0f);
    style.setPointColor(QStringLiteral("#6C757D"));
    style.setPointSize(9.0f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("LABEL"));
    style.setLabelFontSize(11.0f);
    style.setLabelColor(QStringLiteral("#111111"));
    style.setLabelHaloEnabled(true);
    style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
    style.setLabelHaloWidth(2.0f);
    return style;
}

GisLayerStyle resultStyle(const QString& color)
{
    GisLayerStyle style = sourceStyle();
    style.setLineColor(color);
    style.setLineWidth(4.0f);
    style.setPointColor(color);
    style.setPointSize(12.0f);
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
    if (const auto* line = dynamic_cast<const GisShapePolyline*>(&shape))
        return line->parts().size();
    return 0;
}

int vertexCount(const GisShape& shape)
{
    const auto* line = dynamic_cast<const GisShapePolyline*>(&shape);
    if (line == nullptr)
        return 0;

    int total = 0;
    for (const QVector<GisShapePoint>& part : line->parts())
        total += part.size();
    return total;
}

QString partSummary(const GisShape& shape)
{
    const auto* line = dynamic_cast<const GisShapePolyline*>(&shape);
    if (line == nullptr)
        return QStringLiteral("-");

    QStringList rows;
    for (int i = 0; i < line->parts().size(); ++i)
        rows << QStringLiteral("part %1: %2 vertices").arg(i + 1).arg(line->parts()[i].size());
    return rows.join(QStringLiteral("\n"));
}

std::unique_ptr<GisShape> namedShape(std::unique_ptr<GisShape> shape, const QString& label)
{
    if (shape)
        shape->attributes().insert(QStringLiteral("LABEL"), label);
    return shape;
}

QString operationName(FixOperation operation)
{
    switch (operation)
    {
        case FixOperation::FixShape:
            return QStringLiteral("FixShape");
        case FixOperation::FixShapeEx:
            return QStringLiteral("FixShapeEx(preserveEmptyParts=true)");
        case FixOperation::ClearShape:
            return QStringLiteral("ClearShape");
        case FixOperation::Source:
        default:
            return QStringLiteral("Source");
    }
}

void renderScene(
    GisViewer& viewer,
    QTextEdit& detailsView,
    QStatusBar& statusBar,
    FixOperation operation)
{
    viewer.clearShapes();

    const GisShapePolyline source = messyPolyline();
    GisTopology topology;

    QStringList details;
    details << QStringLiteral("Topology fix functions");
    details << QStringLiteral("");
    details << QStringLiteral("Source: messy multipart polyline");
    details << QStringLiteral("- part 1 has duplicate consecutive vertices");
    details << QStringLiteral("- part 2 has only one vertex");
    details << QStringLiteral("- part 3 collapses to one vertex after duplicate cleanup");
    details << QStringLiteral("- part 4 is already valid");
    details << QStringLiteral("");
    details << QStringLiteral("Source parts: %1").arg(partCount(source));
    details << QStringLiteral("Source vertices: %1").arg(vertexCount(source));
    details << QStringLiteral("Source extent: %1").arg(extentText(source.extent()));
    details << QStringLiteral("Source part details:");
    details << partSummary(source);

    viewer.addOwnedShape(
        namedShape(source.clone(), QStringLiteral("source: messy multipart polyline")),
        sourceStyle());

    if (operation != FixOperation::Source)
    {
        std::unique_ptr<GisShape> result;
        QString color;
        switch (operation)
        {
            case FixOperation::FixShape:
                result = topology.FixShape(source);
                color = QStringLiteral("#2A9D8F");
                break;
            case FixOperation::FixShapeEx:
                result = topology.FixShapeEx(source, true);
                color = QStringLiteral("#7B2CBF");
                break;
            case FixOperation::ClearShape:
                result = topology.ClearShape(source);
                color = QStringLiteral("#D95D39");
                break;
            case FixOperation::Source:
            default:
                break;
        }

        if (result)
        {
            details << QStringLiteral("");
            details << QStringLiteral("Operation: %1").arg(operationName(operation));
            details << QStringLiteral("Result parts: %1").arg(partCount(*result));
            details << QStringLiteral("Result vertices: %1").arg(vertexCount(*result));
            details << QStringLiteral("Result extent: %1").arg(extentText(result->extent()));
            details << QStringLiteral("Result part details:");
            details << partSummary(*result);

            if (operation == FixOperation::FixShape)
                details << QStringLiteral("\nFixShape removes duplicate vertices and drops invalid short parts.");
            else if (operation == FixOperation::FixShapeEx)
                details << QStringLiteral("\nFixShapeEx with preserveEmptyParts keeps short/empty parts for diagnostics.");
            else if (operation == FixOperation::ClearShape)
                details << QStringLiteral("\nClearShape currently performs the same cleanup path as FixShape.");

            viewer.addOwnedShape(
                namedShape(std::move(result), QStringLiteral("%1 result").arg(operationName(operation))),
                resultStyle(color));
        }

        statusBar.showMessage(QStringLiteral("%1 applied.").arg(operationName(operation)));
    }
    else
    {
        details << QStringLiteral("");
        details << QStringLiteral("Choose an operation from the combo box to see the cleaned result.");
        statusBar.showMessage(QStringLiteral("Source geometry is shown. Choose a fix operation."));
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
    window.setWindowTitle(QStringLiteral("TopologyFix"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Operation:"), toolbar));

    auto* operationCombo = new QComboBox(toolbar);
    operationCombo->addItem(QStringLiteral("Source only"), static_cast<int>(FixOperation::Source));
    operationCombo->addItem(QStringLiteral("FixShape"), static_cast<int>(FixOperation::FixShape));
    operationCombo->addItem(QStringLiteral("FixShapeEx"), static_cast<int>(FixOperation::FixShapeEx));
    operationCombo->addItem(QStringLiteral("ClearShape"), static_cast<int>(FixOperation::ClearShape));
    toolbar->addWidget(operationCombo);

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(247, 248, 250));

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
        viewer->setViewExtent(GisExtent(-5.9, -2.4, 5.9, 1.8));
    });

    QObject::connect(operationCombo, &QComboBox::currentIndexChanged, &window, [viewer, detailsDock, operationCombo, &window]
    {
        const auto operation = static_cast<FixOperation>(operationCombo->currentData().toInt());
        renderScene(*viewer, *detailsDock, *window.statusBar(), operation);
    });

    renderScene(*viewer, *detailsDock, *window.statusBar(), FixOperation::Source);

    window.show();
    viewer->setViewExtent(GisExtent(-5.9, -2.4, 5.9, 1.8));

    return app.exec();
}
