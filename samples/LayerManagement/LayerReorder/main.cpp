#include "Helpers.h"
#include "Viewer/GisViewer.h"
#include "Layers/GisLayer.h"
#include "Shapes/GisExtent.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;

bool addLayer(GisViewer& viewer, const QString& name, const QString& fileName, const std::function<void(GisLayer&)>& configure = {})
{
    QString errorMessage;
    if (!viewer.addLayerFromPath(sampleDataPath(fileName), &errorMessage))
    {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("LayerReorder"),
            QStringLiteral("Layer could not be loaded:\n%1")
            .arg(errorMessage.isEmpty() ? fileName : errorMessage));
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

void configureWorld(GisLayer& layer)
{
    layer.style().setFillColor(QStringLiteral("#D8E5E1"));
    layer.style().setFillOpacity(220);
    layer.style().setLineColor(QStringLiteral("#7B918D"));
    layer.style().setLineWidth(0.8f);
}

void configureStates(GisLayer& layer)
{
    layer.style().setFillColor(QStringLiteral("#A9C8DB"));
    layer.style().setFillOpacity(115);
    layer.style().setLineColor(QStringLiteral("#356780"));
    layer.style().setLineWidth(1.2f);
}

void configureCities(GisLayer& layer)
{
    layer.style().setPointColor(QStringLiteral("#D95D39"));
    layer.style().setPointSize(7.0f);
}

void refreshLayerList(const GisViewer& viewer, QListWidget& list, int selectedIndex = -1)
{
    list.clear();
    for (int i = 0; i < viewer.layerCount(); ++i)
        list.addItem(viewer.layerDisplayText(i));

    if (selectedIndex >= 0 && selectedIndex < list.count())
        list.setCurrentRow(selectedIndex);
    else if (list.count() > 0)
        list.setCurrentRow(0);
}

void moveSelectedLayer(GisViewer& viewer, QListWidget& list, int delta)
{
    const int fromIndex = list.currentRow();
    if (fromIndex < 0)
        return;

    const int toIndex = fromIndex + delta;
    if (toIndex < 0 || toIndex >= viewer.layerCount())
        return;

    if (!viewer.moveLayer(fromIndex, toIndex))
        return;

    refreshLayerList(viewer, list, toIndex);
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon(QStringLiteral("GeoKernelAppIcon.ico")));

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("LayerReorder"));

    auto* centralWidget = new QWidget(&window);
    auto* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* sidePanel = new QWidget(centralWidget);
    sidePanel->setFixedWidth(220);
    auto* sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(8, 8, 8, 8);
    sideLayout->setSpacing(8);

    auto* layerList = new QListWidget(sidePanel);
    auto* moveUpButton = new QPushButton(QStringLiteral("Move Up"), sidePanel);
    auto* moveDownButton = new QPushButton(QStringLiteral("Move Down"), sidePanel);

    sideLayout->addWidget(layerList, 1);
    sideLayout->addWidget(moveUpButton);
    sideLayout->addWidget(moveDownButton);

    auto* viewer = new GisViewer(centralWidget);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);

    mainLayout->addWidget(sidePanel);
    mainLayout->addWidget(viewer, 1);
    window.setCentralWidget(centralWidget);

    createNavigationToolbar(window, *viewer);

    if (!addLayer(*viewer, QStringLiteral("World"), QStringLiteral("shapefile/world_4326.shp"), configureWorld))
        return 1;

    if (!addLayer(*viewer, QStringLiteral("States"), QStringLiteral("shapefile/usa_states.shp"), configureStates))
        return 1;

    if (!addLayer(*viewer, QStringLiteral("Cities"), QStringLiteral("kml/usa_cities_4326.kml"), configureCities))
        return 1;

    refreshLayerList(*viewer, *layerList);    

    QObject::connect(moveUpButton, &QPushButton::clicked, viewer, [viewer, layerList]
    {
        moveSelectedLayer(*viewer, *layerList, -1);
    });

    QObject::connect(moveDownButton, &QPushButton::clicked, viewer, [viewer, layerList]
    {
        moveSelectedLayer(*viewer, *layerList, 1);
    });

    QObject::connect(viewer, &GisViewer::layersChanged, layerList, [viewer, layerList]
    {
        const int selectedIndex = layerList->currentRow();
        refreshLayerList(*viewer, *layerList, selectedIndex);
    });

    window.show();
    viewer->setViewExtent(GisExtent(-151.2, 16.4, -41.6, 55.6));

    return app.exec();
}
