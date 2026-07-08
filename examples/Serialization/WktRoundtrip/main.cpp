#include <QApplication>
#include <QColor>
#include <QMainWindow>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

#include "Layers/GisLayerStyle.h"
#include "Layers/GisLayerVector.h"
#include "Serialization/Wkt/GisWktReader.h"
#include "Serialization/Wkt/GisWktWriter.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShapePoint.h"
#include "Shapes/GisShapePolygon.h"
#include "Shapes/GisShapePolyline.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Serialization::Wkt;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Viewer;

GisLayerStyle pointStyle()
{
    GisLayerStyle style;
    style.setPointColor(QStringLiteral("#D95D39"));
    style.setLineColor(QStringLiteral("#8C321D"));
    style.setPointSize(12.0f);
    return style;
}

GisLayerStyle lineStyle()
{
    GisLayerStyle style;
    style.setLineColor(QStringLiteral("#E4572E"));
    style.setLineWidth(3.0f);
    return style;
}

GisLayerStyle polygonStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#88D18A80"));
    style.setLineColor(QStringLiteral("#1F7A4D"));
    style.setLineWidth(2.2f);
    return style;
}

std::unique_ptr<GisLayerVector> createLayer(const QString& name, GisShapeType type, const GisLayerStyle& style)
{
    auto layer = GisLayerVector::createInMemory(name, type, GisExtent(-180.0, -90.0, 180.0, 90.0));
    layer->style() = style;
    layer->open();
    return layer;
}

void runSample(GisViewer& viewer, QTextEdit& details)
{
    auto layer = createLayer(QStringLiteral("Roundtrip Polygon"), GisShapeType::Polygon, polygonStyle());
    const QString input = QStringLiteral("POLYGON((-123.25 37.15, -122.15 36.95, -121.55 37.65, -122.05 38.35, -123.05 38.15, -123.25 37.15))");
    const GisShapePolygon polygon = GisWktReader::readPolygon(input);
    const QString output = GisWktWriter::writePolygon(polygon);
    layer->beginEdit();
    layer->addShapeEdit(std::make_unique<GisShapePolygon>(polygon));
    layer->commitEdit();
    viewer.addLayer(layer);
    details.setPlainText(QStringLiteral("WktRoundtrip sample\n\nAPI\nGisWktReader::readPolygon(wkt)\nGisWktWriter::writePolygon(shape)\n\nInput WKT\n%1\n\nOutput WKT\n%2").arg(input, output));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QMainWindow window;
    window.resize(1100, 720);
    window.setWindowTitle(QStringLiteral("WktRoundtrip"));

    auto* root = new QWidget(&window);
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* viewer = new GisViewer(root);
    viewer->setActiveTool(GisViewerTool::Pan);
    layout->addWidget(viewer, 1);

    auto* details = new QTextEdit(root);
    details->setReadOnly(true);
    details->setMaximumHeight(170);
    layout->addWidget(details);

    window.setCentralWidget(root);
    runSample(*viewer, *details);
    window.statusBar()->showMessage(QStringLiteral("WktRoundtrip ready."));
    window.show();
    viewer->setViewExtent(GisExtent(-124.0, 36.4, -120.3, 38.7));
    return app.exec();
}
