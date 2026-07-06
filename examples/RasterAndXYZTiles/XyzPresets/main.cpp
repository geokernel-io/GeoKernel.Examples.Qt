#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QSize>
#include <QStatusBar>
#include <QString>
#include <QTextEdit>
#include <QToolBar>

#include <memory>

#include "Viewer/GisViewer.h"
#include "Shapes/GisExtent.h"
#include "Raster/Xyz/GisLayerXYZ.h"
#include "Raster/Xyz/PredefinedXyzLayers.h"
#include "Raster/Xyz/XyzLayerPreset.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Formats::Raster::Xyz;

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

GisExtent defaultExtent3857()
{
    return GisExtent(
        -1400000.0,
        4100000.0,
        4200000.0,
        7800000.0);
}

QString presetDetails(const XyzLayerPreset& preset, int presetCount, bool cacheEnabled)
{
    QStringList details;
    details << QStringLiteral("XYZ preset layer");
    details << QStringLiteral("");
    details << QStringLiteral("Preset count: %1").arg(presetCount);
    details << QStringLiteral("Selected: %1").arg(preset.name);
    details << QStringLiteral("");
    details << QStringLiteral("URL template:");
    details << preset.urlTemplate;
    details << QStringLiteral("");
    details << QStringLiteral("Min zoom: %1").arg(preset.minZoom);
    details << QStringLiteral("Max zoom: %1").arg(preset.maxZoom);
    details << QStringLiteral("Tile size: %1").arg(preset.tileSize);
    details << QStringLiteral("Local cache: %1").arg(cacheEnabled ? QStringLiteral("enabled") : QStringLiteral("disabled"));

    if (!preset.attribution.trimmed().isEmpty())
    {
        details << QStringLiteral("");
        details << QStringLiteral("Attribution:");
        details << preset.attribution;
    }

    details << QStringLiteral("");
    details << QStringLiteral("The sample creates the layer from:");
    details << QStringLiteral("predefinedXyzLayerPresets()");
    details << QStringLiteral("GisLayerXYZ(name, urlTemplate, minZoom, maxZoom, tileSize, attribution)");

    return details.join(QStringLiteral("\n"));
}

void addPresetLayer(GisViewer& viewer, QTextEdit& detailsView, QStatusBar& statusBar, const QVector<XyzLayerPreset>& presets, int presetIndex, bool cacheEnabled)
{
    if (presetIndex < 0 || presetIndex >= presets.size())
        return;

    const XyzLayerPreset& preset = presets[presetIndex];

    viewer.clearLayers();

    try
    {
        auto layer = std::make_unique<GisLayerXYZ>(
            preset.name,
            preset.urlTemplate,
            preset.minZoom,
            preset.maxZoom,
            preset.tileSize,
            preset.attribution);
        layer->setLocalCacheEnabled(cacheEnabled);
        layer->open();

        viewer.addLayer(layer);
        viewer.setViewExtent(defaultExtent3857());

        detailsView.setPlainText(presetDetails(preset, presets.size(), cacheEnabled));
        statusBar.showMessage(QStringLiteral("XYZ preset loaded: %1").arg(preset.name));
    }
    catch (const std::exception& ex)
    {
        QMessageBox::critical(
            &viewer,
            QStringLiteral("XyzPresets"),
            QStringLiteral("XYZ preset could not be loaded:\n%1").arg(QString::fromUtf8(ex.what())));
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("XyzPresets"));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* detailsDock = new QTextEdit(&window);
    detailsDock->setReadOnly(true);
    detailsDock->setMinimumWidth(330);
    auto* dock = new QDockWidget(QStringLiteral("XYZ preset details"), &window);
    dock->setWidget(detailsDock);
    window.addDockWidget(Qt::RightDockWidgetArea, dock);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QAction* zoomInAction = toolbar->addAction(sampleIcon(QStringLiteral("ZoomIn.svg")), QStringLiteral("Zoom In"));
    QAction* zoomOutAction = toolbar->addAction(sampleIcon(QStringLiteral("ZoomOut.svg")), QStringLiteral("Zoom Out"));
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));
    toolbar->addSeparator();

    QActionGroup toolGroup(&window);
    toolGroup.setExclusive(true);

    QAction* zoomRectAction = toolbar->addAction(sampleIcon(QStringLiteral("RectangularZoom.svg")), QStringLiteral("Zoom Rect"));
    zoomRectAction->setCheckable(true);
    toolGroup.addAction(zoomRectAction);

    QAction* panAction = toolbar->addAction(sampleIcon(QStringLiteral("Pan.svg")), QStringLiteral("Pan"));
    panAction->setCheckable(true);
    toolGroup.addAction(panAction);

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Preset:"), toolbar));

    auto* presetCombo = new QComboBox(toolbar);
    presetCombo->setMinimumWidth(260);
    const QVector<XyzLayerPreset> presets = predefinedXyzLayerPresets();
    for (int i = 0; i < presets.size(); ++i)
        presetCombo->addItem(presets[i].name, i);
    toolbar->addWidget(presetCombo);

    auto* cacheCheck = new QCheckBox(QStringLiteral("Local cache"), toolbar);
    cacheCheck->setChecked(true);
    toolbar->addWidget(cacheCheck);

    QObject::connect(zoomInAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->zoomIn();
    });

    QObject::connect(zoomOutAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->zoomOut();
    });

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setViewExtent(defaultExtent3857());
    });

    QObject::connect(zoomRectAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setActiveTool(GisViewerTool::ZoomBox);
    });

    QObject::connect(panAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setActiveTool(GisViewerTool::Pan);
    });

    const auto reloadPreset = [viewer, detailsDock, presetCombo, cacheCheck, &window, presets]
    {
        addPresetLayer(
            *viewer,
            *detailsDock,
            *window.statusBar(),
            presets,
            presetCombo->currentData().toInt(),
            cacheCheck->isChecked());
    };

    QObject::connect(presetCombo, &QComboBox::currentIndexChanged, &window, reloadPreset);
    QObject::connect(cacheCheck, &QCheckBox::toggled, &window, reloadPreset);

    panAction->setChecked(true);

    const int osmIndex = presetCombo->findText(QStringLiteral("OpenStreetMap"));
    if (osmIndex >= 0)
        presetCombo->setCurrentIndex(osmIndex);

    reloadPreset();

    window.show();
    viewer->setViewExtent(defaultExtent3857());

    return app.exec();
}
