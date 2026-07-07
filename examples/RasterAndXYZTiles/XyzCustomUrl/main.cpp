#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QSize>
#include <QSpinBox>
#include <QStatusBar>
#include <QString>
#include <QTextEdit>
#include <QToolBar>

#include <memory>

#include "Raster/Xyz/GisLayerXYZ.h"
#include "Shapes/GisExtent.h"
#include "Viewer/GisViewer.h"

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Formats::Raster::Xyz;
using namespace GeoKernel::Viewer;

namespace
{
    GisExtent defaultExtent3857()
    {
        return GisExtent(
            -1400000.0,
            4100000.0,
            4200000.0,
            7800000.0);
    }

    bool isSupportedTileTemplate(const QString& urlTemplate)
    {
        const bool hasXyz =
            urlTemplate.contains(QStringLiteral("{z}")) &&
            urlTemplate.contains(QStringLiteral("{x}")) &&
            urlTemplate.contains(QStringLiteral("{y}"));
        const bool hasQuadKey = urlTemplate.contains(QStringLiteral("{q}"));

        return hasXyz || hasQuadKey;
    }

    QString layerDetails(
        const QString& urlTemplate,
        int minZoom,
        int maxZoom,
        bool cacheEnabled)
    {
        QStringList details;
        details << QStringLiteral("Custom XYZ URL sample");
        details << QStringLiteral("");
        details << QStringLiteral("Active URL template:");
        details << urlTemplate;
        details << QStringLiteral("");
        details << QStringLiteral("Min zoom: %1").arg(minZoom);
        details << QStringLiteral("Max zoom: %1").arg(maxZoom);
        details << QStringLiteral("Tile size: 256");
        details << QStringLiteral("Local cache: %1").arg(cacheEnabled ? QStringLiteral("enabled") : QStringLiteral("disabled"));
        details << QStringLiteral("");
        details << QStringLiteral("SDK flow:");
        details << QStringLiteral("auto layer = std::make_unique<GisLayerXYZ>(\"Custom XYZ\", QString());");
        details << QStringLiteral("layer->setUrlTemplate(urlTemplate);");
        details << QStringLiteral("layer->setMinZoom(minZoom);");
        details << QStringLiteral("layer->setMaxZoom(maxZoom);");
        details << QStringLiteral("layer->open();");
        details << QStringLiteral("viewer.addLayer(layer);");
        details << QStringLiteral("");
        details << QStringLiteral("Template requirements:");
        details << QStringLiteral("- XYZ: {z}, {x}, {y}");
        details << QStringLiteral("- or Bing style: {q}");

        return details.join(QStringLiteral("\n"));
    }

    void applyCustomUrl(
        GisViewer& viewer,
        QTextEdit& detailsView,
        QStatusBar& statusBar,
        const QString& urlTemplate,
        int minZoom,
        int maxZoom,
        bool cacheEnabled)
    {
        const QString trimmedUrl = urlTemplate.trimmed();
        if (!isSupportedTileTemplate(trimmedUrl))
        {
            QMessageBox::warning(
                &viewer,
                QStringLiteral("XyzCustomUrl"),
                QStringLiteral("Tile URL template must include {z}, {x}, and {y}, or Bing-style {q}."));
            return;
        }

        viewer.clearLayers();

        try
        {
            auto layer = std::make_unique<GisLayerXYZ>(QStringLiteral("Custom XYZ"), QString());
            layer->setUrlTemplate(trimmedUrl);
            layer->setMinZoom(minZoom);
            layer->setMaxZoom(maxZoom);
            layer->setTileSize(256);
            layer->setLocalCacheEnabled(cacheEnabled);
            layer->open();

            viewer.addLayer(layer);
            viewer.setViewExtent(defaultExtent3857());

            detailsView.setPlainText(layerDetails(trimmedUrl, minZoom, maxZoom, cacheEnabled));
            statusBar.showMessage(QStringLiteral("Custom XYZ URL applied."));
        }
        catch (const std::exception& ex)
        {
            QMessageBox::critical(
                &viewer,
                QStringLiteral("XyzCustomUrl"),
                QStringLiteral("Custom XYZ layer could not be loaded:\n%1").arg(QString::fromUtf8(ex.what())));
        }
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1280, 800);
    window.setWindowTitle(QStringLiteral("XyzCustomUrl"));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* detailsDock = new QTextEdit(&window);
    detailsDock->setReadOnly(true);
    detailsDock->setMinimumWidth(360);
    auto* dock = new QDockWidget(QStringLiteral("Custom XYZ details"), &window);
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
    toolbar->addWidget(new QLabel(QStringLiteral("URL:"), toolbar));

    auto* urlEdit = new QLineEdit(toolbar);
    urlEdit->setMinimumWidth(470);
    urlEdit->setText(QStringLiteral("https://tile.openstreetmap.org/{z}/{x}/{y}.png"));
    toolbar->addWidget(urlEdit);

    toolbar->addWidget(new QLabel(QStringLiteral("Min:"), toolbar));
    auto* minZoomSpin = new QSpinBox(toolbar);
    minZoomSpin->setRange(0, 21);
    minZoomSpin->setValue(0);
    toolbar->addWidget(minZoomSpin);

    toolbar->addWidget(new QLabel(QStringLiteral("Max:"), toolbar));
    auto* maxZoomSpin = new QSpinBox(toolbar);
    maxZoomSpin->setRange(0, 21);
    maxZoomSpin->setValue(19);
    toolbar->addWidget(maxZoomSpin);

    auto* cacheCheck = new QCheckBox(QStringLiteral("Local cache"), toolbar);
    cacheCheck->setChecked(true);
    toolbar->addWidget(cacheCheck);

    QAction* applyAction = toolbar->addAction(QStringLiteral("Apply URL"));

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

    const auto applyUrl = [viewer, detailsDock, urlEdit, minZoomSpin, maxZoomSpin, cacheCheck, &window]
    {
        applyCustomUrl(
            *viewer,
            *detailsDock,
            *window.statusBar(),
            urlEdit->text(),
            minZoomSpin->value(),
            maxZoomSpin->value(),
            cacheCheck->isChecked());
    };

    QObject::connect(applyAction, &QAction::triggered, &window, applyUrl);
    QObject::connect(urlEdit, &QLineEdit::returnPressed, &window, applyUrl);

    panAction->setChecked(true);

    applyUrl();

    window.show();
    viewer->setViewExtent(defaultExtent3857());

    return app.exec();
}
