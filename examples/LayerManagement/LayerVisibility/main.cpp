#include "Helpers.h"
#include "Layers/GisLayer.h"
#include "Shapes/GisExtent.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;

QString prepareLayerFile(QWidget* parent, const QString& zipName)
{
    return ensureSampleFile(
        QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/%1").arg(zipName)),
        zipName,
        zipName.left(zipName.size() - 4),
        QString(zipName).replace(QStringLiteral(".zip"), QStringLiteral(".shp")),
        parent);
}

bool addLayer(GisViewer& viewer, const QString& name, const QString& path, const std::function<void(GisLayer&)>& configure = {})
{
    QString errorMessage;
    if (!viewer.addLayerFromPath(path, &errorMessage))
    {
        QMessageBox::critical(
            &viewer,
            QStringLiteral("LayerVisibility"),
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

    return true;
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
    viewer->setActiveTool(GisViewerTool::Pan);

    mainLayout->addWidget(sidePanel);
    mainLayout->addWidget(viewer, 1);
    window.setCentralWidget(centralWidget);

    createNavigationToolbar(window, *viewer);

    connectLayerVisibilityUi(*viewer, *layerList, *visibilityButton);

    window.show();

    QMetaObject::invokeMethod(&window, [viewer, layerList, visibilityButton]
    {
        const QString worldPath = prepareLayerFile(viewer, QStringLiteral("world_4326.zip"));
        if (worldPath.isEmpty())
            return;

        const QString statesPath = prepareLayerFile(viewer, QStringLiteral("usa_states.zip"));
        if (statesPath.isEmpty())
            return;

        const QString citiesPath = prepareLayerFile(viewer, QStringLiteral("usa_cities.zip"));
        if (citiesPath.isEmpty())
            return;

        if (!addLayer(
            *viewer,
            QStringLiteral("World"),
            worldPath,
            [](GisLayer& layer)
            {
                layer.style().setFillColor(QStringLiteral("#D8E5E1"));
                layer.style().setFillOpacity(220);
                layer.style().setLineColor(QStringLiteral("#7B918D"));
                layer.style().setLineWidth(0.8f);
            }))
        {
            return;
        }

        if (!addLayer(
            *viewer,
            QStringLiteral("States"),
            statesPath,
            [](GisLayer& layer)
            {
                layer.style().setFillColor(QStringLiteral("#A9C8DB"));
                layer.style().setFillOpacity(115);
                layer.style().setLineColor(QStringLiteral("#356780"));
                layer.style().setLineWidth(1.2f);
            }))
        {
            return;
        }

        if (!addLayer(
            *viewer,
            QStringLiteral("Cities"),
            citiesPath,
            [](GisLayer& layer)
            {
                layer.style().setPointColor(QStringLiteral("#D95D39"));
                layer.style().setPointSize(7.0f);
            }))
        {
            return;
        }

        viewer->refreshLayers();
        refreshLayerList(*viewer, *layerList);
        updateToggleButton(*viewer, *layerList, *visibilityButton);
        viewer->setViewExtent(GisExtent(-151.2, 16.4, -41.6, 55.6));
    });

    return app.exec();
}
