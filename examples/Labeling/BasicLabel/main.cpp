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
#include "Layers/GisLayerVector.h"
#include "Shapes/GisExtent.h"

#include "Helpers.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;

void fillLabelFields(QComboBox& combo, const GisLayerVector& layer)
{
    combo.clear();
    for (const GisAttributeDefinition& definition : layer.attributeDefinitions())
        combo.addItem(definition.name);

    const int countryIndex = combo.findText(QStringLiteral("COUNTRY"));
    if (countryIndex >= 0)
        combo.setCurrentIndex(countryIndex);
    else if (combo.count() > 0)
        combo.setCurrentIndex(0);
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
    window.setWindowTitle(QStringLiteral("BasicLabel"));

    auto* central = new QWidget(&window);
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* panel = new QWidget(central);
    panel->setFixedWidth(230);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(8, 8, 8, 8);

    auto* showLabelsCheck = new QCheckBox(QStringLiteral("Show labels"), panel);
    showLabelsCheck->setChecked(true);
    showLabelsCheck->setEnabled(false);

    auto* fieldCombo = new QComboBox(panel);
    fieldCombo->setEnabled(false);

    auto* fontSizeSpin = new QDoubleSpinBox(panel);
    fontSizeSpin->setRange(5.0, 32.0);
    fontSizeSpin->setDecimals(1);
    fontSizeSpin->setSingleStep(1.0);
    fontSizeSpin->setValue(9.0);
    fontSizeSpin->setEnabled(false);

    auto* form = new QFormLayout;
    form->addRow(showLabelsCheck);
    form->addRow(QStringLiteral("Label field"), fieldCombo);
    form->addRow(QStringLiteral("Font size"), fontSizeSpin);

    panelLayout->addWidget(new QLabel(QStringLiteral("Label style"), panel));
    panelLayout->addLayout(form);
    panelLayout->addStretch(1);

    auto* viewer = new GisViewer(central);
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(panel);
    layout->addWidget(viewer, 1);
    window.setCentralWidget(central);

    window.statusBar()->showMessage(QStringLiteral("Preparing world sample data..."));
    window.show();

    QMetaObject::invokeMethod(&window, [&window, viewer, showLabelsCheck, fieldCombo, fontSizeSpin]
    {
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
                QStringLiteral("BasicLabel"),
                QStringLiteral("World layer could not be loaded:\n%1")
                    .arg(errorMessage.isEmpty() ? worldPath : errorMessage));
            window.statusBar()->showMessage(QStringLiteral("World layer could not be loaded."));
            return;
        }

        auto* worldLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
        if (worldLayer == nullptr)
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("BasicLabel"),
                QStringLiteral("Loaded sample layer is not a vector layer."));
            window.statusBar()->showMessage(QStringLiteral("Loaded sample layer is not a vector layer."));
            return;
        }

        worldLayer->setName(QStringLiteral("World - labels"));
        worldLayer->style().setFillColor(QStringLiteral("#D8E5E1"));
        worldLayer->style().setFillOpacity(215);
        worldLayer->style().setLineColor(QStringLiteral("#6F8380"));
        worldLayer->style().setLineWidth(0.8f);
        worldLayer->style().setShowLabels(true);
        worldLayer->style().setLabelField(QStringLiteral("COUNTRY"));
        worldLayer->style().setLabelFontSize(12.0f);
        worldLayer->style().setLabelColor(QStringLiteral("#FFFF00"));
        worldLayer->style().setLabelHaloEnabled(true);
        worldLayer->style().setLabelHaloColor(QStringLiteral("#000000"));
        worldLayer->style().setLabelHaloWidth(2.0f);

        fillLabelFields(*fieldCombo, *worldLayer);
        worldLayer->style().setLabelField(fieldCombo->currentText());

        QObject::connect(showLabelsCheck, &QCheckBox::toggled, &window, [viewer, worldLayer, &window](bool checked)
        {
            worldLayer->style().setShowLabels(checked);
            refreshLabels(*viewer);
            window.statusBar()->showMessage(checked
                ? QStringLiteral("Labels enabled.")
                : QStringLiteral("Labels disabled."));
        });

        QObject::connect(fieldCombo, &QComboBox::currentTextChanged, &window, [viewer, worldLayer, &window](const QString& fieldName)
        {
            worldLayer->style().setLabelField(fieldName);
            refreshLabels(*viewer);
            window.statusBar()->showMessage(QStringLiteral("Label field: %1").arg(fieldName));
        });

        QObject::connect(fontSizeSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), &window, [viewer, worldLayer, &window](double value)
        {
            worldLayer->style().setLabelFontSize(static_cast<float>(value));
            refreshLabels(*viewer);
            window.statusBar()->showMessage(QStringLiteral("Label font size: %1").arg(value, 0, 'f', 1));
        });

        showLabelsCheck->setEnabled(true);
        fieldCombo->setEnabled(true);
        fontSizeSpin->setEnabled(true);
        viewer->setViewExtent(GisExtent(-180.0, -58.0, 180.0, 82.0));
        window.statusBar()->showMessage(QStringLiteral("Labels use showLabels, labelField and labelFontSize."));
    });

    return app.exec();
}
