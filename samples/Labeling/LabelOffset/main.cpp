#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
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
    style.setLabelHaloWidth(2.0f);
    style.setLabelOffsetX(0.0f);
    style.setLabelOffsetY(0.0f);
    return style;
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
    window.setWindowTitle(QStringLiteral("LabelOffset"));

    auto* central = new QWidget(&window);
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* panel = new QWidget(central);
    panel->setFixedWidth(230);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(8, 8, 8, 8);

    auto* offsetXSpin = new QDoubleSpinBox(panel);
    offsetXSpin->setRange(-80.0, 80.0);
    offsetXSpin->setDecimals(1);
    offsetXSpin->setSingleStep(2.0);
    offsetXSpin->setValue(0.0);

    auto* offsetYSpin = new QDoubleSpinBox(panel);
    offsetYSpin->setRange(-80.0, 80.0);
    offsetYSpin->setDecimals(1);
    offsetYSpin->setSingleStep(2.0);
    offsetYSpin->setValue(0.0);

    auto* resetButton = new QPushButton(QStringLiteral("Reset Offset"), panel);

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Offset X"), offsetXSpin);
    form->addRow(QStringLiteral("Offset Y"), offsetYSpin);

    panelLayout->addWidget(new QLabel(QStringLiteral("Label offset"), panel));
    panelLayout->addLayout(form);
    panelLayout->addWidget(resetButton);
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
            QStringLiteral("LabelOffset"),
            QStringLiteral("Sample data was not found:\n%1").arg(worldPath));
        return 1;
    }

    QString errorMessage;
    if (!viewer->addLayerFromPath(worldPath, &errorMessage))
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("LabelOffset"),
            QStringLiteral("World layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? worldPath : errorMessage));
        return 1;
    }

    auto* worldLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
    if (worldLayer == nullptr)
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("LabelOffset"),
            QStringLiteral("Loaded sample layer is not a vector layer."));
        return 1;
    }

    worldLayer->setName(QStringLiteral("World - label offset"));
    worldLayer->style() = labeledWorldStyle();

    auto applyOffset = [&]() {
        worldLayer->style().setLabelOffsetX(static_cast<float>(offsetXSpin->value()));
        worldLayer->style().setLabelOffsetY(static_cast<float>(offsetYSpin->value()));
        refreshLabels(*viewer);
        window.statusBar()->showMessage(
            QStringLiteral("Label offset X: %1, Y: %2")
                .arg(offsetXSpin->value(), 0, 'f', 1)
                .arg(offsetYSpin->value(), 0, 'f', 1));
    };

    QObject::connect(offsetXSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), [&](double) {
        applyOffset();
    });

    QObject::connect(offsetYSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), [&](double) {
        applyOffset();
    });

    QObject::connect(resetButton, &QPushButton::clicked, [&]() {
        offsetXSpin->setValue(0.0);
        offsetYSpin->setValue(0.0);
        applyOffset();
    });

    viewer->setViewExtent(GisExtent(-180.0, -58.0, 180.0, 82.0));
    window.statusBar()->showMessage(QStringLiteral("Labels use labelOffsetX and labelOffsetY."));

    window.show();
    return app.exec();
}
