#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QMainWindow>
#include <QMessageBox>
#include <QSize>
#include <QString>
#include <QToolBar>

#include "Helpers.h"
#include "Controls/Measure/GisMeasureTool.h"
#include "Controls/ScaleBar/GisScaleBar.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Viewer::Controls::Measure;
using namespace GeoKernel::Viewer::Controls::ScaleBar;

bool loadSampleLayers(GisViewer& viewer, QWidget* parent)
{
    const QString worldLayerPath = ensureSampleFile(
        QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/world_4326.zip")),
        QStringLiteral("world_4326.zip"),
        QStringLiteral("world_4326"),
        QStringLiteral("world_4326.shp"),
        parent);
    if (worldLayerPath.isEmpty())
        return false;

    const QString citiesLayerPath = ensureSampleFile(
        QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/world_cities_4326.zip")),
        QStringLiteral("world_cities_4326.zip"),
        QStringLiteral("world_cities_4326"),
        QStringLiteral("world_cities_4326.shp"),
        parent);
    if (citiesLayerPath.isEmpty())
        return false;

    if (!loadLayer(viewer, worldLayerPath, parent))
        return false;

    return loadLayer(viewer, citiesLayerPath, parent);
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("Measure"));

    auto* viewer = new GisViewer(&window);
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

    window.show();

    QMetaObject::invokeMethod(&window, [&window, viewer]
    {
        if (loadSampleLayers(*viewer, &window))
            viewer->fullExtent();
    });

    return app.exec();
}
