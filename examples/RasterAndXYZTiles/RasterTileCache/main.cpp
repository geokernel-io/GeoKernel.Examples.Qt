#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

#include "Layers/GisLayer.h"
#include "Loading/LayerLoadOptions.h"
#include "Common/Gdal/GdalRasterTileCacheStats.h"
#include "Common/Gdal/GdalRasterTileKey.h"
#include "Common/Gdal/GisLayerGdalRaster.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Formats::Common::Gdal;
using namespace GeoKernel::Viewer;
using namespace GeoKernel::Viewer::Loading;

namespace
{
    enum class CacheMode
    {
        None,
        Disabled,
        SmallBudget,
        LargeBudget
    };

    struct BenchmarkResult
    {
        bool valid = false;
        int tileCount = 0;
        int firstPassHits = 0;
        int secondPassHits = 0;
        int firstPassOverviewLevel = -1;
        int secondPassOverviewLevel = -1;
        qint64 firstPassMs = 0;
        qint64 secondPassMs = 0;
        QString errorMessage;
    };

    QString sampleDataPath(const QString& fileName)
    {
        const QDir appDir(QCoreApplication::applicationDirPath());
        return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/data/%1").arg(fileName)));
    }

    QString cacheModeText(CacheMode mode)
    {
        switch (mode)
        {
        case CacheMode::Disabled:
            return QStringLiteral("Cache Disabled");
        case CacheMode::SmallBudget:
            return QStringLiteral("Small Cache Budget");
        case CacheMode::LargeBudget:
            return QStringLiteral("Large Cache Budget");
        case CacheMode::None:
        default:
            return QStringLiteral("No Raster Loaded");
        }
    }

    QString statsText(const GdalRasterTileCacheStats& stats)
    {
        QStringList lines;
        lines << QStringLiteral("Cache stats");
        lines << QStringLiteral("Enabled: %1").arg(stats.enabled ? QStringLiteral("yes") : QStringLiteral("no"));
        lines << QStringLiteral("Items: %1").arg(stats.itemCount);
        lines << QStringLiteral("Used pixel cost: %1").arg(stats.usedPixelCost);
        lines << QStringLiteral("Max pixel cost: %1").arg(stats.maxPixelCost);
        lines << QStringLiteral("Max item pixel cost: %1").arg(stats.maxItemPixelCost);
        return lines.join(QStringLiteral("\n"));
    }

    QString benchmarkText(const BenchmarkResult& result)
    {
        if (!result.valid)
        {
            return result.errorMessage.isEmpty()
                ? QStringLiteral("Benchmark has not run.")
                : QStringLiteral("Benchmark failed: %1").arg(result.errorMessage);
        }

        const qint64 savedMs = result.firstPassMs - result.secondPassMs;
        const double percent = result.firstPassMs > 0
            ? (static_cast<double>(savedMs) / static_cast<double>(result.firstPassMs)) * 100.0
            : 0.0;

        QStringList lines;
        lines << QStringLiteral("Benchmark");
        lines << QStringLiteral("Tiles read per pass: %1").arg(result.tileCount);
        lines << QStringLiteral("First pass: %1 ms, cache hits=%2, overview=%3")
            .arg(result.firstPassMs)
            .arg(result.firstPassHits)
            .arg(result.firstPassOverviewLevel);
        lines << QStringLiteral("Second pass: %1 ms, cache hits=%2, overview=%3")
            .arg(result.secondPassMs)
            .arg(result.secondPassHits)
            .arg(result.secondPassOverviewLevel);
        lines << QStringLiteral("Second pass delta: %1 ms (%2%)")
            .arg(savedMs)
            .arg(percent, 0, 'f', 1);
        lines << QStringLiteral("");
        lines << QStringLiteral("How to read this");
        lines << QStringLiteral("- First pass fills the memory tile cache.");
        lines << QStringLiteral("- Second pass requests the same tiles again.");
        lines << QStringLiteral("- With a large budget, second-pass cache hits should equal tile count.");
        lines << QStringLiteral("- With cache disabled, second-pass hits stay at 0.");
        lines << QStringLiteral("- With a tiny budget, only the last few tiles survive in cache.");
        return lines.join(QStringLiteral("\n"));
    }

