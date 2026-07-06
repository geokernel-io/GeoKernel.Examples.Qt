#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QMainWindow>
#include <QMessageBox>
#include <QSize>
#include <QString>
#include <QToolBar>

#include "Helpers.h"
#include "Controls/Measure/GisMeasureTool.h"
#include "Controls/ScaleBar/GisScaleBar.h"
#include "Shapes/GisExtent.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Viewer;
using namespace GeoKernel::Viewer::Controls::Measure;
using namespace GeoKernel::Viewer::Controls::ScaleBar;

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon(QStringLiteral("GeoKernelAppIcon.ico")));

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("Measure"));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    window.setCentralWidget(viewer);

    auto* measureTool = new GisMeasureTool(viewer);
    measureTool->setViewer(viewer);

    auto* scaleBar = new GisScaleBar(viewer);
    scaleBar->setViewer(viewer);
    scaleBar->resize(180, 52);
    scaleBar->setAnchor(GisScaleBarAnchor::BottomLeft);
    scaleBar->raise();
    scaleBar->show();

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    window.addToolBar(toolbar);

    QActionGroup toolGroup(&window);
    toolGroup.setExclusive(true);

    QAction* panAction = toolbar->addAction(sampleIcon(QStringLiteral("Pan.svg")), QStringLiteral("Pan"));
    panAction->setCheckable(true);
    toolGroup.addAction(panAction);

    QAction* distanceAction = toolbar->addAction(sampleIcon(QStringLiteral("MeasureDistance.svg")), QStringLiteral("Distance"));
    distanceAction->setCheckable(true);
    toolGroup.addAction(distanceAction);

    QAction* areaAction = toolbar->addAction(sampleIcon(QStringLiteral("MeasureArea.svg")), QStringLiteral("Area"));
    areaAction->setCheckable(true);
    toolGroup.addAction(areaAction);

    toolbar->addSeparator();
    QAction* clearAction = toolbar->addAction(sampleIcon(QStringLiteral("Delete.svg")), QStringLiteral("Clear"));
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));

    QObject::connect(panAction, &QAction::triggered, viewer, [viewer, measureTool]
    {
        measureTool->setActive(false);
        viewer->setActiveTool(GisViewerTool::Pan);
    });

    QObject::connect(distanceAction, &QAction::triggered, viewer, [viewer, measureTool]
    {
        viewer->setActiveTool(GisViewerTool::Pan);
        measureTool->startDistance();
    });

    QObject::connect(areaAction, &QAction::triggered, viewer, [viewer, measureTool]
    {
        viewer->setActiveTool(GisViewerTool::Pan);
        measureTool->startArea();
    });

    QObject::connect(clearAction, &QAction::triggered, measureTool, &GisMeasureTool::clear);
    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);

    panAction->setChecked(true);
    viewer->setActiveTool(GisViewerTool::Pan);

    if (!loadLayer(*viewer, sampleDataPath(QStringLiteral("shapefile/world_3857.shp")), &window))
        return 1;

    if (!loadLayer(*viewer, sampleDataPath(QStringLiteral("shapefile/cities_4326.shp")), &window))
        return 1;

    const GisExtent extent(-2003750.8342789242, 3118485.5329647982, 5944423.6090070391, 7928353.6196106179);
    viewer->setViewExtent(extent);

    window.show();
        
    return app.exec();
}
