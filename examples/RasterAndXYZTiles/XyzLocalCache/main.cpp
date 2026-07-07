#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QFileDialog>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QSize>
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
    struct CacheStats
    {
        int tileFiles = 0;
        qint64 bytes = 0;
    };

    GisExtent defaultExtent3857()
    {
        return GisExtent(
            -1400000.0,
            4100000.0,
            4200000.0,
            7800000.0);
    }

    QString defaultCacheDirectory()
    {
        return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("XyzLocalCacheData/osm"));
    }

    CacheStats readCacheStats(const QString& cacheDirectory)
    {
        CacheStats stats;
        QDirIterator iterator(
            cacheDirectory,
            QStringList() << QStringLiteral("*.tile"),
            QDir::Files,
            QDirIterator::Subdirectories);

        while (iterator.hasNext())
        {
            iterator.next();
            ++stats.tileFiles;
            stats.bytes += iterator.fileInfo().size();
        }

        return stats;
    }

    QString formatBytes(qint64 bytes)
    {
        const double kb = static_cast<double>(bytes) / 1024.0;
        const double mb = kb / 1024.0;
        if (mb >= 1.0)
            return QStringLiteral("%1 MB").arg(mb, 0, 'f', 2);
        if (kb >= 1.0)
            return QStringLiteral("%1 KB").arg(kb, 0, 'f', 1);
        return QStringLiteral("%1 bytes").arg(bytes);
    }

    QString detailsText(
        const QString& urlTemplate,
        const QString& cacheDirectory,
        bool cacheEnabled)
    {
        const CacheStats stats = readCacheStats(cacheDirectory);

        QStringList details;
        details << QStringLiteral("XYZ local cache sample");
        details << QStringLiteral("");
        details << QStringLiteral("URL template:");
        details << urlTemplate;
        details << QStringLiteral("");
        details << QStringLiteral("Local cache: %1").arg(cacheEnabled ? QStringLiteral("enabled") : QStringLiteral("disabled"));
        details << QStringLiteral("Configured cache directory:");
        details << QDir::toNativeSeparators(cacheDirectory);
        details << QStringLiteral("");
        details << QStringLiteral("Cache contents:");
        details << QStringLiteral("Tile files: %1").arg(stats.tileFiles);
        details << QStringLiteral("Size: %1").arg(formatBytes(stats.bytes));
        details << QStringLiteral("");
        details << QStringLiteral("SDK flow:");
        details << QStringLiteral("auto layer = std::make_unique<GisLayerXYZ>(name, urlTemplate);");
        details << QStringLiteral("layer->setLocalCacheEnabled(enabled);");
        details << QStringLiteral("layer->setCacheDirectory(cacheDirectory);");
        details << QStringLiteral("layer->open();");
        details << QStringLiteral("viewer.addLayer(layer);");
        details << QStringLiteral("");
        details << QStringLiteral("Pan or zoom the map to request tiles. When local cache is enabled, downloaded tiles are stored under the configured directory and reused on later runs.");

        return details.join(QStringLiteral("\n"));
    }

    void updateDetails(
        QTextEdit& detailsView,
        const QString& urlTemplate,
        const QString& cacheDirectory,
        bool cacheEnabled)
    {
        detailsView.setPlainText(detailsText(urlTemplate, cacheDirectory, cacheEnabled));
    }

    void applyLayer(
        GisViewer& viewer,
        QTextEdit& detailsView,
        QStatusBar& statusBar,
        const QString& urlTemplate,
        const QString& cacheDirectory,
        bool cacheEnabled)
    {
        viewer.clearLayers();

        try
        {
            auto layer = std::make_unique<GisLayerXYZ>(
                QStringLiteral("OSM with Local Cache"),
                urlTemplate,
                0,
                19,
                256,
                QStringLiteral("OpenStreetMap contributors"));
            layer->setLocalCacheEnabled(cacheEnabled);
            layer->setCacheDirectory(cacheDirectory);
            layer->open();

            viewer.addLayer(layer);
            viewer.setViewExtent(defaultExtent3857());

            updateDetails(detailsView, urlTemplate, cacheDirectory, cacheEnabled);
            statusBar.showMessage(cacheEnabled
                ? QStringLiteral("XYZ layer loaded with local disk cache.")
                : QStringLiteral("XYZ layer loaded with local cache disabled."));
        }
        catch (const std::exception& ex)
        {
            QMessageBox::critical(
                &viewer,
                QStringLiteral("XyzLocalCache"),
                QStringLiteral("XYZ layer could not be loaded:\n%1").arg(QString::fromUtf8(ex.what())));
        }
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("XyzLocalCache"));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* detailsView = new QTextEdit(&window);
    detailsView->setReadOnly(true);
    detailsView->setMinimumWidth(360);
    auto* dock = new QDockWidget(QStringLiteral("Local cache details"), &window);
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

    auto* cacheCheck = new QCheckBox(QStringLiteral("Local cache"), toolbar);
    cacheCheck->setChecked(true);
    toolbar->addWidget(cacheCheck);

    toolbar->addWidget(new QLabel(QStringLiteral("Cache:"), toolbar));
    auto* cacheEdit = new QLineEdit(toolbar);
    cacheEdit->setMinimumWidth(360);
    cacheEdit->setText(QDir::toNativeSeparators(defaultCacheDirectory()));
    toolbar->addWidget(cacheEdit);

    auto* browseButton = new QPushButton(QStringLiteral("Browse"), toolbar);
    toolbar->addWidget(browseButton);

    QAction* applyAction = toolbar->addAction(QStringLiteral("Apply Cache"));
    QAction* refreshStatsAction = toolbar->addAction(QStringLiteral("Refresh Stats"));
    QAction* clearCacheAction = toolbar->addAction(QStringLiteral("Clear Cache"));

    const QString urlTemplate = QStringLiteral("https://tile.openstreetmap.org/{z}/{x}/{y}.png");

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

    const auto normalizedCacheDirectory = [cacheEdit]
    {
        return QDir::cleanPath(QDir::fromNativeSeparators(cacheEdit->text().trimmed()));
    };

    const auto reloadLayer = [viewer, detailsView, cacheEdit, cacheCheck, &window, urlTemplate, normalizedCacheDirectory]
    {
        QString cacheDirectory = normalizedCacheDirectory();
        if (cacheDirectory.isEmpty())
            cacheDirectory = defaultCacheDirectory();

        cacheEdit->setText(QDir::toNativeSeparators(cacheDirectory));
        applyLayer(
            *viewer,
            *detailsView,
            *window.statusBar(),
            urlTemplate,
            cacheDirectory,
            cacheCheck->isChecked());
    };

    QObject::connect(applyAction, &QAction::triggered, &window, reloadLayer);
    QObject::connect(cacheCheck, &QCheckBox::toggled, &window, reloadLayer);
    QObject::connect(cacheEdit, &QLineEdit::returnPressed, &window, reloadLayer);

    QObject::connect(browseButton, &QPushButton::clicked, &window, [cacheEdit, &window, normalizedCacheDirectory]
    {
        const QString selected = QFileDialog::getExistingDirectory(
            &window,
            QStringLiteral("Select XYZ cache directory"),
            normalizedCacheDirectory());
        if (!selected.isEmpty())
            cacheEdit->setText(QDir::toNativeSeparators(selected));
    });

    QObject::connect(refreshStatsAction, &QAction::triggered, &window, [detailsView, cacheCheck, urlTemplate, normalizedCacheDirectory]
    {
        updateDetails(*detailsView, urlTemplate, normalizedCacheDirectory(), cacheCheck->isChecked());
    });

    QObject::connect(clearCacheAction, &QAction::triggered, &window, [viewer, detailsView, cacheCheck, urlTemplate, normalizedCacheDirectory, &window]
    {
        const QString cacheDirectory = normalizedCacheDirectory();
        if (cacheDirectory.isEmpty())
            return;

        if (QMessageBox::question(
            &window,
            QStringLiteral("XyzLocalCache"),
            QStringLiteral("Clear all cached tiles under:\n%1").arg(QDir::toNativeSeparators(cacheDirectory))) != QMessageBox::Yes)
        {
            return;
        }

        QDir(cacheDirectory).removeRecursively();
        updateDetails(*detailsView, urlTemplate, cacheDirectory, cacheCheck->isChecked());
        window.statusBar()->showMessage(QStringLiteral("Cache directory cleared."));
        viewer->invalidateRenderCache(true, true);
        viewer->update();
    });

    panAction->setChecked(true);

    reloadLayer();

    window.show();
    viewer->setViewExtent(defaultExtent3857());

    return app.exec();
}
