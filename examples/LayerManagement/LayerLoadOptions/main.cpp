#include <QApplication>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

#include "Viewer/GisViewer.h"
#include "Layers/GisLayer.h"
#include "Layers/GisLayerStyle.h"
#include "Loading/LayerLoadOptions.h"
#include "FeatureSources/SpatialIndexPreparationState.h"

#include "Helpers.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Viewer::FeatureSources;
using namespace GeoKernel::Viewer::Loading;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;

struct QueryBenchmarkResult
{
    int queryCount = 0;
    int hitCount = 0;
    qint64 elapsedMs = 0;
};

enum class LoadedIndexMode
{
    None,
    NoIndex,
    RTree
};

QString spatialIndexStateText(SpatialIndexPreparationState state)
{
    switch (state)
    {
        case SpatialIndexPreparationState::Loading:
            return QStringLiteral("Spatial index is loading...");
        case SpatialIndexPreparationState::BuildingLocator:
            return QStringLiteral("Spatial locator is preparing...");
        case SpatialIndexPreparationState::BuildingIndex:
            return QStringLiteral("Spatial index is building...");
        case SpatialIndexPreparationState::Ready:
            return QStringLiteral("Spatial index is ready.");
        case SpatialIndexPreparationState::Cancelled:
            return QStringLiteral("Spatial index cancelled.");
        case SpatialIndexPreparationState::Failed:
            return QStringLiteral("Spatial index failed.");
        case SpatialIndexPreparationState::None:
        default:
            return QStringLiteral("Spatial index idle.");
    }
}

LayerLoadOptions createLoadOptions(bool useSpatialIndex, QProgressBar& progressBar, QLabel& statusLabel)
{
    LayerLoadOptions options;
    options.useSpatialIndex = useSpatialIndex;
    options.spatialIndexType = SpatialIndexType::RTree;
    options.buildFeatureSource = true;
    options.applyDefaultStyle = true;

    options.defaultStyle.setFillColor(QStringLiteral("#D8E5E1"));
    options.defaultStyle.setFillOpacity(210);
    options.defaultStyle.setLineColor(QStringLiteral("#607D78"));
    options.defaultStyle.setLineWidth(0.9f);

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

    options.indexStateChanged = [&statusLabel](SpatialIndexPreparationState state)
    {
        statusLabel.setText(spatialIndexStateText(state));
        QApplication::processEvents();
    };

    return options;
}

QString prepareStatesLayerPath(QWidget* parent)
{
    return ensureSampleFile(
        QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/usa_states_3857.zip")),
        QStringLiteral("usa_states_3857.zip"),
        QStringLiteral("usa_states_3857"),
        QStringLiteral("usa_states_3857.shp"),
        parent);
}

