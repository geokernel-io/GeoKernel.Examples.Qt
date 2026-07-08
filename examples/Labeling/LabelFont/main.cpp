#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#include "Layers/GisLayerVector.h"
#include "Shapes/GisExtent.h"
#include "Viewer/GisViewer.h"

#include "Helpers.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;

void refreshLabels(GisViewer& viewer)
{
    viewer.invalidateRenderCache(true, true);
    viewer.update();
}

void selectFontFamily(QComboBox& combo, const QString& family)
{
    const int index = combo.findText(family);
    if (index >= 0)
        combo.setCurrentIndex(index);
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("LabelFont"));

    auto* central = new QWidget(&window);
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* panel = new QWidget(central);
    panel->setFixedWidth(245);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(8, 8, 8, 8);

    auto* fontFamilyCombo = new QComboBox(panel);
    fontFamilyCombo->addItems(QFontDatabase::families());
    selectFontFamily(*fontFamilyCombo, QStringLiteral("Arial"));

    auto* boldCheck = new QCheckBox(QStringLiteral("Bold"), panel);
    auto* italicCheck = new QCheckBox(QStringLiteral("Italic"), panel);
    fontFamilyCombo->setEnabled(false);
    boldCheck->setEnabled(false);
    italicCheck->setEnabled(false);

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Font family"), fontFamilyCombo);
    form->addRow(boldCheck);
    form->addRow(italicCheck);

    panelLayout->addWidget(new QLabel(QStringLiteral("Label font"), panel));
    panelLayout->addLayout(form);
    panelLayout->addStretch(1);

    auto* viewer = new GisViewer(central);
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(panel);
    layout->addWidget(viewer, 1);
    window.setCentralWidget(central);

    window.show();

    QMetaObject::invokeMethod(&window, [&window, viewer, fontFamilyCombo, boldCheck, italicCheck]
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
                QStringLiteral("LabelFont"),
                QStringLiteral("World layer could not be loaded:\n%1")
                    .arg(errorMessage.isEmpty() ? worldPath : errorMessage));
            return;
        }

        auto* worldLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
        if (worldLayer == nullptr)
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("LabelFont"),
                QStringLiteral("Loaded sample layer is not a vector layer."));
            return;
        }

        worldLayer->setName(QStringLiteral("World - label font"));
        auto& style = worldLayer->style();
        style.setFillColor(QStringLiteral("#D8E5E1"));
        style.setFillOpacity(215);
        style.setLineColor(QStringLiteral("#6F8380"));
        style.setLineWidth(0.8f);
        style.setShowLabels(true);
        style.setLabelField(QStringLiteral("COUNTRY"));
        style.setLabelFontSize(12.0f);
        style.setLabelColor(QStringLiteral("#1F2933"));
        style.setLabelHaloEnabled(true);
        style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
        style.setLabelHaloWidth(2.0f);
        style.setLabelFontFamily(fontFamilyCombo->currentText());
        style.setLabelBold(boldCheck->isChecked());
        style.setLabelItalic(italicCheck->isChecked());

        QObject::connect(fontFamilyCombo, &QComboBox::currentTextChanged, &window, [viewer, worldLayer, &window](const QString& family) {
            worldLayer->style().setLabelFontFamily(family);
            refreshLabels(*viewer);
            window.statusBar()->showMessage(QStringLiteral("Label font family: %1").arg(family));
        });

        QObject::connect(boldCheck, &QCheckBox::toggled, &window, [viewer, worldLayer, &window](bool checked) {
            worldLayer->style().setLabelBold(checked);
            refreshLabels(*viewer);
            window.statusBar()->showMessage(checked
                ? QStringLiteral("Label bold enabled.")
                : QStringLiteral("Label bold disabled."));
        });

        QObject::connect(italicCheck, &QCheckBox::toggled, &window, [viewer, worldLayer, &window](bool checked) {
            worldLayer->style().setLabelItalic(checked);
            refreshLabels(*viewer);
            window.statusBar()->showMessage(checked
                ? QStringLiteral("Label italic enabled.")
                : QStringLiteral("Label italic disabled."));
        });

        fontFamilyCombo->setEnabled(true);
        boldCheck->setEnabled(true);
        italicCheck->setEnabled(true);
        viewer->setViewExtent(GisExtent(-180.0, -58.0, 180.0, 82.0));
        window.statusBar()->showMessage(QStringLiteral("Labels use labelFontFamily, labelBold and labelItalic."));
    });

    return app.exec();
}
