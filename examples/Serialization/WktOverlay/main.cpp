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
    auto points = createLayer(QStringLiteral("WKT Points"), GisShapeType::Point, pointStyle());
    auto lines = createLayer(QStringLiteral("WKT Lines"), GisShapeType::Polyline, lineStyle());
    auto polygons = createLayer(QStringLiteral("WKT Polygons"), GisShapeType::Polygon, polygonStyle());

    points->beginEdit();
    lines->beginEdit();
    polygons->beginEdit();
    points->addShapeEdit(std::make_unique<GisShapePoint>(GisWktReader::readPoint(QStringLiteral("POINT(-122.4194 37.7749)"))));
    lines->addShapeEdit(std::make_unique<GisShapePolyline>(GisWktReader::readLineString(QStringLiteral("LINESTRING(-123.0 37.1, -122.5 37.8, -121.9 37.3, -121.2 38.0)"))));
    polygons->addShapeEdit(std::make_unique<GisShapePolygon>(GisWktReader::readPolygon(QStringLiteral("POLYGON((-123.25 37.15, -122.15 36.95, -121.55 37.65, -122.05 38.35, -123.05 38.15, -123.25 37.15))"))));
    points->commitEdit();
    lines->commitEdit();
    polygons->commitEdit();

    viewer.addLayer(polygons);
    viewer.addLayer(lines);
    viewer.addLayer(points);

    details.setPlainText(QStringLiteral("WktOverlay sample\n\nAPI\nGisWktReader::readPoint/readLineString/readPolygon\nGisViewer::addLayer(layer)\n\nThree WKT strings are parsed and displayed as overlay layers."));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QMainWindow window;
    window.resize(1100, 720);
    window.setWindowTitle(QStringLiteral("WktOverlay"));

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
    window.statusBar()->showMessage(QStringLiteral("WktOverlay ready."));
    window.show();
    viewer->setViewExtent(GisExtent(-124.0, 36.4, -120.3, 38.7));
    return app.exec();
}
