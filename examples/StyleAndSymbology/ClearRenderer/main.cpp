#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
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

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Symbology;

QIcon sampleIcon(const QString& fileName)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString path = QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/images/%1").arg(fileName)));
    QIcon icon;

    for (const auto mode : { QIcon::Normal, QIcon::Active, QIcon::Selected, QIcon::Disabled })
    {
        icon.addFile(path, QSize(), mode, QIcon::Off);
        icon.addFile(path, QSize(), mode, QIcon::On);
    }

    return icon;
}

QString sampleDataPath(const QString& fileName)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/data/%1").arg(fileName)));
}

GisLayerStyle defaultStateStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#D8E5E1"));
    style.setFillOpacity(220);
    style.setLineColor(QStringLiteral("#536B68"));
    style.setLineWidth(0.9f);
    return style;
}

bool applyCategorizedRenderer(GisLayerVector& layer)
{
    auto renderer = GisSymbolRendererFactory::createCategorizedRenderer(
        layer,
        QStringLiteral("STATE"),
        GisColorRampRegistry::ramp(QStringLiteral("Unique")),
        defaultStateStyle(),
        64);

    if (!renderer)
        return false;

    layer.setSymbolRenderer(std::move(renderer));
    return true;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon(QStringLiteral("GeoKernelAppIcon.ico")));

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
    auto* stateLabel = new QLabel(QStringLiteral("Renderer: categorized by STATE"), toolbar);
    toolbarLayout->addWidget(applyButton);
    toolbarLayout->addWidget(clearButton);
    toolbarLayout->addWidget(stateLabel);
    toolbarLayout->addStretch(1);

    auto* viewer = new GisViewer(central);
    viewer->setMapBackgroundColor(QColor(247, 248, 250));
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(toolbar);
    layout->addWidget(viewer, 1);
    window.setCentralWidget(central);

    const QString path = sampleDataPath(QStringLiteral("shapefile/usa_states_3857.shp"));
    if (!QFileInfo::exists(path))
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("ClearRenderer"),
            QStringLiteral("Sample shapefile was not found:\n%1").arg(path));
        return 1;
    }

    QString errorMessage;
    if (!viewer->addLayerFromPath(path, &errorMessage))
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("ClearRenderer"),
            QStringLiteral("Layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? path : errorMessage));
        return 1;
    }

    auto* statesLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
    if (statesLayer == nullptr)
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("ClearRenderer"),
            QStringLiteral("Loaded layer is not a vector layer."));
        return 1;
    }

    statesLayer->setName(QStringLiteral("USA States"));
    statesLayer->style() = defaultStateStyle();

    if (!applyCategorizedRenderer(*statesLayer))
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("ClearRenderer"),
            QStringLiteral("Could not create categorized renderer from STATE field."));
        return 1;
    }    

    QObject::connect(applyButton, &QPushButton::clicked, [&]() {
        if (applyCategorizedRenderer(*statesLayer))
        {
            stateLabel->setText(QStringLiteral("Renderer: categorized by STATE"));
            viewer->invalidateRenderCache(true, true);
            viewer->update();
            window.statusBar()->showMessage(QStringLiteral("Categorized renderer applied."));
        }
    });

    QObject::connect(clearButton, &QPushButton::clicked, [&]() {
        statesLayer->clearSymbolRenderer();
        stateLabel->setText(QStringLiteral("Renderer: none, default layer style"));
        viewer->invalidateRenderCache(true, true);
        viewer->update();
        window.statusBar()->showMessage(QStringLiteral("Symbol renderer cleared. Layer is back to default style."));
    });

    window.statusBar()->showMessage(QStringLiteral("Categorized renderer applied. Use Clear Renderer to return to default style."));
    window.show();

    viewer->setViewExtent(GisExtent(-16831516.0, 1856556.0, -4631023.0, 7472472.0));

    return app.exec();
}
