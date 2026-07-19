#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QStatusBar>
#include <QVBoxLayout>

#include "CoordinateSystems/CoordinateSystemFactory.h"
#include "Viewer/GisViewer.h"

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Core::CoordinateSystems;
using namespace GeoKernel::Viewer;

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());
    QMainWindow window;
    window.resize(1100, 760);
    window.setWindowTitle(QStringLiteral("WGS 84 Setup"));
    auto* central = new QWidget(&window);
    auto* layout = new QVBoxLayout(central);
    const auto wgs84 = CoordinateSystemFactory::fromEpsg(4326);
    layout->addWidget(new QLabel(QStringLiteral("EPSG:%1 — %2 (GDAL/PROJ)").arg(wgs84->epsgCode()).arg(wgs84->name()), central));
    auto* viewer = new GisViewer(central);
    viewer->setCoordinateSystem(wgs84);
    viewer->setViewExtent({ -180.0, -85.0, 180.0, 85.0 });
    layout->addWidget(viewer, 1);
    window.setCentralWidget(central);
    window.statusBar()->showMessage(QStringLiteral("CoordinateSystemFactory::fromEpsg(4326)"));
    window.show();
    return app.exec();
}
