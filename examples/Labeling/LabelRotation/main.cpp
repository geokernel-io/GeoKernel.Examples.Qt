#include <QApplication>
#include <QColor>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#include "Layers/GisLayerVector.h"
#include "Shapes/GisExtent.h"
#include "Viewer/GisViewer.h"

#include "Helpers.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Viewer;

void refreshLabels(GisViewer& viewer)
{
    viewer.invalidateRenderCache(true, true);
    viewer.update();
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("LabelRotation"));

    auto* central = new QWidget(&window);
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* panel = new QWidget(central);
    panel->setFixedWidth(230);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(8, 8, 8, 8);

    auto* rotationSpin = new QDoubleSpinBox(panel);
    rotationSpin->setRange(-180.0, 180.0);
    rotationSpin->setDecimals(1);
    rotationSpin->setSingleStep(5.0);
    rotationSpin->setSuffix(QStringLiteral(" deg"));
    rotationSpin->setValue(0.0);

    auto* resetButton = new QPushButton(QStringLiteral("Reset Rotation"), panel);
    rotationSpin->setEnabled(false);
    resetButton->setEnabled(false);

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Rotation"), rotationSpin);

    panelLayout->addWidget(new QLabel(QStringLiteral("Label rotation"), panel));
    panelLayout->addLayout(form);
    panelLayout->addWidget(resetButton);
    panelLayout->addStretch(1);

    auto* viewer = new GisViewer(central);
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(panel);
    layout->addWidget(viewer, 1);
    window.setCentralWidget(central);

    window.show();

    QMetaObject::invokeMethod(&window, [&window, viewer, rotationSpin, resetButton]
    {
        window.statusBar()->showMessage(QStringLiteral("Preparing world sample data..."));

        const QString worldPath = ensureSampleFile(
            QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/world_4326.zip")),
            QStringLiteral("world_4326.zip"),
            QStringLiteral("world_4326"),
            QStringLiteral("world_4326.shp"),
            &window);

        if (worldPath.isEmpty())
        {
            window.statusBar()->showMessage(QStringLiteral("World sample data could not be prepared."));
            return;
        }

        QString errorMessage;
        if (!viewer->addLayerFromPath(worldPath, &errorMessage))
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("LabelRotation"),
                QStringLiteral("World layer could not be loaded:\n%1")
                    .arg(errorMessage.isEmpty() ? worldPath : errorMessage));
            return;
        }

        auto* worldLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
        if (worldLayer == nullptr)
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("LabelRotation"),
                QStringLiteral("Loaded sample layer is not a vector layer."));
            return;
        }

        worldLayer->setName(QStringLiteral("World - label rotation"));
        auto& style = worldLayer->style();
        style.setFillColor(QStringLiteral("#D8E5E1"));
        style.setFillOpacity(215);
        style.setLineColor(QStringLiteral("#6F8380"));
        style.setLineWidth(0.8f);
        style.setShowLabels(true);
        style.setLabelField(QStringLiteral("COUNTRY"));
        style.setLabelFontSize(12.0f);
        style.setLabelColor(QStringLiteral("#253238"));
        style.setLabelHaloEnabled(true);
        style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
        style.setLabelHaloWidth(2.0f);
        style.setLabelRotationDegrees(static_cast<float>(rotationSpin->value()));

        auto applyRotation = [viewer, worldLayer, rotationSpin, &window]() {
            worldLayer->style().setLabelRotationDegrees(static_cast<float>(rotationSpin->value()));
            refreshLabels(*viewer);
            window.statusBar()->showMessage(
                QStringLiteral("Label rotation: %1 degrees")
                    .arg(rotationSpin->value(), 0, 'f', 1));
        };

        QObject::connect(rotationSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), &window, [applyRotation](double) {
            applyRotation();
        });

        QObject::connect(resetButton, &QPushButton::clicked, &window, [rotationSpin, applyRotation]() {
            rotationSpin->setValue(0.0);
            applyRotation();
        });

        rotationSpin->setEnabled(true);
        resetButton->setEnabled(true);
        viewer->setViewExtent(GisExtent(-180.0, -58.0, 180.0, 82.0));
        window.statusBar()->showMessage(QStringLiteral("Labels use labelRotationDegrees."));
    });

    return app.exec();
}
