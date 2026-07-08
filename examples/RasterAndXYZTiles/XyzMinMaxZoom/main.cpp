#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSize>
#include <QSpinBox>
#include <QStatusBar>
#include <QString>
#include <QTextEdit>
#include <QToolBar>

#include <algorithm>
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

    QString osmTemplate()
    {
        return QStringLiteral("https://tile.openstreetmap.org/{z}/{x}/{y}.png");
    }

    QString cacheDirectoryFor(int minZoom, int maxZoom)
    {
        return QDir(QCoreApplication::applicationDirPath()).filePath(
            QStringLiteral("XyzMinMaxZoomCache/%1_%2").arg(minZoom).arg(maxZoom));
    }

    QString detailsText(int minZoom, int maxZoom)
    {
        QStringList details;
        details << QStringLiteral("XYZ min/max zoom sample");
        details << QStringLiteral("");
        details << QStringLiteral("URL template:");
        details << osmTemplate();
        details << QStringLiteral("");
        details << QStringLiteral("Applied range:");
        details << QStringLiteral("Min zoom: %1").arg(minZoom);
        details << QStringLiteral("Max zoom: %1").arg(maxZoom);
        details << QStringLiteral("");
        details << QStringLiteral("What it demonstrates:");
        details << QStringLiteral("- setMinZoom limits the lowest tile zoom level.");
        details << QStringLiteral("- setMaxZoom limits the highest tile zoom level.");
        details << QStringLiteral("- Values are clamped by GisLayerXYZ to the safe internal range.");
        details << QStringLiteral("- If min is greater than max, this sample normalizes the range before applying it.");
        details << QStringLiteral("");
        details << QStringLiteral("SDK flow:");
        details << QStringLiteral("auto layer = std::make_unique<GisLayerXYZ>(name, urlTemplate);");
        details << QStringLiteral("layer->setMinZoom(minZoom);");
        details << QStringLiteral("layer->setMaxZoom(maxZoom);");
        details << QStringLiteral("layer->open();");
        details << QStringLiteral("viewer.addLayer(layer);");

        return details.join(QStringLiteral("\n"));
    }

    void applyZoomRange(
        GisViewer& viewer,
        QTextEdit& detailsView,
        QStatusBar& statusBar,
        int minZoom,
        int maxZoom)
    {
        if (minZoom > maxZoom)
            std::swap(minZoom, maxZoom);

        const GisExtent previousExtent = viewer.layerCount() > 0 ? viewer.viewExtent() : defaultExtent3857();
        viewer.clearLayers();

        auto layer = std::make_unique<GisLayerXYZ>(
            QStringLiteral("OSM min %1 max %2").arg(minZoom).arg(maxZoom),
            osmTemplate(),
            0,
            19,
            256,
            QStringLiteral("OpenStreetMap contributors"));
        layer->setMinZoom(minZoom);
        layer->setMaxZoom(maxZoom);
        layer->setLocalCacheEnabled(true);
        layer->setCacheDirectory(cacheDirectoryFor(minZoom, maxZoom));
        layer->open();

        viewer.addLayer(layer);
        viewer.setViewExtent(previousExtent);

        detailsView.setPlainText(detailsText(minZoom, maxZoom));
        statusBar.showMessage(QStringLiteral("XYZ min/max zoom applied: %1 - %2").arg(minZoom).arg(maxZoom));
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("XyzMinMaxZoom"));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* detailsView = new QTextEdit(&window);
    detailsView->setReadOnly(true);
    detailsView->setMinimumWidth(350);
    auto* dock = new QDockWidget(QStringLiteral("Min/max zoom details"), &window);
    dock->setWidget(detailsView);
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

    QAction* applyAction = toolbar->addAction(QStringLiteral("Apply Zoom Range"));
    auto* lowRangeButton = new QPushButton(QStringLiteral("0-5"), toolbar);
    auto* midRangeButton = new QPushButton(QStringLiteral("4-10"), toolbar);
    auto* highRangeButton = new QPushButton(QStringLiteral("8-14"), toolbar);
    toolbar->addWidget(lowRangeButton);
    toolbar->addWidget(midRangeButton);
    toolbar->addWidget(highRangeButton);

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

    const auto applyRange = [viewer, detailsView, minZoomSpin, maxZoomSpin, &window]
    {
        applyZoomRange(
            *viewer,
            *detailsView,
            *window.statusBar(),
            minZoomSpin->value(),
            maxZoomSpin->value());
    };

    QObject::connect(applyAction, &QAction::triggered, &window, applyRange);
    QObject::connect(lowRangeButton, &QPushButton::clicked, &window, [minZoomSpin, maxZoomSpin, applyRange]
    {
        minZoomSpin->setValue(0);
        maxZoomSpin->setValue(5);
        applyRange();
    });
    QObject::connect(midRangeButton, &QPushButton::clicked, &window, [minZoomSpin, maxZoomSpin, applyRange]
    {
        minZoomSpin->setValue(4);
        maxZoomSpin->setValue(10);
        applyRange();
    });
    QObject::connect(highRangeButton, &QPushButton::clicked, &window, [minZoomSpin, maxZoomSpin, applyRange]
    {
        minZoomSpin->setValue(8);
        maxZoomSpin->setValue(14);
        applyRange();
    });

    panAction->setChecked(true);

    applyRange();

    window.show();
    viewer->setViewExtent(defaultExtent3857());

    return app.exec();
}
