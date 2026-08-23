#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
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
#include "Viewer/GisViewer.h"

using namespace GeoKernel::AI;
using namespace GeoKernel::Analysis;
using namespace GeoKernel::Formats::Raster::Tiff;
using namespace GeoKernel::Viewer;

namespace {
struct JobResult {
    QString error;
    QString sourcePath;
    AiModelManifest manifest;
    AiRasterObjectDetectionLayerResult detection;
    std::unique_ptr<GisLayerTIFF> sourceLayer;
};

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

QString diagnostics(const JobResult& result) {
    const auto& inference = result.detection.inference;
    QString text = QStringLiteral(
        "GeoKernel AI object detection\n\n"
        "Model: %1 %2\nProvider: %3\nRaster: %4 x %5\n"
        "Elapsed: %6 ms\n\nDetections: %7\nSkipped: %8\nVector output: in-memory layer")
        .arg(result.manifest.id, result.manifest.version,
             aiExecutionProviderName(inference.inference.provider))
        .arg(inference.rasterWidth).arg(inference.rasterHeight)
        .arg(inference.inference.elapsedMilliseconds)
        .arg(result.detection.materialization.detectionCount)
        .arg(result.detection.materialization.skippedCount);
    if (!result.detection.materialization.warnings.isEmpty())
        text += "\n\nWarnings:\n" + result.detection.materialization.warnings.join("\n");
    return text;
}
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QMainWindow window;
    window.resize(1280, 820);
    window.setWindowTitle("ObjectDetectionInference");
    window.setWindowIcon(QIcon(":/icons/geokernel.ico"));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);
    auto* status = window.statusBar();
    status->showMessage("Select an object-detection model package and a GeoTIFF.");

    auto* panel = new QWidget(&window);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->addWidget(new QLabel("<b>Generic object detection inference</b>", panel));
    auto* description = new QLabel(
        "Run any compatible GeoKernel object_detection ONNX package and draw "
        "georeferenced bounding boxes as an in-memory layer.", panel);
    description->setWordWrap(true);
    panelLayout->addWidget(description);

    auto* form = new QFormLayout;
    QLineEdit *modelPath = nullptr, *rasterPath = nullptr;
    QPushButton *browseModel = nullptr, *browseRaster = nullptr;
    form->addRow("Model package", pathEditor(panel, modelPath, browseModel));
    form->addRow("Input raster", pathEditor(panel, rasterPath, browseRaster));
    auto* provider = new QComboBox(panel);
    provider->addItem("Auto", static_cast<int>(AiExecutionProvider::Auto));
    provider->addItem("CPU", static_cast<int>(AiExecutionProvider::CPU));
    provider->addItem("CUDA", static_cast<int>(AiExecutionProvider::CUDA));
    provider->addItem("DirectML", static_cast<int>(AiExecutionProvider::DirectML));
    form->addRow("Execution provider", provider);
    panelLayout->addLayout(form);

    auto* run = new QPushButton("Run object detection inference", panel);
    panelLayout->addWidget(run);
    auto* progress = new QProgressBar(panel);
    progress->setRange(0, 100);
    progress->setValue(0);
    panelLayout->addWidget(progress);
    panelLayout->addWidget(new QLabel("Inference diagnostics", panel));
    auto* details = new QTextEdit(panel);
    details->setReadOnly(true);
    details->setPlainText("The package manifest must declare task=object_detection and boxes, scores and classes outputs.");
    panelLayout->addWidget(details, 1);
    auto* legend = new QLabel("<b>Detection layer</b><br><font color='#ff4020'>■</font> Bounding box", panel);
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
    QObject::connect(zoomBox, &QAction::triggered, viewer, [viewer] { viewer->setActiveTool(GisViewerTool::ZoomBox); });
    QObject::connect(pan, &QAction::triggered, viewer, [viewer] { viewer->setActiveTool(GisViewerTool::Pan); });
    QObject::connect(viewer, &GisViewer::drawingProgressChanged, &window,
        [progress](int value, const QString& text) {
            progress->setValue(std::clamp(value, 0, 100));
            progress->setFormat(QStringLiteral("%1% — %2").arg(std::clamp(value, 0, 100)).arg(text));
        });

    QObject::connect(browseModel, &QPushButton::clicked, &window, [&window, modelPath] {
        const QString path = QFileDialog::getExistingDirectory(&window, "Select GeoKernel object-detection package", modelPath->text());
        if (!path.isEmpty()) modelPath->setText(QDir::toNativeSeparators(path));
    });
    QObject::connect(browseRaster, &QPushButton::clicked, &window, [&window, rasterPath] {
        const QString path = QFileDialog::getOpenFileName(&window, "Select input raster", rasterPath->text(), "GeoTIFF (*.tif *.tiff)");
        if (!path.isEmpty()) rasterPath->setText(QDir::toNativeSeparators(path));
    });

    QObject::connect(run, &QPushButton::clicked, &window,
        [&window, viewer, status, modelPath, rasterPath, provider, run, progress, details] {
        if (!QFileInfo::exists(modelPath->text()) || !QFileInfo::exists(rasterPath->text())) {
            QMessageBox::warning(&window, "ObjectDetectionInference", "Select an existing model package and input raster.");
            return;
        }
        run->setEnabled(false);
        progress->setValue(0);
        progress->setFormat("0% — Opening model package...");
        details->setPlainText("Validating the package and preparing raster inference...");
        auto result = std::make_shared<JobResult>();
        result->sourcePath = rasterPath->text();
        const QString packageDirectory = modelPath->text();
        const QString inputRaster = rasterPath->text();
        const auto selectedProvider = static_cast<AiExecutionProvider>(provider->currentData().toInt());

        auto* worker = QThread::create([result, packageDirectory, inputRaster, selectedProvider, progress] {
            try {
                const AiModelPackage package = AiModelPackage::open(packageDirectory);
                const QStringList errors = package.validationErrors();
                if (!errors.isEmpty()) throw std::runtime_error(errors.join("\n").toStdString());
                result->manifest = package.manifest();
                AiRasterObjectDetectionLayerRequest request;
                request.inference.modelPackagePath = packageDirectory;
                request.inference.rasterPath = inputRaster;
                request.inference.sessionOptions.provider = selectedProvider;
                request.layerName = QStringLiteral("object_detections");
                result->detection = AiRasterObjectDetectionLayerAlgorithm::run(
                    request, [progress](const AiRasterObjectDetectionProgress& state) {
                        QMetaObject::invokeMethod(progress, [progress, state] {
                            progress->setValue(state.percent);
                            progress->setFormat(QStringLiteral("%1% — %2").arg(state.percent).arg(state.message));
                        }, Qt::QueuedConnection);
                    });
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
                QMessageBox::critical(&window, "ObjectDetectionInference", result->error);
                return;
            }
            try {
                progress->setValue(95);
                progress->setFormat("95% — Opening source and detection overlay...");
                viewer->clearLayers();
                result->sourceLayer = std::make_unique<GisLayerTIFF>(result->sourcePath);
                result->sourceLayer->setName("Detection source raster");
                result->sourceLayer->open();
                viewer->addLayer(result->sourceLayer);
                auto& layer = result->detection.materialization.layer;
                if (!layer) throw std::runtime_error("The in-memory detection layer was not created.");
                GisLayerStyle style;
                style.setFillColor(QStringLiteral("#FF4020"));
                style.setFillOpacity(90);
                style.setLineColor(QStringLiteral("#B52312"));
                style.setLineWidth(1.5F);
                layer->style() = style;
                layer->setVisible(true);
                viewer->addLayer(layer);
                viewer->fullExtent();
                details->setPlainText(diagnostics(*result));
                progress->setValue(100);
                progress->setFormat("100% — Inference complete");
                status->showMessage(QStringLiteral("%1 detection bounding boxes drawn from memory in %2 ms.")
                    .arg(result->detection.materialization.detectionCount)
                    .arg(result->detection.inference.inference.elapsedMilliseconds));
            } catch (const std::exception& exception) {
                const QString error = QString::fromUtf8(exception.what());
                details->setPlainText("Inference completed, but the preview could not be opened:\n" + error);
                QMessageBox::critical(&window, "ObjectDetectionInference", error);
            }
        });
        worker->start();
    });

    window.show();
    return app.exec();
}
