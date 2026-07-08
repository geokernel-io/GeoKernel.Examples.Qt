#include "Helpers.h"
#include "Viewer/GisViewer.h"
#include "Symbology/GisDefaultSymbolStyles.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Symbology;

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("HelloMap"));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    createNavigationToolbar(window, *viewer);

    window.show();

    QMetaObject::invokeMethod(&window, [&window, viewer]
    {
        const QString worldLayerPath = ensureSampleFile(
            QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/world_4326.zip")),
            QStringLiteral("world_4326.zip"),
            QStringLiteral("world_4326"),
            QStringLiteral("world_4326.shp"),
            &window);

        if (!worldLayerPath.isEmpty() && loadLayer(*viewer, worldLayerPath, &window))
            viewer->fullExtent();
    });

    return app.exec();
}