bool loadVectorLayer(GisViewer& viewer, const QString& path, bool useSpatialIndex, QProgressBar& progressBar, QLabel& statusLabel)
{
    progressBar.setValue(0);
    statusLabel.setText(useSpatialIndex
        ? QStringLiteral("Loading USA states with RTree...")
        : QStringLiteral("Loading USA states without spatial index..."));

    QElapsedTimer timer;
    timer.start();

    LayerLoadOptions options = createLoadOptions(useSpatialIndex, progressBar, statusLabel);

    viewer.clearLayers();

    QString errorMessage;
    if (!viewer.addLayerFromPath(path, options, &errorMessage))
    {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("LayerLoadOptions"),
            QStringLiteral("Layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? path : errorMessage));
        statusLabel.setText(QStringLiteral("Load failed."));
        return false;
    }

    if (auto* layer = viewer.mapLayerAt(0))
        layer->setName(useSpatialIndex ? QStringLiteral("USA States - RTree") : QStringLiteral("USA States - No Index"));

    viewer.setViewExtent(GisExtent(-16831516, 1856556, -4631023, 7472472));

    progressBar.setValue(100);
    statusLabel.setText(useSpatialIndex
        ? QStringLiteral("RTree layer loaded. Load time: %1 ms.").arg(timer.elapsed())
        : QStringLiteral("No-index layer loaded. Load time: %1 ms.").arg(timer.elapsed()));
    return true;
}

QueryBenchmarkResult runQueryBenchmark(GisViewer& viewer, LoadedIndexMode mode, QProgressBar& progressBar, QLabel& statusLabel, QLabel& noIndexResult, QLabel& rtreeResult)
{
    QueryBenchmarkResult result;

    const GisLayer* layer = viewer.mapLayerAt(0);
    if (layer == nullptr)
    {
        statusLabel.setText(QStringLiteral("Load a vector layer first."));
        return result;
    }

    const GisExtent extent = layer->extent();
    if (extent.isEmpty())
    {
        statusLabel.setText(QStringLiteral("Layer extent is empty."));
        return result;
    }

    constexpr int rows = 5;
    constexpr int columns = 8;
    constexpr int passes = 6;
    const double stepX = extent.width() / columns;
    const double stepY = extent.height() / rows;
    const double queryWidth = stepX * 0.65;
    const double queryHeight = stepY * 0.65;

    int totalHits = 0;
    int completed = 0;
    const int totalQueries = rows * columns * passes;

    progressBar.setValue(0);
    statusLabel.setText(QStringLiteral("Running query benchmark..."));
    QApplication::processEvents();

    QElapsedTimer timer;
    timer.start();

    for (int pass = 0; pass < passes; ++pass)
    {
        for (int row = 0; row < rows; ++row)
        {
            for (int column = 0; column < columns; ++column)
            {
                const double xMin = extent.xMin() + column * stepX;
                const double yMin = extent.yMin() + row * stepY;
                const GisExtent queryExtent(
                    xMin,
                    yMin,
                    xMin + queryWidth,
                    yMin + queryHeight);

                totalHits += viewer.hitTestFeaturesInExtent(queryExtent).size();

                ++completed;
                if (completed % 16 == 0)
                {
                    progressBar.setValue((completed * 100) / totalQueries);
                    QApplication::processEvents();
                }
            }
        }
    }

    progressBar.setValue(100);
    result.queryCount = totalQueries;
    result.hitCount = totalHits;
    result.elapsedMs = timer.elapsed();
    const QString resultText = QStringLiteral("%1: query %2 ms, %3 queries, %4 hits")
        .arg(mode == LoadedIndexMode::RTree ? QStringLiteral("RTree") : QStringLiteral("No Index"))
        .arg(result.elapsedMs)
        .arg(result.queryCount)
        .arg(result.hitCount);

    if (mode == LoadedIndexMode::RTree)
        rtreeResult.setText(resultText);
    else if (mode == LoadedIndexMode::NoIndex)
        noIndexResult.setText(resultText);

    statusLabel.setText(QStringLiteral("Query test finished: %1 queries, %2 hits, %3 ms.")
        .arg(totalQueries)
        .arg(totalHits)
        .arg(result.elapsedMs));
    return result;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("LayerLoadOptions"));

    auto* root = new QWidget(&window);
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* optionsBar = new QWidget(root);
    auto* optionsLayout = new QHBoxLayout(optionsBar);
    optionsLayout->setContentsMargins(6, 4, 6, 4);
    optionsLayout->setSpacing(8);

    auto* loadWithoutIndex = new QPushButton(QStringLiteral("Load No Index"), optionsBar);
    auto* loadWithRTree = new QPushButton(QStringLiteral("Load RTree"), optionsBar);
    auto* runQueryTest = new QPushButton(QStringLiteral("Run Query Test"), optionsBar);
    auto* clearLayers = new QPushButton(QStringLiteral("Clear"), optionsBar);

    optionsLayout->addWidget(loadWithoutIndex);
    optionsLayout->addWidget(loadWithRTree);
    optionsLayout->addWidget(runQueryTest);
    optionsLayout->addWidget(clearLayers);
    optionsLayout->addStretch();

    auto* resultBar = new QWidget(root);
    auto* resultLayout = new QVBoxLayout(resultBar);
    resultLayout->setContentsMargins(6, 2, 6, 4);
    resultLayout->setSpacing(2);
    auto* helpLabel = new QLabel(QStringLiteral("Load one index mode, then run the query test. Load time is shown separately and is not part of the test result."), resultBar);
    auto* noIndexResult = new QLabel(QStringLiteral("No Index: -"), resultBar);
    auto* rtreeResult = new QLabel(QStringLiteral("RTree: -"), resultBar);
    resultLayout->addWidget(helpLabel);
    resultLayout->addWidget(noIndexResult);
    resultLayout->addWidget(rtreeResult);

    auto* viewer = new GisViewer(root);
    viewer->setActiveTool(GisViewerTool::Pan);

    auto* statusBar = new QWidget(root);
    auto* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(6, 3, 6, 3);
    auto* progressBar = new QProgressBar(statusBar);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setFixedWidth(220);
    auto* statusLabel = new QLabel(QStringLiteral("Ready."), statusBar);
    statusLayout->addWidget(statusLabel, 1);
    statusLayout->addWidget(progressBar);

    rootLayout->addWidget(optionsBar);
    rootLayout->addWidget(resultBar);
    rootLayout->addWidget(viewer, 1);
    rootLayout->addWidget(statusBar);
    window.setCentralWidget(root);

    LoadedIndexMode currentMode = LoadedIndexMode::None;

    QObject::connect(loadWithoutIndex, &QPushButton::clicked, viewer, [viewer, progressBar, statusLabel, &window, &currentMode]
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QString path = prepareStatesLayerPath(&window);
        if (!path.isEmpty() && loadVectorLayer(*viewer, path, false, *progressBar, *statusLabel))
            currentMode = LoadedIndexMode::NoIndex;
        QApplication::restoreOverrideCursor();
    });

    QObject::connect(loadWithRTree, &QPushButton::clicked, viewer, [viewer, progressBar, statusLabel, &window, &currentMode]
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QString path = prepareStatesLayerPath(&window);
        if (!path.isEmpty() && loadVectorLayer(*viewer, path, true, *progressBar, *statusLabel))
            currentMode = LoadedIndexMode::RTree;
        QApplication::restoreOverrideCursor();
    });

    QObject::connect(runQueryTest, &QPushButton::clicked, viewer, [=, &currentMode]
    {
        if (currentMode == LoadedIndexMode::None)
        {
            statusLabel->setText(QStringLiteral("Load the shapefile first."));
            return;
        }

        QApplication::setOverrideCursor(Qt::WaitCursor);
        runQueryBenchmark(*viewer, currentMode, *progressBar, *statusLabel, *noIndexResult, *rtreeResult);
        QApplication::restoreOverrideCursor();
    });

    QObject::connect(clearLayers, &QPushButton::clicked, viewer, [=, &currentMode]
    {
        viewer->clearLayers();
        currentMode = LoadedIndexMode::None;
        progressBar->setValue(0);
        noIndexResult->setText(QStringLiteral("No Index: -"));
        rtreeResult->setText(QStringLiteral("RTree: -"));
        statusLabel->setText(QStringLiteral("Layers cleared."));
    });

    window.show();

    return app.exec();
}
