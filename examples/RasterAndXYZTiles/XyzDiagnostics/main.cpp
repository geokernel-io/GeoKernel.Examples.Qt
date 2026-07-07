#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QGridLayout>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QSize>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTextEdit>
#include <QToolBar>

#include <memory>

#include "Common/DiagnosticsSnapshot.h"
#include "Raster/Xyz/GisLayerXYZ.h"
#include "Shapes/GisExtent.h"
#include "Viewer/GisViewer.h"

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Formats::Common;
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

    QString cacheDirectory()
    {
        return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("XyzDiagnosticsCache/osm"));
    }

    QString bytesText(quint64 bytes)
    {
        const double mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
        return QStringLiteral("%1 bytes (%2 MiB)")
            .arg(bytes)
            .arg(mib, 0, 'f', 2);
    }

    QString avgText(qint64 totalMs, quint64 count)
    {
        if (count == 0)
        {
            return QStringLiteral("0.00 ms");
        }

        return QStringLiteral("%1 ms").arg(static_cast<double>(totalMs) / static_cast<double>(count), 0, 'f', 2);
    }

    QString detailsText(const GisLayerXYZ& layer)
    {
        const DiagnosticsSnapshot snapshot = layer.provider().diagnosticsSnapshot();
        const quint64 memoryTotal = snapshot.memoryHits + snapshot.memoryMisses;
        const quint64 diskTotal = snapshot.diskHits + snapshot.diskMisses;
        const quint64 downloadTotal = snapshot.downloadsSucceeded + snapshot.downloadsFailed;

        QStringList details;
        details << QStringLiteral("XYZ diagnosticsSnapshot sample");
        details << QStringLiteral("Updated: %1").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")));
        details << QStringLiteral("");
        details << QStringLiteral("Layer");
        details << QStringLiteral("Name: %1").arg(layer.name());
        details << QStringLiteral("URL template: %1").arg(layer.urlTemplate());
        details << QStringLiteral("Tile size: %1").arg(layer.tileSize());
        details << QStringLiteral("Zoom range: %1 - %2").arg(layer.minZoom()).arg(layer.maxZoom());
        details << QStringLiteral("Local cache: %1").arg(layer.localCacheEnabled() ? QStringLiteral("enabled") : QStringLiteral("disabled"));
        details << QStringLiteral("Cache directory: %1").arg(layer.cacheDirectory());
        details << QStringLiteral("");
        details << QStringLiteral("Memory cache");
        details << QStringLiteral("Hits: %1").arg(snapshot.memoryHits);
        details << QStringLiteral("Misses: %1").arg(snapshot.memoryMisses);
        details << QStringLiteral("Total lookups: %1").arg(memoryTotal);
        details << QStringLiteral("");
        details << QStringLiteral("Disk cache");
        details << QStringLiteral("Hits: %1").arg(snapshot.diskHits);
        details << QStringLiteral("Misses: %1").arg(snapshot.diskMisses);
        details << QStringLiteral("Total lookups: %1").arg(diskTotal);
        details << QStringLiteral("Read time total: %1 ms").arg(snapshot.diskReadMs);
        details << QStringLiteral("Decode time total: %1 ms").arg(snapshot.decodeMs);
        details << QStringLiteral("Average read: %1").arg(avgText(snapshot.diskReadMs, snapshot.diskHits));
        details << QStringLiteral("Average decode: %1").arg(avgText(snapshot.decodeMs, snapshot.diskHits));
        details << QStringLiteral("");
        details << QStringLiteral("Network");
        details << QStringLiteral("Downloads started: %1").arg(snapshot.downloadsStarted);
        details << QStringLiteral("Downloads succeeded: %1").arg(snapshot.downloadsSucceeded);
        details << QStringLiteral("Downloads failed: %1").arg(snapshot.downloadsFailed);
        details << QStringLiteral("Downloads completed: %1").arg(downloadTotal);
        details << QStringLiteral("Bytes downloaded: %1").arg(bytesText(snapshot.bytesDownloaded));
        details << QStringLiteral("Download time total: %1 ms").arg(snapshot.downloadMs);
        details << QStringLiteral("Average download: %1").arg(avgText(snapshot.downloadMs, snapshot.downloadsSucceeded));
        details << QStringLiteral("Queue depth: %1").arg(snapshot.networkQueueDepth);
        details << QStringLiteral("Max queue depth: %1").arg(snapshot.maxNetworkQueueDepth);
        details << QStringLiteral("");
        details << QStringLiteral("How to test");
        details << QStringLiteral("- Pan or zoom the map to request new tiles.");
        details << QStringLiteral("- First pass usually increases downloads and disk misses.");
        details << QStringLiteral("- Revisit the same area to see memory/disk cache hits.");

        return details.join(QStringLiteral("\n"));
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("XyzDiagnostics"));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto layer = std::make_unique<GisLayerXYZ>(
        QStringLiteral("OSM Diagnostics"),
        osmTemplate(),
        0,
        19,
        256);
    layer->setAttribution(QStringLiteral("OpenStreetMap"));
    layer->setLocalCacheEnabled(true);
    layer->setCacheDirectory(cacheDirectory());
    layer->open();

    GisLayerXYZ* xyzLayer = layer.get();
    viewer->addLayer(layer);

    auto* detailsView = new QTextEdit(&window);
    detailsView->setReadOnly(true);
    detailsView->setMinimumWidth(390);

    auto* dock = new QDockWidget(QStringLiteral("XYZ diagnostics"), &window);
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
    QAction* refreshAction = toolbar->addAction(QStringLiteral("Refresh Stats"));

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

    const auto refreshDiagnostics = [xyzLayer, detailsView, &window]
    {
        if (xyzLayer == nullptr)
        {
            detailsView->setPlainText(QStringLiteral("XYZ layer is not available."));
            return;
        }

        detailsView->setPlainText(detailsText(*xyzLayer));
        const DiagnosticsSnapshot snapshot = xyzLayer->provider().diagnosticsSnapshot();
        window.statusBar()->showMessage(QStringLiteral("XYZ diagnostics: %1 requests, %2 downloads, %3 disk hits, %4 memory hits")
            .arg(snapshot.downloadsStarted)
            .arg(snapshot.downloadsSucceeded)
            .arg(snapshot.diskHits)
            .arg(snapshot.memoryHits));
    };

    QObject::connect(refreshAction, &QAction::triggered, &window, refreshDiagnostics);

    auto* timer = new QTimer(&window);
    timer->setInterval(750);
    QObject::connect(timer, &QTimer::timeout, &window, refreshDiagnostics);
    timer->start();

    panAction->setChecked(true);
    refreshDiagnostics();

    window.show();
    viewer->setViewExtent(defaultExtent3857());

    return app.exec();
}
