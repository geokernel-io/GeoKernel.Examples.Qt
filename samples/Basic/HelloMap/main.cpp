#include "Helpers.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Viewer;

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon(QStringLiteral("GeoKernelAppIcon.ico")));

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("HelloMap"));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    createNavigationToolbar(window, *viewer);

    if (!loadWorldLayer(*viewer, &window))
        return 1;
    
    window.show();
    viewer->fullExtent();

    return app.exec();
}
