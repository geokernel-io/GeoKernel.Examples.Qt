#include "Helpers.h"
#include "Viewer/GisViewer.h"
#include "Controls/MiniMap/GisMiniMap.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Viewer::Controls::Minimap;

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("Minimap"));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* miniMap = new GisMiniMap(viewer);
    miniMap->setViewer(viewer);
    miniMap->setMapBackgroundColor(QColor(250, 251, 250, 220));
    miniMap->resize(220, 150);
    miniMap->setAnchor(GisMiniMapAnchor::TopRight);
    miniMap->raise();
    miniMap->show();

    const QString worldLayerPath = ensureSampleFile(
        QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/world_4326.zip")),
        QStringLiteral("world_4326.zip"),
        QStringLiteral("world_4326"),
        QStringLiteral("world_4326.shp"),
        &window);
    if (worldLayerPath.isEmpty() || !loadLayer(*viewer, worldLayerPath, &window))
        return 1;

    if (!loadLayer(*viewer, sampleDataPath(QStringLiteral("shapefile/cities_4326.shp")), &window))
        return 1;

    window.show();
    viewer->fullExtent();

    return app.exec();
}
