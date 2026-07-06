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

#include "Layers/GisLayerVector.h"
#include "Shapes/GisExtent.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Viewer;

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
    style.setLabelRotationDegrees(0.0f);
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

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Rotation"), rotationSpin);

    panelLayout->addWidget(new QLabel(QStringLiteral("Label rotation"), panel));
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
            QStringLiteral("LabelRotation"),
            QStringLiteral("Sample data was not found:\n%1").arg(worldPath));
        return 1;
    }

    QString errorMessage;
    if (!viewer->addLayerFromPath(worldPath, &errorMessage))
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("LabelRotation"),
            QStringLiteral("World layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? worldPath : errorMessage));
        return 1;
    }

    auto* worldLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
    if (worldLayer == nullptr)
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("LabelRotation"),
            QStringLiteral("Loaded sample layer is not a vector layer."));
        return 1;
    }

    worldLayer->setName(QStringLiteral("World - label rotation"));
    worldLayer->style() = labeledWorldStyle();

    auto applyRotation = [&]() {
        worldLayer->style().setLabelRotationDegrees(static_cast<float>(rotationSpin->value()));
        refreshLabels(*viewer);
        window.statusBar()->showMessage(
            QStringLiteral("Label rotation: %1 degrees")
                .arg(rotationSpin->value(), 0, 'f', 1));
    };

    QObject::connect(rotationSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), [&](double) {
        applyRotation();
    });

    QObject::connect(resetButton, &QPushButton::clicked, [&]() {
        rotationSpin->setValue(0.0);
        applyRotation();
    });

    viewer->setViewExtent(GisExtent(-180.0, -58.0, 180.0, 82.0));
    window.statusBar()->showMessage(QStringLiteral("Labels use labelRotationDegrees."));

    window.show();
    return app.exec();
}