    QString detailsText(CacheMode mode, const GisLayerGdalRaster* raster, const BenchmarkResult& benchmark)
    {
        QStringList details;
        details << QStringLiteral("RasterTileCache sample");
        details << QStringLiteral("");
        details << QStringLiteral("Load mode: %1").arg(cacheModeText(mode));
        details << QStringLiteral("Source: %1").arg(sampleDataPath(QStringLiteral("world_8km.tif")));
        details << QStringLiteral("");

        if (raster == nullptr)
        {
            details << QStringLiteral("No raster layer loaded.");
            return details.join(QStringLiteral("\n"));
        }

        const auto& metadata = raster->metadata();
        details << QStringLiteral("Raster metadata");
        details << QStringLiteral("Driver: %1").arg(metadata.driverName);
        details << QStringLiteral("Size: %1 x %2 px").arg(metadata.width).arg(metadata.height);
        details << QStringLiteral("Bands: %1").arg(metadata.bandCount);
        details << QStringLiteral("Overview count: %1").arg(metadata.overviews.size());
        details << QStringLiteral("");
        details << statsText(raster->provider().memoryTileCacheStats());
        details << QStringLiteral("");
        details << QStringLiteral("LayerLoadOptions knobs");
        details << QStringLiteral("rasterTileCacheEnabled");
        details << QStringLiteral("rasterTileCachePixelBudget");
        details << QStringLiteral("rasterTileCacheMaximumItemPixels");
        details << QStringLiteral("");
        details << benchmarkText(benchmark);
        return details.join(QStringLiteral("\n"));
    }

    LayerLoadOptions createOptions(CacheMode mode, QProgressBar& progressBar, QLabel& statusLabel)
    {
        LayerLoadOptions options;
        options.prepareRasterOverviews = true;
        options.rasterOverviewMinimumPixels = 0;
        options.rasterTileCacheEnabled = mode != CacheMode::Disabled;

        if (mode == CacheMode::SmallBudget)
        {
            options.rasterTileCachePixelBudget = 128 * 1024;
            options.rasterTileCacheMaximumItemPixels = 128 * 1024;
        }
        else if (mode == CacheMode::LargeBudget)
        {
            options.rasterTileCachePixelBudget = 4 * 1024 * 1024;
            options.rasterTileCacheMaximumItemPixels = 512 * 512;
        }
        else
        {
            options.rasterTileCachePixelBudget = 0;
            options.rasterTileCacheMaximumItemPixels = 0;
        }

        options.progress = [&progressBar](int value)
        {
            progressBar.setValue(std::clamp(value, 0, 100));
            QApplication::processEvents();
        };

        options.status = [&statusLabel](const QString& text)
        {
            statusLabel.setText(text);
            QApplication::processEvents();
        };

        return options;
    }

    GisLayerGdalRaster* rasterLayerAt(GisViewer& viewer)
    {
        return dynamic_cast<GisLayerGdalRaster*>(viewer.mapLayerAt(0));
    }

    QVector<GdalRasterTileKey> benchmarkTiles(const GisLayerGdalRaster& raster)
    {
        const auto& metadata = raster.metadata();
        QVector<GdalRasterTileKey> keys;

        constexpr int columns = 8;
        constexpr int rows = 4;
        keys.reserve(columns * rows);

        const int sourceTileWidth = std::max(1, metadata.width / columns);
        const int sourceTileHeight = std::max(1, metadata.height / rows);

        for (int row = 0; row < rows; ++row)
        {
            for (int column = 0; column < columns; ++column)
            {
                GdalRasterTileKey key;
                key.sourceX = column * sourceTileWidth;
                key.sourceY = row * sourceTileHeight;
                key.sourceWidth = column == columns - 1
                    ? metadata.width - key.sourceX
                    : sourceTileWidth;
                key.sourceHeight = row == rows - 1
                    ? metadata.height - key.sourceY
                    : sourceTileHeight;
                key.targetWidth = 256;
                key.targetHeight = 256;
                key.overviewLevel = GdalRasterWindowRequest::AutoOverviewLevel;
                keys.append(key);
            }
        }

        return keys;
    }

