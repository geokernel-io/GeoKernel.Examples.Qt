#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#include "Viewer/GisViewer.h"
#include "Shapes/GisExtent.h"
#include "Layers/GisLayerVector.h"

#include "Helpers.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;

void addHaloColor(QComboBox& combo, const QString& label, const QString& color)
{
    combo.addItem(label, color);
}

QString currentHaloColor(const QComboBox& combo)
{
    return combo.currentData().toString();
}

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
    window.setWindowTitle(QStringLiteral("LabelHalo"));

    auto* central = new QWidget(&window);
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* panel = new QWidget(central);
    panel->setFixedWidth(230);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(8, 8, 8, 8);

    auto* haloEnabledCheck = new QCheckBox(QStringLiteral("Halo enabled"), panel);
    haloEnabledCheck->setChecked(true);

    auto* haloColorCombo = new QComboBox(panel);
    addHaloColor(*haloColorCombo, QStringLiteral("White"), QStringLiteral("#FFFFFF"));
    addHaloColor(*haloColorCombo, QStringLiteral("Black"), QStringLiteral("#000000"));
    addHaloColor(*haloColorCombo, QStringLiteral("Yellow"), QStringLiteral("#FFF2A8"));
    addHaloColor(*haloColorCombo, QStringLiteral("Blue"), QStringLiteral("#BAE6FD"));
    haloColorCombo->setCurrentIndex(2);

    auto* haloWidthSpin = new QDoubleSpinBox(panel);
    haloWidthSpin->setRange(0.5, 8.0);
    haloWidthSpin->setDecimals(1);
    haloWidthSpin->setSingleStep(0.5);
    haloWidthSpin->setValue(2.5);
    haloEnabledCheck->setEnabled(false);
    haloColorCombo->setEnabled(false);
    haloWidthSpin->setEnabled(false);

    auto* form = new QFormLayout;
    form->addRow(haloEnabledCheck);
    form->addRow(QStringLiteral("Halo color"), haloColorCombo);
    form->addRow(QStringLiteral("Halo width"), haloWidthSpin);

    panelLayout->addWidget(new QLabel(QStringLiteral("Label halo"), panel));
    panelLayout->addLayout(form);
    panelLayout->addStretch(1);

    auto* viewer = new GisViewer(central);
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(panel);
    layout->addWidget(viewer, 1);
    window.setCentralWidget(central);

    window.show();

    QMetaObject::invokeMethod(&window, [&window, viewer, haloEnabledCheck, haloColorCombo, haloWidthSpin]
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
                QStringLiteral("LabelHalo"),
                QStringLiteral("World layer could not be loaded:\n%1")
                    .arg(errorMessage.isEmpty() ? worldPath : errorMessage));
            return;
        }

        auto* worldLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
        if (worldLayer == nullptr)
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("LabelHalo"),
                QStringLiteral("Loaded sample layer is not a vector layer."));
            return;
        }

        worldLayer->setName(QStringLiteral("World - label halo"));
        auto& style = worldLayer->style();
        style.setFillColor(QStringLiteral("#D8E5E1"));
        style.setFillOpacity(215);
        style.setLineColor(QStringLiteral("#6F8380"));
        style.setLineWidth(0.8f);
        style.setShowLabels(true);
        style.setLabelField(QStringLiteral("COUNTRY"));
        style.setLabelFontSize(12.0f);
        style.setLabelColor(QStringLiteral("#253238"));
        style.setLabelHaloEnabled(haloEnabledCheck->isChecked());
        style.setLabelHaloColor(currentHaloColor(*haloColorCombo));
        style.setLabelHaloWidth(static_cast<float>(haloWidthSpin->value()));

        QObject::connect(haloEnabledCheck, &QCheckBox::toggled, &window, [viewer, worldLayer, &window](bool checked) {
            worldLayer->style().setLabelHaloEnabled(checked);
            refreshLabels(*viewer);
            window.statusBar()->showMessage(checked
                ? QStringLiteral("Label halo enabled.")
                : QStringLiteral("Label halo disabled."));
        });

        QObject::connect(haloColorCombo, &QComboBox::currentIndexChanged, &window, [viewer, worldLayer, haloColorCombo, &window](int index) {
            const QString color = haloColorCombo->itemData(index).toString();
            worldLayer->style().setLabelHaloColor(color);
            refreshLabels(*viewer);
            window.statusBar()->showMessage(QStringLiteral("Label halo color: %1").arg(color));
        });

        QObject::connect(haloWidthSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), &window, [viewer, worldLayer, &window](double value) {
            worldLayer->style().setLabelHaloWidth(static_cast<float>(value));
            refreshLabels(*viewer);
            window.statusBar()->showMessage(QStringLiteral("Label halo width: %1").arg(value, 0, 'f', 1));
        });

        haloEnabledCheck->setEnabled(true);
        haloColorCombo->setEnabled(true);
        haloWidthSpin->setEnabled(true);
        viewer->setViewExtent(GisExtent(-180.0, -58.0, 180.0, 82.0));
        window.statusBar()->showMessage(QStringLiteral("Labels use labelHaloEnabled, labelHaloColor and labelHaloWidth."));
    });

    return app.exec();
}
