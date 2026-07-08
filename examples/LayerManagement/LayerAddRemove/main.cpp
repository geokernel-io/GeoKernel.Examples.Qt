#include "Helpers.h"
#include "Viewer/GisViewer.h"
#include "Layers/GisLayer.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;

bool addLayer(GisViewer& viewer, const QString& name, const QString& zipName, const QString& targetFolder, const QString& requiredFileName, QWidget* parent, const std::function<void(GisLayer&)>& configure = {})
{
    if (viewer.mapLayerByName(name) != nullptr)
        return true;

    const QString path = ensureSampleFile(
        QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/%1").arg(zipName)),
        zipName,
        targetFolder,
        requiredFileName,
        parent);
    if (path.isEmpty())
        return false;
    
    QString errorMessage;
    if (!viewer.addLayerFromPath(path, &errorMessage))
    {
        QMessageBox::critical(
            parent,
            QStringLiteral("LayerAddRemove"),
            QStringLiteral("Layer could not be loaded:\n%1")
            .arg(errorMessage.isEmpty() ? path : errorMessage));
        return false;
    }

    if (GisLayer* layer = viewer.mapLayerAt(0))
    {
        layer->setName(name);
        if (configure)
            configure(*layer);
    }

    viewer.refreshLayers();

    return true;
}

void createToolbar(QMainWindow& window, GisViewer& viewer)
{
    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);

    QAction* addWorldAction = toolbar->addAction(QStringLiteral("Add World"));
    QAction* addStatesAction = toolbar->addAction(QStringLiteral("Add States"));
    QAction* addCitiesAction = toolbar->addAction(QStringLiteral("Add Cities"));
    toolbar->addSeparator();
    QAction* removeWorldAction = toolbar->addAction(QStringLiteral("Remove World"));
    QAction* removeStatesAction = toolbar->addAction(QStringLiteral("Remove States"));
    QAction* removeCitiesAction = toolbar->addAction(QStringLiteral("Remove Cities"));
    toolbar->addSeparator();
    QAction* clearLayersAction = toolbar->addAction(QStringLiteral("Clear Layers"));

    QObject::connect(addWorldAction, &QAction::triggered, &viewer, [&viewer]
    {
        addLayer(
            viewer,
            QStringLiteral("World"),
            QStringLiteral("world_4326.zip"),
            QStringLiteral("world_4326"),
            QStringLiteral("world_4326.shp"),
            &viewer,
            [](GisLayer& layer)
            {
                layer.style().setFillColor(QStringLiteral("#D8E5E1"));
                layer.style().setFillOpacity(210);
                layer.style().setLineColor(QStringLiteral("#7B918D"));
                layer.style().setLineWidth(0.8f);
            });
    });

    QObject::connect(addStatesAction, &QAction::triggered, &viewer, [&viewer]
    {
        addLayer(
            viewer,
            QStringLiteral("States"),
            QStringLiteral("usa_states.zip"),
            QStringLiteral("usa_states"),
            QStringLiteral("usa_states.shp"),
            &viewer,
            [](GisLayer& layer)
            {
                layer.style().setFillColor(QStringLiteral("#A9C8DB"));
                layer.style().setFillOpacity(100);
                layer.style().setLineColor(QStringLiteral("#356780"));
                layer.style().setLineWidth(1.2f);
            });
    });

    QObject::connect(addCitiesAction, &QAction::triggered, &viewer, [&viewer]
    {
        addLayer(
            viewer,
            QStringLiteral("Cities"),
            QStringLiteral("usa_cities.zip"),
            QStringLiteral("usa_cities"),
            QStringLiteral("usa_cities.shp"),
            &viewer,
            [](GisLayer& layer)
            {
                layer.style().setPointColor(QStringLiteral("#D95D39"));
                layer.style().setPointSize(7.0f);
            });
    });

    QObject::connect(removeWorldAction, &QAction::triggered, &viewer, [&viewer]
    {
        viewer.removeLayerByName(QStringLiteral("World"));
    });

    QObject::connect(removeStatesAction, &QAction::triggered, &viewer, [&viewer]
    {
        viewer.removeLayerByName(QStringLiteral("States"));
    });

    QObject::connect(removeCitiesAction, &QAction::triggered, &viewer, [&viewer]
    {
        viewer.removeLayerByName(QStringLiteral("Cities"));
    });

    QObject::connect(clearLayersAction, &QAction::triggered, &viewer, &GisViewer::clearLayers);
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("LayerAddRemove"));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    createToolbar(window, *viewer);

    window.show();

    return app.exec();
}
