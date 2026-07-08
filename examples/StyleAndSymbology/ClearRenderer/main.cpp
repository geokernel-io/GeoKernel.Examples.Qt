#include <QApplication>
#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#include "Viewer/GisViewer.h"
#include "Shapes/GisExtent.h"
#include "Layers/GisLayerVector.h"
#include "Symbology/GisColorRampRegistry.h"
#include "Symbology/GisSymbolRendererFactory.h"

#include "Helpers.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Symbology;

bool applyCategorizedRenderer(GisLayerVector& layer, const GisLayerStyle& defaultStyle)
{
    auto renderer = GisSymbolRendererFactory::createCategorizedRenderer(
        layer,
        QStringLiteral("STATE"),
        GisColorRampRegistry::ramp(QStringLiteral("Unique")),
        defaultStyle,
        64);

    if (!renderer)
        return false;

    layer.setSymbolRenderer(std::move(renderer));
    return true;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("ClearRenderer"));

    auto* central = new QWidget(&window);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* toolbar = new QWidget(central);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(6, 4, 6, 4);
    toolbarLayout->setSpacing(6);

    auto* applyButton = new QPushButton(QStringLiteral("Apply Categorized Renderer"), toolbar);
    auto* clearButton = new QPushButton(QStringLiteral("Clear Renderer"), toolbar);
    auto* stateLabel = new QLabel(QStringLiteral("Preparing sample data..."), toolbar);
    applyButton->setEnabled(false);
    clearButton->setEnabled(false);
    toolbarLayout->addWidget(applyButton);
    toolbarLayout->addWidget(clearButton);
    toolbarLayout->addWidget(stateLabel);
    toolbarLayout->addStretch(1);

    auto* viewer = new GisViewer(central);
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(toolbar);
    layout->addWidget(viewer, 1);
    window.setCentralWidget(central);

    window.show();

    QMetaObject::invokeMethod(&window, [&window, viewer, applyButton, clearButton, stateLabel]
    {
        const QString path = ensureSampleFile(
            QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/usa_states_3857.zip")),
            QStringLiteral("usa_states_3857.zip"),
            QStringLiteral("usa_states_3857"),
            QStringLiteral("usa_states_3857.shp"),
            &window);
        if (path.isEmpty())
        {
            stateLabel->setText(QStringLiteral("Sample data could not be prepared."));
            window.statusBar()->showMessage(QStringLiteral("Sample data could not be prepared."));
            return;
        }

        viewer->addOpenStreetMapLayer();

        QString errorMessage;
        if (!viewer->addLayerFromPath(path, &errorMessage))
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("ClearRenderer"),
                QStringLiteral("Layer could not be loaded:\n%1")
                    .arg(errorMessage.isEmpty() ? path : errorMessage));
            stateLabel->setText(QStringLiteral("Layer could not be loaded."));
            window.statusBar()->showMessage(QStringLiteral("Layer could not be loaded."));
            return;
        }

        auto* statesLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
        if (statesLayer == nullptr)
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("ClearRenderer"),
                QStringLiteral("Loaded layer is not a vector layer."));
            stateLabel->setText(QStringLiteral("Loaded layer is not a vector layer."));
            window.statusBar()->showMessage(QStringLiteral("Loaded layer is not a vector layer."));
            return;
        }

        GisLayerStyle stateStyle;
        stateStyle.setFillColor(QStringLiteral("#D8E5E1"));
        stateStyle.setFillOpacity(220);
        stateStyle.setLineColor(QStringLiteral("#536B68"));
        stateStyle.setLineWidth(0.9f);

        statesLayer->setName(QStringLiteral("USA States"));
        statesLayer->style() = stateStyle;

        if (!applyCategorizedRenderer(*statesLayer, stateStyle))
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("ClearRenderer"),
                QStringLiteral("Could not create categorized renderer from STATE field."));
            stateLabel->setText(QStringLiteral("Categorized renderer could not be created."));
            window.statusBar()->showMessage(QStringLiteral("Categorized renderer could not be created."));
            return;
        }

        QObject::connect(applyButton, &QPushButton::clicked, &window, [viewer, statesLayer, stateLabel, stateStyle, &window]
        {
            if (applyCategorizedRenderer(*statesLayer, stateStyle))
            {
                stateLabel->setText(QStringLiteral("Renderer: categorized by STATE"));
                viewer->invalidateRenderCache(true, true);
                viewer->update();
                window.statusBar()->showMessage(QStringLiteral("Categorized renderer applied."));
            }
        });

        QObject::connect(clearButton, &QPushButton::clicked, &window, [viewer, statesLayer, stateLabel, &window]
        {
            statesLayer->clearSymbolRenderer();
            stateLabel->setText(QStringLiteral("Renderer: none, default layer style"));
            viewer->invalidateRenderCache(true, true);
            viewer->update();
            window.statusBar()->showMessage(QStringLiteral("Symbol renderer cleared. Layer is back to default style."));
        });

        applyButton->setEnabled(true);
        clearButton->setEnabled(true);
        stateLabel->setText(QStringLiteral("Renderer: categorized by STATE"));
        window.statusBar()->showMessage(QStringLiteral("Categorized renderer applied. Use Clear Renderer to return to default style."));
        viewer->setViewExtent(GisExtent(-16831516.0, 1856556.0, -4631023.0, 7472472.0));
    });

    return app.exec();
}
