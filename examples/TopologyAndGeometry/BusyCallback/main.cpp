#include "Helpers.h"
#include "Viewer/GisViewer.h"
#include "Layers/GisLayer.h"
#include "Layers/GisLayerStyle.h"
#include "Loading/LayerLoadOptions.h"
#include "FeatureSources/SpatialIndexPreparationState.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Spatial;
using namespace GeoKernel::Viewer;
using namespace GeoKernel::Viewer::FeatureSources;
using namespace GeoKernel::Viewer::Loading;

QString spatialIndexStateText(SpatialIndexPreparationState state)
{
    switch (state)
    {
        case SpatialIndexPreparationState::Loading:
            return QStringLiteral("Spatial index loading...");
        case SpatialIndexPreparationState::BuildingLocator:
            return QStringLiteral("Spatial locator preparing...");
        case SpatialIndexPreparationState::BuildingIndex:
            return QStringLiteral("Spatial index building...");
        case SpatialIndexPreparationState::Ready:
            return QStringLiteral("Spatial index ready.");
        case SpatialIndexPreparationState::Cancelled:
            return QStringLiteral("Spatial index cancelled.");
        case SpatialIndexPreparationState::Failed:
            return QStringLiteral("Spatial index failed.");
        case SpatialIndexPreparationState::None:
        default:
            return QStringLiteral("Spatial index idle.");
    }
}

void setProgress(QProgressBar& progressBar, int value)
{
    progressBar.setValue(std::clamp(value, 0, 100));
    QApplication::processEvents();
}

void setStatus(QLabel& statusLabel, const QString& text)
{
    statusLabel.setText(text);
    QApplication::processEvents();
}

