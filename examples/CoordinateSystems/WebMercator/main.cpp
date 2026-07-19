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
    window.setWindowTitle(QStringLiteral("Web Mercator"));
    auto* central = new QWidget(&window);
    auto* layout = new QVBoxLayout(central);
    const auto webMercator = CoordinateSystemFactory::fromEpsg(3857);
    layout->addWidget(new QLabel(QStringLiteral("EPSG:%1 — %2, meters/unit: %3")
        .arg(webMercator->epsgCode()).arg(webMercator->name())
        .arg(webMercator->metersPerUnit().value_or(0.0)), central));
    auto* viewer = new GisViewer(central);
    viewer->setCoordinateSystem(webMercator);
    viewer->setViewExtent({ -20037508.0, -20037508.0, 20037508.0, 20037508.0 });
    layout->addWidget(viewer, 1);
    window.setCentralWidget(central);
    window.statusBar()->showMessage(QStringLiteral("CoordinateSystemFactory::fromEpsg(3857)"));
    window.show();
    return app.exec();
}
