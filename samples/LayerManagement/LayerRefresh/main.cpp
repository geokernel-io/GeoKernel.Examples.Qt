#include "Helpers.h"
#include "Viewer/GisViewer.h"
#include "Layers/GisLayer.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;

void applyBaseStyle(GisLayer& layer)
{
    layer.setName(QStringLiteral("California"));
    layer.style().setFillColor(QStringLiteral("#D8E5E1"));
    layer.style().setFillOpacity(210);
    layer.style().setLineColor(QStringLiteral("#6F8883"));
    layer.style().setLineWidth(0.9f);
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon(QStringLiteral("GeoKernelAppIcon.ico")));

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("LayerRefresh"));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* toolbar = window.addToolBar(QStringLiteral("Layer Refresh"));
    toolbar->setMovable(false);

    QAction* fillAction = toolbar->addAction(QStringLiteral("Change Fill"));
    QAction* outlineAction = toolbar->addAction(QStringLiteral("Change Outline"));
    QAction* opacityAction = toolbar->addAction(QStringLiteral("Change Opacity"));
    toolbar->addSeparator();
    QAction* refreshAction = toolbar->addAction(QStringLiteral("Refresh Layer"));

    if (!loadLayer(*viewer, sampleDataPath(QStringLiteral("shapefile/california.shp")), &window))
        return 1;

    GisLayer* californiaLayer = viewer->mapLayerAt(0);
    if (californiaLayer == nullptr)
        return 1;

    applyBaseStyle(*californiaLayer);

    const std::array<QString, 4> fillColors {
        QStringLiteral("#D8E5E1"),
        QStringLiteral("#D9C7A5"),
        QStringLiteral("#C7D7EA"),
        QStringLiteral("#D7C5DE")
    };
    const std::array<QString, 4> outlineColors {
        QStringLiteral("#6F8883"),
        QStringLiteral("#A24A3D"),
        QStringLiteral("#356780"),
        QStringLiteral("#6F4D8C")
    };
    const std::array<int, 4> opacities { 210, 160, 110, 235 };

    int fillIndex = 0;
    int outlineIndex = 0;
    int opacityIndex = 0;

    QObject::connect(fillAction, &QAction::triggered, viewer, [californiaLayer, &fillIndex, fillColors]
    {
        fillIndex = (fillIndex + 1) % static_cast<int>(fillColors.size());
        californiaLayer->style().setFillColor(fillColors[fillIndex]);
    });

    QObject::connect(outlineAction, &QAction::triggered, viewer, [californiaLayer, &outlineIndex, outlineColors]
    {
        outlineIndex = (outlineIndex + 1) % static_cast<int>(outlineColors.size());
        californiaLayer->style().setLineColor(outlineColors[outlineIndex]);
        californiaLayer->style().setLineWidth(outlineIndex == 0 ? 0.9f : 1.6f);
    });

    QObject::connect(opacityAction, &QAction::triggered, viewer, [californiaLayer, &opacityIndex, opacities]
    {
        opacityIndex = (opacityIndex + 1) % static_cast<int>(opacities.size());
        californiaLayer->style().setFillOpacity(opacities[opacityIndex]);
    });

    QObject::connect(refreshAction, &QAction::triggered, viewer, &GisViewer::refreshLayers);

    viewer->refreshLayers();

    window.show();
    viewer->fullExtent();

    return app.exec();
}
