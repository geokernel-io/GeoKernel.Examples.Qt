#include "Helpers.h"
#include "Viewer/GisViewer.h"
#include "Controls/ScaleBar/GisScaleBar.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Viewer::Controls::ScaleBar;

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("Scalebar"));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* scaleBar = new GisScaleBar(viewer);
    scaleBar->setViewer(viewer);
    scaleBar->resize(180, 52);
    scaleBar->setAnchor(GisScaleBarAnchor::BottomRight);
    scaleBar->raise();
    scaleBar->show();

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
