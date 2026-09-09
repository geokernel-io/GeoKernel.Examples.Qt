#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QMainWindow>
#include <QMessageBox>
#include <QSize>
#include <QStatusBar>
#include <QString>
#include <QToolBar>

#include "Viewer/GisViewer.h"
#include "Shapes/GisExtent.h"

#include "SampleSupport.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Shapes;

GisExtent europeExtent3857()
{
    return GisExtent(
        -1400000.0,
        4100000.0,
        4200000.0,
        7800000.0);
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("OpenStreetMap"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico")));

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("OpenStreetMap"));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QAction* zoomInAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/zoom-in.svg")), QStringLiteral("Zoom In"));
    QAction* zoomOutAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/zoom-out.svg")), QStringLiteral("Zoom Out"));
    QAction* fullExtentAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/full-extent.svg")), QStringLiteral("Full Extent"));
    toolbar->addSeparator();

    QActionGroup toolGroup(&window);
    toolGroup.setExclusive(true);

    QAction* zoomRectAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/zoom-box.svg")), QStringLiteral("Zoom Rect"));
    zoomRectAction->setCheckable(true);
    toolGroup.addAction(zoomRectAction);

    QAction* panAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/pan.svg")), QStringLiteral("Pan"));
    panAction->setCheckable(true);
    toolGroup.addAction(panAction);

    QObject::connect(zoomInAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->zoomIn();
    });

    QObject::connect(zoomOutAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->zoomOut();
    });

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->fullExtent();
    });

    QObject::connect(zoomRectAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setActiveTool(GisViewerTool::ZoomBox);
    });

    QObject::connect(panAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setActiveTool(GisViewerTool::Pan);
    });

    panAction->setChecked(true);

    if (viewer->addOpenStreetMapLayer() < 0)
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("OpenStreetMap"),
            QStringLiteral("OpenStreetMap layer could not be added."));
        return 1;
    }

    window.statusBar()->showMessage(QStringLiteral("OpenStreetMap added with viewer->addOpenStreetMapLayer()."));

    window.show();
    viewer->setViewExtent(europeExtent3857());

    return app.exec();
}
