#include "Helpers.h"
#include "Viewer/GisViewer.h"
#include "Layers/GisLayer.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("LayerRefresh"));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* toolbar = window.addToolBar(QStringLiteral("Layer Refresh"));
    toolbar->setMovable(false);

    QAction* fillAction = toolbar->addAction(QStringLiteral("Change Fill"));
    QAction* outlineAction = toolbar->addAction(QStringLiteral("Change Outline"));
    QAction* opacityAction = toolbar->addAction(QStringLiteral("Change Opacity"));
    toolbar->addSeparator();
    QAction* refreshAction = toolbar->addAction(QStringLiteral("Refresh Layer"));

    fillAction->setEnabled(false);
    outlineAction->setEnabled(false);
    opacityAction->setEnabled(false);
    refreshAction->setEnabled(false);

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

    QObject::connect(refreshAction, &QAction::triggered, viewer, &GisViewer::refreshLayers);

    window.show();

    QMetaObject::invokeMethod(&window, [&window, viewer, fillAction, outlineAction, opacityAction, refreshAction, &fillIndex, &outlineIndex, &opacityIndex, fillColors, outlineColors, opacities]
    {
        const QString californiaPath = ensureSampleFile(
            QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/california.zip")),
            QStringLiteral("california.zip"),
            QStringLiteral("california"),
            QStringLiteral("california.shp"),
            &window);
        if (californiaPath.isEmpty())
            return;

        if (!loadLayer(*viewer, californiaPath, &window))
            return;

        GisLayer* californiaLayer = viewer->mapLayerAt(0);
        if (californiaLayer == nullptr)
            return;

        californiaLayer->setName(QStringLiteral("California"));
        californiaLayer->style().setFillColor(QStringLiteral("#D8E5E1"));
        californiaLayer->style().setFillOpacity(210);
        californiaLayer->style().setLineColor(QStringLiteral("#6F8883"));
        californiaLayer->style().setLineWidth(0.9f);

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

        fillAction->setEnabled(true);
        outlineAction->setEnabled(true);
        opacityAction->setEnabled(true);
        refreshAction->setEnabled(true);

        viewer->refreshLayers();
        viewer->fullExtent();
    });

    return app.exec();
}
