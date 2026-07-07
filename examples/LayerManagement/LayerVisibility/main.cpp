#include "Helpers.h"
#include "Layers/GisLayer.h"
#include "Shapes/GisExtent.h"
#include "Viewer/GisViewer.h"

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
            QStringLiteral("LayerVisibility"),
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

QString layerText(const GisViewer& viewer, int index)
{
    const GisLayer* layer = viewer.mapLayerAt(index);
    if (layer == nullptr)
        return QString();

    return QStringLiteral("%1 %2")
        .arg(layer->visible() ? QStringLiteral("[x]") : QStringLiteral("[ ]"))
        .arg(viewer.layerDisplayText(index));
}

void refreshLayerList(const GisViewer& viewer, QListWidget& list, int selectedIndex = -1)
{
    list.clear();
    for (int i = 0; i < viewer.layerCount(); ++i)
        list.addItem(layerText(viewer, i));

    if (selectedIndex >= 0 && selectedIndex < list.count())
        list.setCurrentRow(selectedIndex);
    else if (list.count() > 0)
        list.setCurrentRow(0);
}

void updateToggleButton(const GisViewer& viewer, const QListWidget& list, QPushButton& button)
{
    const int index = list.currentRow();
    const GisLayer* layer = viewer.mapLayerAt(index);
    if (layer == nullptr)
    {
        button.setEnabled(false);
        button.setText(QStringLiteral("Change Visibility"));
        return;
    }

    button.setEnabled(true);
    button.setText(layer->visible()
        ? QStringLiteral("Change Visibility: Hide")
        : QStringLiteral("Change Visibility: Show"));
}

void toggleSelectedLayerVisibility(GisViewer& viewer, QListWidget& list, QPushButton& button)
{
    const int index = list.currentRow();
    GisLayer* layer = viewer.mapLayerAt(index);
    if (layer == nullptr)
        return;

    viewer.setLayerVisible(index, !layer->visible());
    refreshLayerList(viewer, list, index);
    updateToggleButton(viewer, list, button);
}

void connectLayerVisibilityUi(GisViewer& viewer, QListWidget& layerList, QPushButton& visibilityButton)
{
    QObject::connect(&layerList, &QListWidget::currentRowChanged, &visibilityButton, [&viewer, &layerList, &visibilityButton]
    {
        updateToggleButton(viewer, layerList, visibilityButton);
    });

    QObject::connect(&visibilityButton, &QPushButton::clicked, &viewer, [&viewer, &layerList, &visibilityButton]
    {
        toggleSelectedLayerVisibility(viewer, layerList, visibilityButton);
    });

    QObject::connect(&viewer, &GisViewer::layersChanged, &layerList, [&viewer, &layerList, &visibilityButton]
    {
        const int selectedIndex = layerList.currentRow();
        refreshLayerList(viewer, layerList, selectedIndex);
        updateToggleButton(viewer, layerList, visibilityButton);
    });
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("LayerVisibility"));

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
    auto* visibilityButton = new QPushButton(QStringLiteral("Change Visibility"), sidePanel);

    sideLayout->addWidget(layerList, 1);
    sideLayout->addWidget(visibilityButton);

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
    updateToggleButton(*viewer, *layerList, *visibilityButton);    

    connectLayerVisibilityUi(*viewer, *layerList, *visibilityButton);

    window.show();
    viewer->setViewExtent(GisExtent(-151.2, 16.4, -41.6, 55.6));

    return app.exec();
}
