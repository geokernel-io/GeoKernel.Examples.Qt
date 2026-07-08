#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
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
#include <limits>

#include "Layers/GisLayer.h"
#include "Loading/LayerLoadOptions.h"
#include "Common/Gdal/GdalRasterWindowRequest.h"
#include "Common/Gdal/GisLayerGdalRaster.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Formats::Common::Gdal;
using namespace GeoKernel::Viewer;
using namespace GeoKernel::Viewer::Loading;

namespace
{
    struct BenchmarkResult
    {
        bool valid = false;
        int passes = 0;
        int targetWidth = 0;
        int targetHeight = 0;
        int selectedOverview = -1;
        qint64 elapsedMs = 0;
        QString errorMessage;
    };

    QString sampleDataPath(const QString& fileName)
    {
        const QDir appDir(QCoreApplication::applicationDirPath());
        return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/data/%1").arg(fileName)));
    }

    QString workingDirectory()
    {
        const QDir appDir(QCoreApplication::applicationDirPath());
        return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("RasterOverviewData")));
    }

    QString workingRasterPath()
    {
        return QDir(workingDirectory()).filePath(QStringLiteral("world_8km_overview_test.tif"));
    }

    bool resetWorkingCopy(QString* errorMessage)
    {
        QDir dir(workingDirectory());
        if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        {
            if (errorMessage != nullptr)
                *errorMessage = QStringLiteral("Could not create working directory: %1").arg(dir.absolutePath());
            return false;
        }

        const QString targetPath = workingRasterPath();
        const QString overviewPath = targetPath + QStringLiteral(".ovr");

        QFile::remove(targetPath);
        QFile::remove(overviewPath);

        const QString sourcePath = sampleDataPath(QStringLiteral("world_8km.tif"));
        if (!QFile::copy(sourcePath, targetPath))
        {
            if (errorMessage != nullptr)
                *errorMessage = QStringLiteral("Could not copy %1 to %2").arg(sourcePath, targetPath);
            return false;
        }

        QFile::setPermissions(
            targetPath,
            QFile::ReadOwner | QFile::WriteOwner |
            QFile::ReadUser | QFile::WriteUser |
            QFile::ReadGroup | QFile::WriteGroup |
            QFile::ReadOther);
        return true;
    }

    QString factorListText(const QVector<int>& factors)
    {
        if (factors.isEmpty())
            return QStringLiteral("-");

        QStringList parts;
        for (int factor : factors)
            parts << QString::number(factor);
        return parts.join(QStringLiteral(", "));
    }

    QString overviewListText(const QVector<GdalRasterOverviewInfo>& overviews)
    {
        if (overviews.isEmpty())
            return QStringLiteral("-");

        QStringList parts;
        for (const GdalRasterOverviewInfo& overview : overviews)
            parts << QStringLiteral("%1x%2").arg(overview.width).arg(overview.height);
        return parts.join(QStringLiteral(", "));
    }

    QString rasterDetailsText(
        const QString& mode,
        qint64 elapsedMs,
        const GisLayerGdalRaster* raster,
        const QString& workingPath,
        const QString& benchmarkText,
        const QString& comparisonText)
    {
        QFileInfo rasterInfo(workingPath);
        QFileInfo overviewInfo(workingPath + QStringLiteral(".ovr"));

        QStringList details;
        details << QStringLiteral("RasterOverview sample");
        details << QStringLiteral("");
        details << QStringLiteral("Load mode: %1").arg(mode);
        details << QStringLiteral("Load elapsed: %1 ms").arg(elapsedMs);
        details << QStringLiteral("Working raster: %1").arg(workingPath);
        details << QStringLiteral("Raster file size: %1 bytes").arg(rasterInfo.exists() ? rasterInfo.size() : 0);
        details << QStringLiteral("Overview file: %1").arg(overviewInfo.filePath());
        details << QStringLiteral("Overview file exists: %1").arg(overviewInfo.exists() ? QStringLiteral("yes") : QStringLiteral("no"));
        details << QStringLiteral("Overview file size: %1 bytes").arg(overviewInfo.exists() ? overviewInfo.size() : 0);
        details << QStringLiteral("");

        if (raster == nullptr)
        {
            details << QStringLiteral("No raster layer loaded.");
            return details.join(QStringLiteral("\n"));
        }

        const auto& metadata = raster->metadata();
        const qint64 rasterPixels =
            static_cast<qint64>(std::max(0, metadata.width)) *
            static_cast<qint64>(std::max(0, metadata.height));

        details << QStringLiteral("Raster metadata");
        details << QStringLiteral("Driver: %1").arg(metadata.driverName);
        details << QStringLiteral("Size: %1 x %2 px").arg(metadata.width).arg(metadata.height);
        details << QStringLiteral("Pixels: %1").arg(rasterPixels);
        details << QStringLiteral("Bands: %1").arg(metadata.bandCount);
        details << QStringLiteral("EPSG: %1").arg(metadata.epsgCode == 0 ? QStringLiteral("unknown") : QString::number(metadata.epsgCode));
        details << QStringLiteral("");
        details << QStringLiteral("Provider overview state");
        details << QStringLiteral("Provider overview path: %1").arg(raster->provider().overviewFilePath());
        details << QStringLiteral("Recommended pyramid ready: %1").arg(raster->provider().hasRecommendedOverviewPyramid() ? QStringLiteral("yes") : QStringLiteral("no"));
        details << QStringLiteral("Overview count: %1").arg(metadata.overviews.size());
        details << QStringLiteral("Overview dimensions: %1").arg(overviewListText(metadata.overviews));
        details << QStringLiteral("Overview factors: %1").arg(factorListText(raster->provider().overviewFactors()));
        details << QStringLiteral("Recommended factors: %1").arg(factorListText(raster->provider().recommendedOverviewFactors()));
        details << QStringLiteral("");
        details << QStringLiteral("LayerLoadOptions knobs");
        details << QStringLiteral("prepareRasterOverviews = true/false");
        details << QStringLiteral("rasterOverviewMinimumPixels = threshold");
        details << QStringLiteral("rasterOverviewResampling = AVERAGE");
        details << QStringLiteral("");
        details << QStringLiteral("Why overviews matter");
        details << QStringLiteral("- Overview is useful when the raster is drawn much smaller than its real pixel size.");
        details << QStringLiteral("- Without overview, GDAL reads the full-resolution raster and downsamples it.");
        details << QStringLiteral("- With overview, GDAL can read a prebuilt smaller pyramid level.");
        details << QStringLiteral("- If selected overview is -1, this request did not benefit from the pyramid.");
        details << QStringLiteral("- If selected overview is 0 or greater, the request used a pyramid level.");
        details << QStringLiteral("");
        details << QStringLiteral("Benchmark");
        details << (benchmarkText.isEmpty() ? QStringLiteral("Run Downsample Benchmark after loading a raster.") : benchmarkText);
        if (!comparisonText.isEmpty())
            details << comparisonText;
        details << QStringLiteral("");
        details << QStringLiteral("Test flow");
        details << QStringLiteral("1. Reset Working Copy removes the .ovr file.");
        details << QStringLiteral("2. Load Without Overview skips pyramid creation.");
        details << QStringLiteral("3. Load With Overview sets rasterOverviewMinimumPixels=0 and builds the pyramid.");
        details << QStringLiteral("4. Run Downsample Benchmark and compare selected overview level + elapsed time.");
        details << QStringLiteral("5. This sample raster is small; real gains become clearer on large rasters.");

        return details.join(QStringLiteral("\n"));
    }

    BenchmarkResult runDownsampleBenchmark(GisLayerGdalRaster& raster)
    {
        BenchmarkResult result;

        const auto& metadata = raster.metadata();
        if (metadata.width <= 0 || metadata.height <= 0)
        {
            result.errorMessage = QStringLiteral("Raster size is invalid.");
            return result;
        }

        GdalRasterWindowRequest request;
        request.sourceX = 0;
        request.sourceY = 0;
        request.sourceWidth = metadata.width;
        request.sourceHeight = metadata.height;
        request.targetWidth = 128;
        request.targetHeight = 64;
        request.exactSourceLeft = 0.0;
        request.exactSourceTop = 0.0;
        request.exactSourceRight = metadata.width;
        request.exactSourceBottom = metadata.height;

        result.selectedOverview = raster.provider().selectedOverviewLevel(request);
        result.passes = 40;
        result.targetWidth = request.targetWidth;
        result.targetHeight = request.targetHeight;

        raster.provider().clearMemoryTileCache();
        const bool cacheWasEnabled = raster.provider().memoryTileCacheEnabled();
        raster.provider().setMemoryTileCacheEnabled(false);

        QElapsedTimer timer;
        timer.start();

        for (int i = 0; i < result.passes; ++i)
        {
            QString errorMessage;
            const QImage image = raster.provider().readWindow(request, &errorMessage);
            if (image.isNull() || !errorMessage.isEmpty())
            {
                result.errorMessage = errorMessage.isEmpty()
                    ? QStringLiteral("GDAL returned an empty image.")
                    : errorMessage;
                break;
            }
        }

        result.elapsedMs = timer.elapsed();
        raster.provider().setMemoryTileCacheEnabled(cacheWasEnabled);
        result.valid = result.errorMessage.isEmpty();
        return result;
    }

    QString benchmarkText(const QString& mode, const BenchmarkResult& result)
    {
        if (!result.valid)
        {
            return QStringLiteral("%1 benchmark failed: %2")
                .arg(mode, result.errorMessage);
        }

        return QStringLiteral("%1 zoomed-out benchmark: %2 reads to %3x%4, selected overview=%5, elapsed=%6 ms")
            .arg(mode)
            .arg(result.passes)
            .arg(result.targetWidth)
            .arg(result.targetHeight)
            .arg(result.selectedOverview)
            .arg(result.elapsedMs);
    }

    QString comparisonText(const BenchmarkResult& withoutOverview, const BenchmarkResult& withOverview)
    {
        if (!withoutOverview.valid || !withOverview.valid)
            return {};

        if (withoutOverview.elapsedMs <= 0)
            return {};

        const qint64 savedMs = withoutOverview.elapsedMs - withOverview.elapsedMs;
        const double percent = (static_cast<double>(savedMs) / static_cast<double>(withoutOverview.elapsedMs)) * 100.0;

        if (savedMs <= 0)
        {
            return QStringLiteral("Comparison: overview did not win on this run (%1 ms without, %2 ms with). This can happen on small rasters or warm OS cache.")
                .arg(withoutOverview.elapsedMs)
                .arg(withOverview.elapsedMs);
        }

        return QStringLiteral("Comparison: overview saved %1 ms (%2% faster) for this zoomed-out read (%3 ms without, %4 ms with).")
            .arg(savedMs)
            .arg(percent, 0, 'f', 1)
            .arg(withoutOverview.elapsedMs)
            .arg(withOverview.elapsedMs);
    }

    LayerLoadOptions createRasterOptions(
        bool prepareOverview,
        QProgressBar& progressBar,
        QLabel& statusLabel)
    {
        LayerLoadOptions options;
        options.prepareRasterOverviews = prepareOverview;
        options.rasterOverviewMinimumPixels = prepareOverview ? 0 : std::numeric_limits<qint64>::max();
        options.rasterOverviewResampling = QStringLiteral("AVERAGE");

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

    GisLayerGdalRaster* rasterLayerAt(GisViewer& viewer, int index)
    {
        return dynamic_cast<GisLayerGdalRaster*>(viewer.mapLayerAt(index));
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("RasterOverview"));

    auto* root = new QWidget(&window);
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* toolbar = new QWidget(root);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(6, 4, 6, 4);
    toolbarLayout->setSpacing(8);

    auto* resetButton = new QPushButton(QStringLiteral("Reset Working Copy"), toolbar);
    auto* loadWithoutOverviewButton = new QPushButton(QStringLiteral("Load Without Overview"), toolbar);
    auto* loadWithOverviewButton = new QPushButton(QStringLiteral("Load With Overview"), toolbar);
    auto* benchmarkButton = new QPushButton(QStringLiteral("Run Downsample Benchmark"), toolbar);
    auto* fullExtentButton = new QPushButton(QStringLiteral("Full Extent"), toolbar);

    toolbarLayout->addWidget(resetButton);
    toolbarLayout->addWidget(loadWithoutOverviewButton);
    toolbarLayout->addWidget(loadWithOverviewButton);
    toolbarLayout->addWidget(benchmarkButton);
    toolbarLayout->addWidget(fullExtentButton);
    toolbarLayout->addStretch();

    auto* content = new QWidget(root);
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    auto* viewer = new GisViewer(content);
    viewer->setActiveTool(GisViewerTool::Pan);

    auto* detailsView = new QTextEdit(content);
    detailsView->setReadOnly(true);
    detailsView->setMinimumWidth(420);

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

    QString currentMode = QStringLiteral("Reset");
    qint64 currentElapsedMs = 0;
    QString lastBenchmarkText;
    BenchmarkResult withoutOverviewBenchmark;
    BenchmarkResult withOverviewBenchmark;

    const auto updateDetails = [&]
    {
        detailsView->setPlainText(rasterDetailsText(
            currentMode,
            currentElapsedMs,
            rasterLayerAt(*viewer, 0),
            workingRasterPath(),
            lastBenchmarkText,
            comparisonText(withoutOverviewBenchmark, withOverviewBenchmark)));
    };

    const auto resetWorking = [&]
    {
        viewer->clearLayers();
        progressBar->setValue(0);

        QString errorMessage;
        if (!resetWorkingCopy(&errorMessage))
        {
            QMessageBox::critical(&window, QStringLiteral("RasterOverview"), errorMessage);
            statusLabel->setText(QStringLiteral("Reset failed."));
            return false;
        }

        currentMode = QStringLiteral("Reset");
        currentElapsedMs = 0;
        lastBenchmarkText.clear();
        withoutOverviewBenchmark = {};
        withOverviewBenchmark = {};
        updateDetails();
        statusLabel->setText(QStringLiteral("Working copy reset. Overview file removed."));
        return true;
    };

    const auto loadRaster = [&](bool prepareOverview)
    {
        const QString mode = prepareOverview
            ? QStringLiteral("Load With Overview")
            : QStringLiteral("Load Without Overview");

        if (!QFileInfo::exists(workingRasterPath()))
        {
            QString errorMessage;
            if (!resetWorkingCopy(&errorMessage))
            {
                QMessageBox::critical(&window, QStringLiteral("RasterOverview"), errorMessage);
                return;
            }
        }

        viewer->clearLayers();
        progressBar->setValue(0);
        statusLabel->setText(mode);
        QApplication::setOverrideCursor(Qt::WaitCursor);

        QElapsedTimer timer;
        timer.start();

        LayerLoadOptions options = createRasterOptions(prepareOverview, *progressBar, *statusLabel);
        QString errorMessage;
        const bool loaded = viewer->addLayerFromPath(workingRasterPath(), options, &errorMessage);
        const qint64 elapsedMs = timer.elapsed();

        QApplication::restoreOverrideCursor();

        if (!loaded)
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("RasterOverview"),
                QStringLiteral("Raster could not be loaded:\n%1")
                    .arg(errorMessage.isEmpty() ? workingRasterPath() : errorMessage));
            statusLabel->setText(QStringLiteral("Load failed."));
            currentMode = mode;
            currentElapsedMs = elapsedMs;
            updateDetails();
            return;
        }

        if (GisLayer* layer = viewer->mapLayerAt(0))
            layer->setName(prepareOverview ? QStringLiteral("GeoTIFF - Overview") : QStringLiteral("GeoTIFF - No Overview"));

        viewer->fullExtent();
        progressBar->setValue(100);
        statusLabel->setText(QStringLiteral("%1 finished in %2 ms.").arg(mode).arg(elapsedMs));
        currentMode = mode;
        currentElapsedMs = elapsedMs;
        lastBenchmarkText.clear();
        updateDetails();
    };

    QObject::connect(resetButton, &QPushButton::clicked, viewer, resetWorking);
    QObject::connect(loadWithoutOverviewButton, &QPushButton::clicked, viewer, [=, &loadRaster]
    {
        loadRaster(false);
    });
    QObject::connect(loadWithOverviewButton, &QPushButton::clicked, viewer, [=, &loadRaster]
    {
        loadRaster(true);
    });
    QObject::connect(benchmarkButton, &QPushButton::clicked, viewer, [=, &currentMode, &lastBenchmarkText, &withoutOverviewBenchmark, &withOverviewBenchmark, &updateDetails]
    {
        GisLayerGdalRaster* raster = rasterLayerAt(*viewer, 0);
        if (raster == nullptr)
        {
            statusLabel->setText(QStringLiteral("Load a raster first."));
            return;
        }

        QApplication::setOverrideCursor(Qt::WaitCursor);
        statusLabel->setText(QStringLiteral("Running downsample benchmark..."));
        QApplication::processEvents();

        const BenchmarkResult result = runDownsampleBenchmark(*raster);

        QApplication::restoreOverrideCursor();
        lastBenchmarkText = benchmarkText(currentMode, result);
        if (currentMode == QStringLiteral("Load Without Overview"))
            withoutOverviewBenchmark = result;
        else if (currentMode == QStringLiteral("Load With Overview"))
            withOverviewBenchmark = result;

        const QString comparison = comparisonText(withoutOverviewBenchmark, withOverviewBenchmark);
        statusLabel->setText(comparison.isEmpty() ? lastBenchmarkText : comparison);
        updateDetails();
    });
    QObject::connect(fullExtentButton, &QPushButton::clicked, viewer, [viewer]
    {
        viewer->fullExtent();
    });

    resetWorking();
    window.show();

    return app.exec();
}
