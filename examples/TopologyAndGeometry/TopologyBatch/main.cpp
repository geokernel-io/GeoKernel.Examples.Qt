#include <QApplication>
#include <QElapsedTimer>
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

namespace
{
    GisShapePolygon polygonFromRing(std::initializer_list<GisShapePoint> points, const QString& label)
    {
        GisShapePolygon polygon;
        QVector<GisShapePoint> ring;
        for (const GisShapePoint& point : points)
            ring.append(point);
        polygon.parts().append(std::move(ring));
        polygon.attributes().insert(QStringLiteral("LABEL"), label);
        return polygon;
    }

    GisShapePolygon rectangle(double xMin, double yMin, double xMax, double yMax, const QString& label)
    {
        return polygonFromRing({
            GisShapePoint(xMin, yMin),
            GisShapePoint(xMax, yMin),
            GisShapePoint(xMax, yMax),
            GisShapePoint(xMin, yMax),
            GisShapePoint(xMin, yMin)
        }, label);
    }

    GisShapePolygon diamond(double cx, double cy, double rx, double ry, const QString& label)
    {
        return polygonFromRing({
            GisShapePoint(cx, cy + ry),
            GisShapePoint(cx + rx, cy),
            GisShapePoint(cx, cy - ry),
            GisShapePoint(cx - rx, cy),
            GisShapePoint(cx, cy + ry)
        }, label);
    }

