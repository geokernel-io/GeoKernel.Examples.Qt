#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
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

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;

QString sampleDataPath(const QString& fileName)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/data/%1").arg(fileName)));
}

GisLayerStyle labeledWorldStyle()
{
    GisLayerStyle style;
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
    style.setLabelHaloWidth(2.5f);
    return style;
}

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

    auto* form = new QFormLayout;
    form->addRow(haloEnabledCheck);
    form->addRow(QStringLiteral("Halo color"), haloColorCombo);
    form->addRow(QStringLiteral("Halo width"), haloWidthSpin);

    panelLayout->addWidget(new QLabel(QStringLiteral("Label halo"), panel));
    panelLayout->addLayout(form);
    panelLayout->addStretch(1);

    auto* viewer = new GisViewer(central);
    viewer->setMapBackgroundColor(QColor(247, 248, 250));
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(panel);
    layout->addWidget(viewer, 1);
    window.setCentralWidget(central);

    const QString worldPath = sampleDataPath(QStringLiteral("shapefile/world_4326.shp"));
    if (!QFileInfo::exists(worldPath))
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("LabelHalo"),
            QStringLiteral("Sample data was not found:\n%1").arg(worldPath));
        return 1;
    }

    QString errorMessage;
    if (!viewer->addLayerFromPath(worldPath, &errorMessage))
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("LabelHalo"),
            QStringLiteral("World layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? worldPath : errorMessage));
        return 1;
    }

    auto* worldLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
    if (worldLayer == nullptr)
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("LabelHalo"),
            QStringLiteral("Loaded sample layer is not a vector layer."));
        return 1;
    }

    worldLayer->setName(QStringLiteral("World - label halo"));
    worldLayer->style() = labeledWorldStyle();
    worldLayer->style().setLabelHaloColor(currentHaloColor(*haloColorCombo));

    QObject::connect(haloEnabledCheck, &QCheckBox::toggled, [&](bool checked) {
        worldLayer->style().setLabelHaloEnabled(checked);
        refreshLabels(*viewer);
        window.statusBar()->showMessage(checked
            ? QStringLiteral("Label halo enabled.")
            : QStringLiteral("Label halo disabled."));
    });

    QObject::connect(haloColorCombo, &QComboBox::currentIndexChanged, [&](int index) {
        const QString color = haloColorCombo->itemData(index).toString();
        worldLayer->style().setLabelHaloColor(color);
        refreshLabels(*viewer);
        window.statusBar()->showMessage(QStringLiteral("Label halo color: %1").arg(color));
    });

    QObject::connect(haloWidthSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), [&](double value) {
        worldLayer->style().setLabelHaloWidth(static_cast<float>(value));
        refreshLabels(*viewer);
        window.statusBar()->showMessage(QStringLiteral("Label halo width: %1").arg(value, 0, 'f', 1));
    });

    viewer->setViewExtent(GisExtent(-180.0, -58.0, 180.0, 82.0));
    window.statusBar()->showMessage(QStringLiteral("Labels use labelHaloEnabled, labelHaloColor and labelHaloWidth."));

    window.show();

    return app.exec();
}
