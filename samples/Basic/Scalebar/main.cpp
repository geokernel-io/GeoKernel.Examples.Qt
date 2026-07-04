#include "Helpers.h"
#include "Viewer/GisViewer.h"
#include "Controls/ScaleBar/GisScaleBar.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Viewer::Controls::ScaleBar;

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon(QStringLiteral("GeoKernelAppIcon.ico")));

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("Scalebar"));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* scaleBar = new GisScaleBar(viewer);
    scaleBar->setViewer(viewer);
    scaleBar->resize(180, 52);
    scaleBar->setAnchor(GisScaleBarAnchor::BottomRight);
    scaleBar->raise();
    scaleBar->show();

    if (!loadWorldLayer(*viewer, &window))
        return 1;

    window.show();
    viewer->fullExtent();

    return app.exec();
}
