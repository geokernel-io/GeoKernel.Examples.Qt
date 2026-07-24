#include "SampleSupport.h"

#include <QApplication>
#include <QIcon>
#include <QMainWindow>
#include <QMetaObject>

using namespace GeoKernel::Viewer;

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("HelloMap"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico")));

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
        const QString worldLayer = ensureWorldLayer(&window);
        if (!worldLayer.isEmpty() && loadLayer(*viewer, worldLayer, &window))
            viewer->fullExtent();
    });

    return app.exec();
}