void appendLog(QTextEdit& eventLog, const QString& text)
{
    eventLog.append(QStringLiteral("%1  %2")
        .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")), text));
}

QString extentText(const GisExtent& extent)
{
    if (extent.isEmpty())
        return QStringLiteral("<empty>");

    return QStringLiteral("(%1, %2) - (%3, %4)")
        .arg(extent.xMin(), 0, 'f', 3)
        .arg(extent.yMin(), 0, 'f', 3)
        .arg(extent.xMax(), 0, 'f', 3)
        .arg(extent.yMax(), 0, 'f', 3);
}

LayerLoadOptions createLoadOptions(QProgressBar& progressBar, QLabel& statusLabel, QTextEdit& eventLog)
{
    LayerLoadOptions options;
    options.useSpatialIndex = true;
    options.spatialIndexType = SpatialIndexType::RTree;
    options.buildFeatureSource = true;
    options.applyDefaultStyle = true;

    options.defaultStyle.setPointColor(QStringLiteral("#2D82B7"));
    options.defaultStyle.setPointSize(2.8f);
    options.defaultStyle.setLineColor(QStringLiteral("#1C5D87"));
    options.defaultStyle.setLineWidth(0.8f);

    options.progress = [&progressBar](int value)
    {
        setProgress(progressBar, value);
    };

    options.status = [&statusLabel](const QString& text)
    {
        setStatus(statusLabel, text);
    };

    options.indexStateChanged = [&statusLabel, &eventLog](SpatialIndexPreparationState state)
    {
        setStatus(statusLabel, spatialIndexStateText(state));
        appendLog(eventLog, QStringLiteral("Callback: spatialIndexState=%1").arg(static_cast<int>(state)));
    };

    return options;
}

void connectBusyEvents(GisViewer& viewer, QLabel& busyStateLabel, QTextEdit& eventLog)
{
    QObject::connect(&viewer, &GisViewer::busyChanged, &viewer, [&busyStateLabel, &eventLog](bool busy)
    {
        busyStateLabel.setText(busy ? QStringLiteral("Busy: ON") : QStringLiteral("Busy: OFF"));
        appendLog(eventLog, QStringLiteral("Event: busyChanged(%1)").arg(busy ? QStringLiteral("true") : QStringLiteral("false")));
    });

    QObject::connect(&viewer, &GisViewer::layerAdded, &viewer, [&eventLog](GisLayer* layer)
    {
        appendLog(eventLog, QStringLiteral("Event: layerAdded(name=%1)").arg(layer != nullptr ? layer->name() : QStringLiteral("<null>")));
    });

    QObject::connect(&viewer, &GisViewer::layersChanged, &viewer, [&viewer, &eventLog]
    {
        appendLog(eventLog, QStringLiteral("Event: layersChanged(count=%1)").arg(viewer.layerCount()));
    });
}

void loadLargeLayer(
    GisViewer& viewer,
    QPushButton& loadButton,
    QPushButton& clearButton,
    QProgressBar& progressBar,
    QLabel& statusLabel,
    QLabel& busyStateLabel,
    QTextEdit& eventLog)
{
    const QString path = sampleDataPath(QStringLiteral("shapefile/output_1m_points.shp"));
    if (!QFileInfo::exists(path))
    {
        QMessageBox::critical(
            &viewer,
            QStringLiteral("BusyCallback"),
            QStringLiteral("Layer file could not be found:\n%1").arg(path));
        return;
    }

    loadButton.setEnabled(false);
    clearButton.setEnabled(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    setProgress(progressBar, 0);
    setStatus(statusLabel, QStringLiteral("Loading output_1m_points.shp..."));
    appendLog(eventLog, QStringLiteral("Action: addLayerFromPath(output_1m_points.shp)"));

    QElapsedTimer timer;
    timer.start();

    QString errorMessage;
    const LayerLoadOptions options = createLoadOptions(progressBar, statusLabel, eventLog);
    viewer.clearLayers();
    const bool loaded = viewer.addLayerFromPath(path, options, &errorMessage);

    QApplication::restoreOverrideCursor();
    loadButton.setEnabled(true);
    clearButton.setEnabled(true);
    busyStateLabel.setText(QStringLiteral("Busy: OFF"));

    if (!loaded)
    {
        setProgress(progressBar, 0);
        setStatus(statusLabel, QStringLiteral("Layer load failed."));
        appendLog(eventLog, QStringLiteral("Result: load failed"));
        QMessageBox::critical(
            &viewer,
            QStringLiteral("BusyCallback"),
            QStringLiteral("Layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? path : errorMessage));
        return;
    }

    if (auto* layer = viewer.mapLayerAt(0))
    {
        layer->setName(QStringLiteral("One Million Points"));
        appendLog(eventLog, QStringLiteral("Layer: extent=%1").arg(extentText(layer->extent())));
    }

    viewer.fullExtent();
    viewer.invalidateRenderCache(true, true);
    viewer.repaint();

    setProgress(progressBar, 100);
    setStatus(statusLabel, QStringLiteral("Layer loaded in %1 ms.").arg(timer.elapsed()));
    appendLog(eventLog, QStringLiteral("Result: loaded in %1 ms").arg(timer.elapsed()));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("BusyCallback"));

    auto* root = new QWidget(&window);
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* toolbar = new QWidget(root);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(6, 4, 6, 4);
    toolbarLayout->setSpacing(8);

    auto* loadButton = new QPushButton(QStringLiteral("Load Large Layer"), toolbar);
    auto* clearButton = new QPushButton(QStringLiteral("Clear Layers"), toolbar);
    auto* busyStateLabel = new QLabel(QStringLiteral("Busy: OFF"), toolbar);

    toolbarLayout->addWidget(loadButton);
    toolbarLayout->addWidget(clearButton);
    toolbarLayout->addSpacing(12);
    toolbarLayout->addWidget(busyStateLabel);
    toolbarLayout->addStretch();

    auto* content = new QWidget(root);
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    auto* viewer = new GisViewer(content);
    viewer->setActiveTool(GisViewerTool::Pan);

    auto* eventLog = new QTextEdit(content);
    eventLog->setReadOnly(true);
    eventLog->setMinimumWidth(360);

    contentLayout->addWidget(viewer, 1);
    contentLayout->addWidget(eventLog);

    auto* statusBar = new QWidget(root);
    auto* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(6, 3, 6, 3);
    auto* statusLabel = new QLabel(QStringLiteral("Ready. Click Load Large Layer to see busy/progress callbacks."), statusBar);
    auto* progressBar = new QProgressBar(statusBar);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setFixedWidth(220);
    statusLayout->addWidget(statusLabel, 1);
    statusLayout->addWidget(progressBar);

    rootLayout->addWidget(toolbar);
    rootLayout->addWidget(content, 1);
    rootLayout->addWidget(statusBar);
    window.setCentralWidget(root);

    connectBusyEvents(*viewer, *busyStateLabel, *eventLog);
    appendLog(*eventLog, QStringLiteral("Sample ready. API: busyChanged + LayerLoadOptions callbacks."));

    QObject::connect(loadButton, &QPushButton::clicked, viewer, [=]
    {
        loadLargeLayer(*viewer, *loadButton, *clearButton, *progressBar, *statusLabel, *busyStateLabel, *eventLog);
    });

    QObject::connect(clearButton, &QPushButton::clicked, viewer, [=]
    {
        viewer->clearLayers();
        setProgress(*progressBar, 0);
        setStatus(*statusLabel, QStringLiteral("Layers cleared."));
        appendLog(*eventLog, QStringLiteral("Action: clearLayers()"));
    });

    window.show();

    return app.exec();
}