    bool readTiles(
        GisLayerGdalRaster& raster,
        const QVector<GdalRasterTileKey>& keys,
        int& cacheHits,
        int& lastOverviewLevel,
        qint64& elapsedMs,
        QString& errorMessage)
    {
        cacheHits = 0;
        lastOverviewLevel = -1;

        QElapsedTimer timer;
        timer.start();

        for (const GdalRasterTileKey& key : keys)
        {
            bool cacheHit = false;
            int overviewLevel = -1;
            const QImage image = raster.provider().readTile(key, &errorMessage, &cacheHit, &overviewLevel);
            if (image.isNull() || !errorMessage.isEmpty())
            {
                elapsedMs = timer.elapsed();
                if (errorMessage.isEmpty())
                    errorMessage = QStringLiteral("GDAL returned an empty tile.");
                return false;
            }

            if (cacheHit)
                ++cacheHits;
            lastOverviewLevel = overviewLevel;
        }

        elapsedMs = timer.elapsed();
        return true;
    }

    BenchmarkResult runBenchmark(GisLayerGdalRaster& raster)
    {
        BenchmarkResult result;
        const QVector<GdalRasterTileKey> keys = benchmarkTiles(raster);
        result.tileCount = keys.size();

        raster.provider().clearMemoryTileCache();

        QString errorMessage;
        if (!readTiles(
            raster,
            keys,
            result.firstPassHits,
            result.firstPassOverviewLevel,
            result.firstPassMs,
            errorMessage))
        {
            result.errorMessage = errorMessage;
            return result;
        }

        errorMessage.clear();
        if (!readTiles(
            raster,
            keys,
            result.secondPassHits,
            result.secondPassOverviewLevel,
            result.secondPassMs,
            errorMessage))
        {
            result.errorMessage = errorMessage;
            return result;
        }

        result.valid = true;
        return result;
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("RasterTileCache"));

    auto* root = new QWidget(&window);
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* toolbar = new QWidget(root);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(6, 4, 6, 4);
    toolbarLayout->setSpacing(8);

    auto* loadDisabledButton = new QPushButton(QStringLiteral("Load Cache Disabled"), toolbar);
    auto* loadSmallButton = new QPushButton(QStringLiteral("Load Small Budget"), toolbar);
    auto* loadLargeButton = new QPushButton(QStringLiteral("Load Large Budget"), toolbar);
    auto* benchmarkButton = new QPushButton(QStringLiteral("Run Tile Benchmark"), toolbar);
    auto* clearCacheButton = new QPushButton(QStringLiteral("Clear Tile Cache"), toolbar);
    auto* fullExtentButton = new QPushButton(QStringLiteral("Full Extent"), toolbar);

    toolbarLayout->addWidget(loadDisabledButton);
    toolbarLayout->addWidget(loadSmallButton);
    toolbarLayout->addWidget(loadLargeButton);
    toolbarLayout->addWidget(benchmarkButton);
    toolbarLayout->addWidget(clearCacheButton);
    toolbarLayout->addWidget(fullExtentButton);
    toolbarLayout->addStretch();

