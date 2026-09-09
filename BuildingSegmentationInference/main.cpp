#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTextEdit>
#include <QThread>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>
#include "GeoKernelAI.h"
#include "GeoKernelAnalysis.h"
#include "Raster/Tiff/GisLayerTIFF.h"
#include "SampleSupport.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::AI;
using namespace GeoKernel::Analysis;
using namespace GeoKernel::Formats::Raster::Tiff;
using namespace GeoKernel::Viewer;

namespace {
struct JobResult {
    QString error;
    QString sourcePath;
    QString maskPath;
    AiModelManifest manifest;
    AiRasterInferenceResult inference;
    qint64 polygonCount = 0;
    std::unique_ptr<GisLayerTIFF> sourceLayer;
    std::unique_ptr<GeoKernel::Core::Layers::GisLayerVector> predictionLayer;
};

QString existingPath(const QString& candidate) {
    return QFileInfo::exists(candidate) ? QDir::toNativeSeparators(candidate) : QString{};
}

QString diagnostics(const JobResult& result) {
    return QStringLiteral(
        "GeoKernel AI building segmentation inference\n\n"
        "Model: %1 %2\nProvider: %3\nRaster: %4 x %5\nTiles: %6\nElapsed: %7 ms\n\n"
        "Instance labels:\n%8\n\nBuilding polygons: %9\nVector output: in-memory layer")
        .arg(result.manifest.id, result.manifest.version,
             aiExecutionProviderName(result.inference.provider))
        .arg(result.inference.width).arg(result.inference.height)
        .arg(result.inference.processedTiles).arg(result.inference.elapsedMilliseconds)
        .arg(QDir::toNativeSeparators(result.maskPath))
        .arg(result.polygonCount);
}

QWidget* pathEditor(QWidget* parent, QLineEdit*& editor, QPushButton*& browse) {
    auto* container = new QWidget(parent);
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    editor = new QLineEdit(container);
    browse = new QPushButton("Browse...", container);
    layout->addWidget(editor, 1);
    layout->addWidget(browse);
    return container;
}
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QMainWindow window;
    window.resize(1280, 820);
    window.setWindowTitle("BuildingSegmentationInference");
    window.setWindowIcon(QIcon(":/icons/geokernel.ico"));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);
    // Create all geometry-affecting window chrome before the OpenGL viewer
    // starts painting. Lazily creating the status bar after inference causes
    // a QOpenGLWidget resize during paint on Windows/NVIDIA drivers.
    auto* status = window.statusBar();
    status->showMessage(QStringLiteral("Map ready."));

    auto* panel = new QWidget(&window);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->addWidget(new QLabel("<b>Building segmentation inference</b>", panel));
    panelLayout->addWidget(new QLabel(
        "Run a GeoKernel model package on a georeferenced RGB raster.", panel));

    auto* form = new QFormLayout;
    QLineEdit *modelPath = nullptr, *rasterPath = nullptr, *outputPath = nullptr;
    QPushButton *browseModel = nullptr, *browseRaster = nullptr, *browseOutput = nullptr;
    form->addRow("Model package", pathEditor(panel, modelPath, browseModel));
    form->addRow("Input raster", pathEditor(panel, rasterPath, browseRaster));
    form->addRow("Instance labels", pathEditor(panel, outputPath, browseOutput));
    auto* provider = new QComboBox(panel);
    provider->addItem("Auto", static_cast<int>(AiExecutionProvider::Auto));
    provider->addItem("CPU", static_cast<int>(AiExecutionProvider::CPU));
    provider->addItem("CUDA", static_cast<int>(AiExecutionProvider::CUDA));
    provider->addItem("DirectML", static_cast<int>(AiExecutionProvider::DirectML));
    form->addRow("Execution provider", provider);
    panelLayout->addLayout(form);

    auto* run = new QPushButton("Run building segmentation inference", panel);
    panelLayout->addWidget(run);
    auto* progress = new QProgressBar(panel);
    progress->setRange(0, 100);
    progress->setValue(0);
    panelLayout->addWidget(progress);
    auto* details = new QTextEdit(panel);
    details->setReadOnly(true);
    details->setPlainText("Select a model package and an input raster.");
    panelLayout->addWidget(new QLabel("Inference diagnostics", panel));
    panelLayout->addWidget(details, 1);

    auto* legend = new QLabel(panel);
    legend->setTextFormat(Qt::RichText);
    legend->setText(
        "<b>Building segmentation classes</b><br>"
        "<font color='#ebebeb'>●</font> Background &nbsp; "
        "<font color='#ff4020'>●</font> Building");
    legend->setWordWrap(true);
    panelLayout->addWidget(legend);

    auto* dock = new QDockWidget("GeoKernel AI", &window);
    dock->setMinimumWidth(420);
    dock->setWidget(panel);
    window.addDockWidget(Qt::RightDockWidgetArea, dock);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);
    auto* zoomIn = toolbar->addAction(QIcon(":/icons/zoom-in.svg"), "Zoom In");
    auto* zoomOut = toolbar->addAction(QIcon(":/icons/zoom-out.svg"), "Zoom Out");
    auto* fullExtent = toolbar->addAction(QIcon(":/icons/full-extent.svg"), "Full Extent");
    toolbar->addSeparator();
    QActionGroup tools(&window);
    tools.setExclusive(true);
    auto* zoomBox = toolbar->addAction(QIcon(":/icons/zoom-box.svg"), "Zoom Rect");
    zoomBox->setCheckable(true); tools.addAction(zoomBox);
    auto* pan = toolbar->addAction(QIcon(":/icons/pan.svg"), "Pan");
    pan->setCheckable(true); pan->setChecked(true); tools.addAction(pan);
    QObject::connect(zoomIn, &QAction::triggered, viewer, &GisViewer::zoomIn);
    QObject::connect(zoomOut, &QAction::triggered, viewer, &GisViewer::zoomOut);
    QObject::connect(fullExtent, &QAction::triggered, viewer, &GisViewer::fullExtent);
    QObject::connect(zoomBox, &QAction::triggered, viewer,
        [viewer] { viewer->setActiveTool(GisViewerTool::ZoomBox); });
    QObject::connect(pan, &QAction::triggered, viewer,
        [viewer] { viewer->setActiveTool(GisViewerTool::Pan); });
    QObject::connect(viewer, &GisViewer::drawingProgressChanged, &window,
        [progress](int value, const QString& text) {
            progress->setValue(std::clamp(value, 0, 100));
            progress->setFormat(QStringLiteral("%1% — %2").arg(std::clamp(value, 0, 100)).arg(text));
        });

    auto openBaseRaster = [viewer](const QString& path) {
        if (path.trimmed().isEmpty()) return;
        viewer->clearLayers();
        auto layer = std::make_unique<GisLayerTIFF>(path);
        layer->setName("Buildings RGB source");
        layer->open();
        viewer->addLayer(layer);
        viewer->fullExtent();
    };

    QObject::connect(browseModel, &QPushButton::clicked, &window, [&window, modelPath] {
        const QString path = QFileDialog::getExistingDirectory(&window, "Select GeoKernel model package", modelPath->text());
        if (!path.isEmpty()) modelPath->setText(QDir::toNativeSeparators(path));
    });
    QObject::connect(browseRaster, &QPushButton::clicked, &window, [&window, rasterPath, outputPath, openBaseRaster] {
        const QString path = QFileDialog::getOpenFileName(&window, "Select input raster", rasterPath->text(), "GeoTIFF (*.tif *.tiff)");
        if (path.isEmpty()) return;
        rasterPath->setText(QDir::toNativeSeparators(path));
        const QFileInfo input(path);
        outputPath->setText(QDir::toNativeSeparators(input.dir().filePath(input.completeBaseName() + "_building_mask.tif")));
        openBaseRaster(path);
    });
    QObject::connect(browseOutput, &QPushButton::clicked, &window, [&window, outputPath] {
        const QString path = QFileDialog::getSaveFileName(&window, "Save instance labels", outputPath->text(), "GeoTIFF (*.tif)");
        if (!path.isEmpty()) outputPath->setText(QDir::toNativeSeparators(path));
    });

    QObject::connect(run, &QPushButton::clicked, &window,
        [&window, viewer, status, modelPath, rasterPath, outputPath, provider, run, progress, details] {
        if (!QFileInfo::exists(modelPath->text()) || !QFileInfo::exists(rasterPath->text())) {
            QMessageBox::warning(&window, "BuildingSegmentationInference", "Select an existing model package and input raster.");
            return;
        }
        if (outputPath->text().trimmed().isEmpty()) {
            QMessageBox::warning(&window, "BuildingSegmentationInference", "Select an instance-label output path.");
            return;
        }

        run->setEnabled(false);
        progress->setValue(0);
        progress->setFormat("0% — Opening model package...");
        details->setPlainText("Validating the model package and preparing tiled inference...");
        auto result = std::make_shared<JobResult>();
        result->sourcePath = rasterPath->text();
        result->maskPath = outputPath->text();
        const QString packageDirectory = modelPath->text();
        const QString inputRaster = rasterPath->text();
        const auto selectedProvider = static_cast<AiExecutionProvider>(provider->currentData().toInt());

        auto* worker = QThread::create([result, packageDirectory, inputRaster, selectedProvider, progress] {
            try {
                const AiModelPackage package = AiModelPackage::open(packageDirectory);
                const QStringList errors = package.validationErrors();
                if (!errors.isEmpty()) throw std::runtime_error(errors.join("\n").toStdString());
                result->manifest = package.manifest();
                if (result->manifest.inputs.isEmpty() || result->manifest.outputs.isEmpty())
                    throw std::runtime_error("The model manifest does not define input and output tensors.");

                AiInstanceVectorizationRequest request;
                request.inference.modelPackagePath = packageDirectory;
                request.inference.rasterPath = inputRaster;
                request.inference.sessionOptions.provider = selectedProvider;
                request.labelRasterPath = result->maskPath;
                request.connectivity = 4;
                request.layerName = QStringLiteral("building_predictions");
                request.instanceIdField = QStringLiteral("instance_id");
                auto vectorized = AiInstanceVectorizationAlgorithm::runToMemory(
                    request, {}, [progress](const AnalysisProgress& state) {
                        QMetaObject::invokeMethod(progress, [progress, state] {
                            progress->setValue(state.percent);
                            progress->setFormat(QStringLiteral("%1% — %2").arg(state.percent).arg(state.message));
                        }, Qt::QueuedConnection);
                    });
                result->inference = vectorized.inference.labels;
                result->polygonCount = vectorized.polygons.polygonCount;
                result->predictionLayer = std::move(vectorized.polygons.layer);
            } catch (const std::exception& exception) {
                result->error = QString::fromUtf8(exception.what());
            }
        });
        QObject::connect(worker, &QThread::finished, &window,
            [&window, viewer, status, run, progress, details, result, worker] {
            worker->deleteLater();
            run->setEnabled(true);
            if (!result->error.isEmpty()) {
                progress->setValue(0);
                progress->setFormat("Inference failed");
                details->setPlainText("Inference failed:\n" + result->error);
                QMessageBox::critical(&window, "BuildingSegmentationInference", result->error);
                return;
            }
            try {
                progress->setValue(95);
                progress->setFormat("95% — Opening source and prediction overlay...");
                viewer->clearLayers();
                result->sourceLayer = std::make_unique<GisLayerTIFF>(result->sourcePath);
                result->sourceLayer->setName("NAIP RGB source");
                result->sourceLayer->open();
                viewer->addLayer(result->sourceLayer);
                GisLayerStyle predictionStyle;
                predictionStyle.setFillColor(QStringLiteral("#FF4020"));
                predictionStyle.setFillOpacity(125);
                predictionStyle.setLineColor(QStringLiteral("#B52312"));
                predictionStyle.setLineWidth(1.25F);
                if (!result->predictionLayer)
                    throw std::runtime_error("The in-memory building polygon layer was not created.");
                result->predictionLayer->setName("Building prediction polygons");
                result->predictionLayer->style() = predictionStyle;
                result->predictionLayer->setVisible(true);
                viewer->addLayer(result->predictionLayer);
                viewer->fullExtent();
                details->setPlainText(diagnostics(*result));
                progress->setValue(100);
                progress->setFormat("100% — Inference complete");
                status->showMessage(
                    QStringLiteral("Building mask and %1 vector polygons created in %2 ms.")
                        .arg(result->polygonCount)
                        .arg(result->inference.elapsedMilliseconds));
            } catch (const std::exception& exception) {
                result->error = QString::fromUtf8(exception.what());
                details->setPlainText("The mask was created, but the raster/vector preview could not be opened:\n" + result->error);
                QMessageBox::critical(&window, "BuildingSegmentationInference", result->error);
            }
        });
        worker->start();
    });

    window.show();
    QMetaObject::invokeMethod(&window, [&window, modelPath, rasterPath, outputPath, details, openBaseRaster] {
        details->setPlainText("Preparing the buildings raster and GeoKernel ONNX model package...");
        const QString raster = ensureSampleFile(
            QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/buildings.zip")),
            QStringLiteral("buildings.zip"),
            QStringLiteral("buildings"),
            QStringLiteral("buildings.tif"), &window);
        if (raster.isEmpty()) {
            details->setPlainText("The buildings input raster could not be prepared.");
            return;
        }

        const QString manifest = ensureSampleFile(
            QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/buildings-model.zip")),
            QStringLiteral("buildings-model.zip"),
            QStringLiteral("buildings-model"),
            QStringLiteral("geokernel-model.json"), &window);
        if (manifest.isEmpty()) {
            rasterPath->setText(QDir::toNativeSeparators(raster));
            const QFileInfo input(raster);
            outputPath->setText(QDir::toNativeSeparators(
                input.dir().filePath(input.completeBaseName() + "_building_mask.tif")));
            details->setPlainText(
                "The buildings raster is ready, but the ONNX model package is not published yet. "
                "Select a local GeoKernel model package or upload buildings-model.zip to release v1.");
            openBaseRaster(raster);
            return;
        }

        rasterPath->setText(QDir::toNativeSeparators(raster));
        modelPath->setText(QDir::toNativeSeparators(QFileInfo(manifest).absolutePath()));
        const QFileInfo input(raster);
        outputPath->setText(QDir::toNativeSeparators(
            input.dir().filePath(input.completeBaseName() + "_building_mask.tif")));
        try {
            openBaseRaster(raster);
            details->setPlainText(
                "The buildings raster and ONNX model package are ready. Run inference to add building polygons.");
        } catch (const std::exception& exception) {
            details->setPlainText("The sample files are ready, but the input raster could not be opened:\n" +
                QString::fromUtf8(exception.what()));
        }
    }, Qt::QueuedConnection);
    return app.exec();
}
