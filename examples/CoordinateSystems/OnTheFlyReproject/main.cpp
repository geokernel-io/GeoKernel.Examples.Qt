#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QStatusBar>
#include <QVBoxLayout>

#include "CoordinateSystems/CoordinateSystemFactory.h"
#include "CoordinateSystems/CoordinateTransformer.h"
#include "Shapes/GisShapePoint.h"

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Core::CoordinateSystems;
using namespace GeoKernel::Core::Shapes;

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());
    const auto wgs84 = CoordinateSystemFactory::fromEpsg(4326);
    const auto webMercator = CoordinateSystemFactory::fromEpsg(3857);
    const CoordinateTransformer transformer(*wgs84, *webMercator);
    const GisShapePoint istanbul = transformer.transform({ 28.9784, 41.0082 });
    QMainWindow window;
    window.resize(760, 260);
    window.setWindowTitle(QStringLiteral("On-the-fly Reprojection"));
    auto* label = new QLabel(QStringLiteral(
        "GDAL/PROJ transformation\n\nEPSG:4326: 28.9784, 41.0082\nEPSG:3857: %1, %2")
        .arg(istanbul.x(), 0, 'f', 2).arg(istanbul.y(), 0, 'f', 2), &window);
    label->setAlignment(Qt::AlignCenter);
    window.setCentralWidget(label);
    window.statusBar()->showMessage(QStringLiteral("CoordinateTransformer: EPSG:4326 -> EPSG:3857"));
    window.show();
    return app.exec();
}