    auto* content = new QWidget(root);
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    auto* viewer = new GisViewer(content);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);

    auto* detailsView = new QTextEdit(content);
    detailsView->setReadOnly(true);
    detailsView->setMinimumWidth(430);

    contentLayout->addWidget(viewer, 1);
    contentLayout->addWidget(detailsView);

    auto* statusBar = new QWidget(root);
    auto* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(6, 3, 6, 3);

    auto* statusLabel = new QLabel(QStringLiteral("Ready."), statusBar);
    auto* progressBar = new QProgressBar(statusBar);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setFixedWidth(240);

    statusLayout->addWidget(statusLabel, 1);
    statusLayout->addWidget(progressBar);

    rootLayout->addWidget(toolbar);
    rootLayout->addWidget(content, 1);
    rootLayout->addWidget(statusBar);
    window.setCentralWidget(root);

    CacheMode currentMode = CacheMode::None;
    BenchmarkResult lastBenchmark;

    const auto updateDetails = [&]
    {
        detailsView->setPlainText(detailsText(currentMode, rasterLayerAt(*viewer), lastBenchmark));
    };

    const auto loadRaster = [&](CacheMode mode)
    {
        currentMode = mode;
        lastBenchmark = {};
        viewer->clearLayers();
        progressBar->setValue(0);
        statusLabel->setText(QStringLiteral("Loading %1...").arg(cacheModeText(mode)));
        QApplication::setOverrideCursor(Qt::WaitCursor);

        LayerLoadOptions options = createOptions(mode, *progressBar, *statusLabel);
        QString errorMessage;
        const bool loaded = viewer->addLayerFromPath(sampleDataPath(QStringLiteral("world_8km.tif")), options, &errorMessage);

        QApplication::restoreOverrideCursor();

        if (!loaded)
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("RasterTileCache"),
                QStringLiteral("Raster could not be loaded:\n%1")
                    .arg(errorMessage.isEmpty() ? sampleDataPath(QStringLiteral("world_8km.tif")) : errorMessage));
            statusLabel->setText(QStringLiteral("Load failed."));
            updateDetails();
            return;
        }

        if (GisLayer* layer = viewer->mapLayerAt(0))
            layer->setName(cacheModeText(mode));

        viewer->fullExtent();
        progressBar->setValue(100);
        statusLabel->setText(QStringLiteral("%1 loaded.").arg(cacheModeText(mode)));
        updateDetails();
    };

    QObject::connect(loadDisabledButton, &QPushButton::clicked, viewer, [=, &loadRaster]
    {
        loadRaster(CacheMode::Disabled);
    });

    QObject::connect(loadSmallButton, &QPushButton::clicked, viewer, [=, &loadRaster]
    {
        loadRaster(CacheMode::SmallBudget);
    });

    QObject::connect(loadLargeButton, &QPushButton::clicked, viewer, [=, &loadRaster]
    {
        loadRaster(CacheMode::LargeBudget);
    });

    QObject::connect(benchmarkButton, &QPushButton::clicked, viewer, [=, &lastBenchmark, &updateDetails]
    {
        GisLayerGdalRaster* raster = rasterLayerAt(*viewer);
        if (raster == nullptr)
        {
            statusLabel->setText(QStringLiteral("Load a raster first."));
            return;
        }

        QApplication::setOverrideCursor(Qt::WaitCursor);
        statusLabel->setText(QStringLiteral("Running tile cache benchmark..."));
        QApplication::processEvents();

        lastBenchmark = runBenchmark(*raster);

        QApplication::restoreOverrideCursor();
        statusLabel->setText(benchmarkText(lastBenchmark).split(QStringLiteral("\n")).value(2));
        updateDetails();
    });

    QObject::connect(clearCacheButton, &QPushButton::clicked, viewer, [=, &lastBenchmark, &updateDetails]
    {
        if (GisLayerGdalRaster* raster = rasterLayerAt(*viewer))
        {
            raster->provider().clearMemoryTileCache();
            lastBenchmark = {};
            statusLabel->setText(QStringLiteral("Memory tile cache cleared."));
            updateDetails();
        }
    });

    QObject::connect(fullExtentButton, &QPushButton::clicked, viewer, [viewer]
    {
        viewer->fullExtent();
    });

    updateDetails();
    window.show();
    loadRaster(CacheMode::LargeBudget);

    return app.exec();
}