    QVector<GisShapePolygon> sourcePolygons()
    {
        QVector<GisShapePolygon> polygons;
        int index = 1;

        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                const double x = -5.4 + col * 2.35 + (row % 2) * 0.45;
                const double y = -2.2 + row * 1.55;
                polygons.append(rectangle(
                    x,
                    y,
                    x + 2.15,
                    y + 1.35,
                    QStringLiteral("P%1").arg(index++)));
            }
        }

        polygons.append(diamond(-2.8, 1.2, 1.45, 1.0, QStringLiteral("P%1").arg(index++)));
        polygons.append(diamond(2.2, -0.9, 1.35, 0.9, QStringLiteral("P%1").arg(index++)));

        return polygons;
    }

    GisLayerStyle sourceStyle(int index, bool validated = false)
    {
        static const QVector<QString> fills = {
            QStringLiteral("#BFD7EA"),
            QStringLiteral("#D8EAC4"),
            QStringLiteral("#F3D6A3"),
            QStringLiteral("#D9C8F0"),
            QStringLiteral("#BFE3D9"),
            QStringLiteral("#F0C7C7")
        };
        static const QVector<QString> lines = {
            QStringLiteral("#2F80C2"),
            QStringLiteral("#5B8E3E"),
            QStringLiteral("#B7791F"),
            QStringLiteral("#7048A8"),
            QStringLiteral("#2D6A4F"),
            QStringLiteral("#B23A48")
        };

        GisLayerStyle style;
        style.setFillColor(fills[index % fills.size()]);
        style.setFillOpacity(validated ? 135 : 90);
        style.setLineColor(lines[index % lines.size()]);
        style.setLineWidth(validated ? 2.2f : 1.5f);
        style.setShowLabels(true);
        style.setLabelField(QStringLiteral("LABEL"));
        style.setLabelFontSize(9.5f);
        style.setLabelColor(QStringLiteral("#202124"));
        style.setLabelHaloEnabled(true);
        style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
        style.setLabelHaloWidth(2.0f);
        return style;
    }

    GisLayerStyle invalidStyle()
    {
        GisLayerStyle style;
        style.setFillColor(QStringLiteral("#F4A261"));
        style.setFillOpacity(165);
        style.setLineColor(QStringLiteral("#D62828"));
        style.setLineWidth(3.0f);
        style.setShowLabels(true);
        style.setLabelField(QStringLiteral("LABEL"));
        style.setLabelFontSize(10.0f);
        style.setLabelColor(QStringLiteral("#7A0010"));
        style.setLabelHaloEnabled(true);
        style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
        style.setLabelHaloWidth(2.0f);
        return style;
    }

    GisLayerStyle unionStyle()
    {
        GisLayerStyle style;
        style.setFillColor(QStringLiteral("#F9C74F"));
        style.setFillOpacity(135);
        style.setLineColor(QStringLiteral("#D95D39"));
        style.setLineWidth(4.0f);
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

    int partCount(const GisShape& shape)
    {
        if (const auto* polygon = dynamic_cast<const GisShapePolygon*>(&shape))
            return polygon->parts().size();
        if (const auto* polyline = dynamic_cast<const GisShapePolyline*>(&shape))
            return polyline->parts().size();
        if (shape.shapeType() == GisShapeType::Point || shape.shapeType() == GisShapeType::MultiPoint)
            return 1;

        return 0;
    }

    int vertexCount(const GisShape& shape)
    {
        int total = 0;

        if (const auto* polygon = dynamic_cast<const GisShapePolygon*>(&shape))
        {
            for (const QVector<GisShapePoint>& part : polygon->parts())
                total += part.size();
        }
        else if (const auto* polyline = dynamic_cast<const GisShapePolyline*>(&shape))
        {
            for (const QVector<GisShapePoint>& part : polyline->parts())
                total += part.size();
        }
        else if (shape.shapeType() == GisShapeType::Point || shape.shapeType() == GisShapeType::MultiPoint)
        {
            total = 1;
        }

        return total;
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

    std::unique_ptr<GisShape> namedClone(const GisShape& shape, const QString& label)
    {
        std::unique_ptr<GisShape> copy = shape.clone();
        copy->attributes().insert(QStringLiteral("LABEL"), label);
        return copy;
    }

    void renderScene(
        GisViewer& viewer,
        QTextEdit& detailsView,
        QStatusBar& statusBar,
        bool runBatch)
    {
        viewer.clearShapes();

        const QVector<GisShapePolygon> polygons = sourcePolygons();

        QStringList details;
        details << QStringLiteral("TopologyBatch");
        details << QStringLiteral("Batch flow: CheckShape each polygon, then UnionOnList(valid polygons).");
        details << QStringLiteral("");
        details << QStringLiteral("Source polygon count: %1").arg(polygons.size());

        if (!runBatch)
        {
            for (int i = 0; i < polygons.size(); ++i)
                viewer.addOwnedShape(polygons[i].clone(), sourceStyle(i));

            details << QStringLiteral("");
            details << QStringLiteral("Click Run Batch to validate all polygons and build the union.");
            statusBar.showMessage(QStringLiteral("Source polygons are ready. Click Run Batch."));
        }
        else
        {
            GisTopology topology;
            GisTopology::ShapeList validShapes;
            validShapes.reserve(polygons.size());

            int invalidCount = 0;
            int sourceVertexCount = 0;

            details << QStringLiteral("");
            details << QStringLiteral("Validation:");

            for (int i = 0; i < polygons.size(); ++i)
            {
                const GisShapePolygon& polygon = polygons[i];
                sourceVertexCount += vertexCount(polygon);

                const bool valid = topology.CheckShape(polygon);
                details << QStringLiteral("P%1: %2, vertices=%3, extent=%4")
                    .arg(i + 1)
                    .arg(valid ? QStringLiteral("valid") : QStringLiteral("invalid"))
                    .arg(vertexCount(polygon))
                    .arg(extentText(polygon.extent()));

                if (valid)
                {
                    validShapes.append(&polygon);
                    viewer.addOwnedShape(polygon.clone(), sourceStyle(i, true));
                }
                else
                {
                    invalidCount++;
                    viewer.addOwnedShape(
                        namedClone(polygon, QStringLiteral("P%1 invalid").arg(i + 1)),
                        invalidStyle());
                }
            }

            details << QStringLiteral("");
            details << QStringLiteral("Valid polygons used for union: %1").arg(validShapes.size());
            details << QStringLiteral("Invalid polygons skipped: %1").arg(invalidCount);
            details << QStringLiteral("Source vertex total: %1").arg(sourceVertexCount);

            QElapsedTimer timer;
            timer.start();
            std::unique_ptr<GisShape> result = topology.UnionOnList(validShapes);
            const qint64 elapsedMs = timer.elapsed();

            if (result && !result->isEmpty())
            {
                result->attributes().insert(QStringLiteral("LABEL"), QStringLiteral("batch union result"));

                details << QStringLiteral("");
                details << QStringLiteral("Union result:");
                details << QStringLiteral("Type: %1").arg(shapeTypeText(*result));
                details << QStringLiteral("Parts: %1").arg(partCount(*result));
                details << QStringLiteral("Vertices: %1").arg(vertexCount(*result));
                details << QStringLiteral("Extent: %1").arg(extentText(result->extent()));
                details << QStringLiteral("Elapsed: %1 ms").arg(elapsedMs);

                viewer.addOwnedShape(std::move(result), unionStyle());
                statusBar.showMessage(QStringLiteral("Batch topology completed: %1 valid polygon(s), %2 ms.").arg(validShapes.size()).arg(elapsedMs));
            }
            else
            {
                details << QStringLiteral("");
                details << QStringLiteral("Union result: empty");
                details << QStringLiteral("Elapsed: %1 ms").arg(elapsedMs);
                statusBar.showMessage(QStringLiteral("Batch topology returned an empty result."));
            }
        }

        viewer.invalidateRenderCache(true, true);
        viewer.update();
        detailsView.setPlainText(details.join(QStringLiteral("\n")));
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1040, 680);
    window.setWindowTitle(QStringLiteral("TopologyBatch"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    QAction* fullExtentAction = toolbar->addAction(QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Batch: CheckShape + UnionOnList"), toolbar));
    toolbar->addSeparator();
    QAction* runBatchAction = toolbar->addAction(QStringLiteral("Run Batch"));

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
        viewer->setViewExtent(GisExtent(-6.5, -3.0, 5.8, 3.6));
    });

    QObject::connect(runBatchAction, &QAction::triggered, &window, [viewer, detailsDock, &window]
    {
        renderScene(*viewer, *detailsDock, *window.statusBar(), true);
    });

    renderScene(*viewer, *detailsDock, *window.statusBar(), false);

    window.show();
    viewer->setViewExtent(GisExtent(-6.5, -3.0, 5.8, 3.6));

    return app.exec();
}
