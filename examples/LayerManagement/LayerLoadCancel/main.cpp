#include <QApplication>
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
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <atomic>

#include "Viewer/GisViewer.h"
#include "Layers/GisLayer.h"
#include "Layers/GisLayerStyle.h"
#include "Loading/LayerLoadOptions.h"
#include "FeatureSources/SpatialIndexPreparationState.h"

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Viewer::FeatureSources;
using namespace GeoKernel::Viewer::Loading;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Spatial;
using namespace GeoKernel::Core::Shapes;

QString sampleDataPath(const QString& relativePath)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/data/%1").arg(relativePath)));
}

QString spatialIndexStateText(SpatialIndexPreparationState state)
{
    switch (state)
    {
        case SpatialIndexPreparationState::Loading:
            return QStringLiteral("Spatial index is loading...");
        case SpatialIndexPreparationState::BuildingLocator:
            return QStringLiteral("Feature locators are preparing...");
        case SpatialIndexPreparationState::BuildingIndex:
            return QStringLiteral("Spatial index is building...");
        case SpatialIndexPreparationState::Ready:
            return QStringLiteral("Spatial index is ready.");
        case SpatialIndexPreparationState::Cancelled:
            return QStringLiteral("Load cancelled while preparing spatial index.");
        case SpatialIndexPreparationState::Failed:
            return QStringLiteral("Spatial index failed.");
        case SpatialIndexPreparationState::None:
        default:
            return QStringLiteral("Spatial index idle.");
    }
}

LayerLoadOptions createLoadOptions(std::atomic_bool& cancelRequested, QProgressBar& progressBar, QLabel& statusLabel)
{
    LayerLoadOptions options;
    options.useSpatialIndex = true;
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

    options.isCancellationRequested = [&cancelRequested]()
    {
        QApplication::processEvents();
        return cancelRequested.load();
    };

    options.indexStateChanged = [&statusLabel](SpatialIndexPreparationState state)
    {
        statusLabel.setText(spatialIndexStateText(state));
        QApplication::processEvents();
    };

    return options;
}

void loadLargeLayer(GisViewer& viewer, std::atomic_bool& cancelRequested, QPushButton& loadButton, QPushButton& cancelButton, QProgressBar& progressBar, QLabel& statusLabel)
{
    cancelRequested.store(false);
    loadButton.setEnabled(false);
    cancelButton.setEnabled(true);
    progressBar.setValue(0);
    statusLabel.setText(QStringLiteral("Layer load started..."));
    QApplication::processEvents();

    QElapsedTimer timer;
    timer.start();

    LayerLoadOptions options = createLoadOptions(cancelRequested, progressBar, statusLabel);
    const QString path = sampleDataPath(QStringLiteral("shapefile/output_1m_points.shp"));    

    viewer.clearLayers();

    QString errorMessage;
    const bool loaded = viewer.addLayerFromPath(path, options, &errorMessage);

    cancelButton.setEnabled(false);
    loadButton.setEnabled(true);

    if (cancelRequested.load())
    {
        progressBar.setValue(0);
        statusLabel.setText(QStringLiteral("Layer load cancelled after %1 ms.").arg(timer.elapsed()));
        return;
    }

    if (!loaded)
    {
        progressBar.setValue(0);
        statusLabel.setText(QStringLiteral("Layer load failed."));
        QMessageBox::critical(
            nullptr,
            QStringLiteral("LayerLoadCancel"),
            QStringLiteral("Layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? path : errorMessage));
        return;
    }

    if (auto* layer = viewer.mapLayerAt(0))
        layer->setName(QStringLiteral("One Million Points"));

    viewer.fullExtent();
    progressBar.setValue(100);
    statusLabel.setText(QStringLiteral("Layer loaded in %1 ms.").arg(timer.elapsed()));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("LayerLoadCancel"));

    auto* root = new QWidget(&window);
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* toolbar = new QWidget(root);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(6, 4, 6, 4);
    toolbarLayout->setSpacing(8);

    auto* loadButton = new QPushButton(QStringLiteral("Load Large Layer"), toolbar);
    auto* cancelButton = new QPushButton(QStringLiteral("Cancel"), toolbar);
    auto* clearButton = new QPushButton(QStringLiteral("Clear"), toolbar);
    cancelButton->setEnabled(false);

    toolbarLayout->addWidget(loadButton);
    toolbarLayout->addWidget(cancelButton);
    toolbarLayout->addWidget(clearButton);
    toolbarLayout->addStretch();

    auto* viewer = new GisViewer(root);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);

    auto* statusBar = new QWidget(root);
    auto* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(6, 3, 6, 3);
    auto* statusLabel = new QLabel(QStringLiteral("Press Load Large Layer, then Cancel while loading."), statusBar);
    auto* progressBar = new QProgressBar(statusBar);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setFixedWidth(220);
    statusLayout->addWidget(statusLabel, 1);
    statusLayout->addWidget(progressBar);

    rootLayout->addWidget(toolbar);
    rootLayout->addWidget(viewer, 1);
    rootLayout->addWidget(statusBar);
    window.setCentralWidget(root);

    std::atomic_bool cancelRequested { false };

    QObject::connect(loadButton, &QPushButton::clicked, viewer, [&]
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        loadLargeLayer(
            *viewer,
            cancelRequested,
            *loadButton,
            *cancelButton,
            *progressBar,
            *statusLabel);
        QApplication::restoreOverrideCursor();
    });

    QObject::connect(cancelButton, &QPushButton::clicked, viewer, [&]
    {
        cancelRequested.store(true);
        statusLabel->setText(QStringLiteral("Cancel requested..."));
    });

    QObject::connect(clearButton, &QPushButton::clicked, viewer, [&]
    {
        cancelRequested.store(false);
        viewer->clearLayers();
        progressBar->setValue(0);
        loadButton->setEnabled(true);
        cancelButton->setEnabled(false);
        statusLabel->setText(QStringLiteral("Layers cleared."));
    });

    window.show();

    return app.exec();
}
