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
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTextEdit>
#include <QThread>
#include <QTemporaryDir>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include "GeoKernelAI.h"
#include "GeoKernelAnalysis.h"
#include "Raster/Tiff/GisLayerTIFF.h"
#include "SampleSupport.h"
#include "Symbology/GisCategorizedSymbolRenderer.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::AI;
using namespace GeoKernel::Analysis;
using namespace GeoKernel::Core::Symbology;
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

double intersectionOverUnion(const AiDetection& left, const AiDetection& right) {
    const double width = std::max(0.0, std::min(left.maxX, right.maxX) - std::max(left.minX, right.minX));
    const double height = std::max(0.0, std::min(left.maxY, right.maxY) - std::max(left.minY, right.minY));
    const double intersection = width * height;
    const double leftArea = std::max(0.0, left.maxX - left.minX) * std::max(0.0, left.maxY - left.minY);
    const double rightArea = std::max(0.0, right.maxX - right.minX) * std::max(0.0, right.maxY - right.minY);
    const double unionArea = leftArea + rightArea - intersection;
    return unionArea > 0.0 ? intersection / unionArea : 0.0;
}

QVector<AiDetection> classAwareNms(QVector<AiDetection> detections, double threshold) {
    std::sort(detections.begin(), detections.end(), [](const AiDetection& left, const AiDetection& right) {
        return left.score > right.score;
    });
    QVector<AiDetection> kept;
    for (const AiDetection& candidate : detections) {
        bool suppressed = false;
        for (const AiDetection& accepted : kept) {
            if (candidate.classIndex == accepted.classIndex &&
                intersectionOverUnion(candidate, accepted) > threshold) {
                suppressed = true;
                break;
            }
        }
        if (!suppressed) kept.append(candidate);
    }
    return kept;
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
    status->showMessage("Preparing the NWPU-VHR-10 sample images and model...");

    auto* panel = new QWidget(&window);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->addWidget(new QLabel("<b>Generic object detection inference</b>", panel));
    auto* description = new QLabel(
        "Run the NWPU-VHR-10 ONNX package and draw detected aerial objects "
        "as an on-the-fly vector layer.", panel);
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

    auto* imageNavigation = new QHBoxLayout;
    auto* previousImage = new QPushButton("Previous image", panel);
    auto* imagePosition = new QLabel("No sample images", panel);
    imagePosition->setAlignment(Qt::AlignCenter);
    auto* nextImage = new QPushButton("Next image", panel);
    previousImage->setEnabled(false);
    nextImage->setEnabled(false);
    imageNavigation->addWidget(previousImage);
    imageNavigation->addWidget(imagePosition, 1);
    imageNavigation->addWidget(nextImage);
    panelLayout->addLayout(imageNavigation);

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
    auto* legend = new QLabel(
        "<b>NWPU-VHR-10 classes</b><br>"
        "<font color='#E53935'>■</font> airplane &nbsp; "
        "<font color='#1E88E5'>■</font> ship &nbsp; "
        "<font color='#FB8C00'>■</font> storage tank<br>"
        "<font color='#43A047'>■</font> baseball diamond &nbsp; "
        "<font color='#8E24AA'>■</font> tennis court<br>"
        "<font color='#D81B60'>■</font> basketball court &nbsp; "
        "<font color='#00ACC1'>■</font> ground track field<br>"
        "<font color='#FDD835'>■</font> harbor &nbsp; "
        "<font color='#3949AB'>■</font> bridge &nbsp; "
        "<font color='#6D4C41'>■</font> vehicle", panel);
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
    QStringList sampleImages;
    int currentImageIndex = -1;
    std::unique_ptr<GisLayerTIFF> previewLayer;

    auto openSourceImage = [viewer, &previewLayer](const QString& path) {
        viewer->clearLayers();
        previewLayer = std::make_unique<GisLayerTIFF>(path);
        previewLayer->setName("NWPU-VHR-10 source image");
        previewLayer->open();
        viewer->addLayer(previewLayer);
        viewer->fullExtent();
    };

    std::function<void(int)> selectImage =
        [&, rasterPath, imagePosition, previousImage, nextImage](int index) {
            if (index < 0 || index >= sampleImages.size()) return;
            currentImageIndex = index;
            const QString path = sampleImages.at(index);
            rasterPath->setText(QDir::toNativeSeparators(path));
            imagePosition->setText(QStringLiteral("%1 / %2 — %3")
                .arg(index + 1).arg(sampleImages.size()).arg(QFileInfo(path).fileName()));
            previousImage->setEnabled(index > 0);
            nextImage->setEnabled(index + 1 < sampleImages.size());
            openSourceImage(path);
            progress->setValue(0);
            progress->setFormat("Ready");
            details->setPlainText("Image selected. Run inference to create its on-the-fly detection layer.");
        };

    QObject::connect(previousImage, &QPushButton::clicked, &window,
        [&] { selectImage(currentImageIndex - 1); });
    QObject::connect(nextImage, &QPushButton::clicked, &window,
        [&] { selectImage(currentImageIndex + 1); });

    QObject::connect(browseRaster, &QPushButton::clicked, &window, [&window, rasterPath, openSourceImage] {
        const QString path = QFileDialog::getOpenFileName(&window, "Select input image", rasterPath->text(),
            "Raster images (*.jpg *.jpeg *.png *.tif *.tiff)");
        if (!path.isEmpty()) rasterPath->setText(QDir::toNativeSeparators(path));
        if (!path.isEmpty()) openSourceImage(path);
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
                const QImage source(inputRaster);
                if (source.isNull()) throw std::runtime_error("The input image could not be decoded.");
                constexpr int windowSize = 512;
                constexpr int overlap = 256;
                constexpr int stride = windowSize - overlap;
                const int stepsX = std::max(1, static_cast<int>(std::ceil(
                    static_cast<double>(source.width() - windowSize) / stride)) + 1);
                const int stepsY = std::max(1, static_cast<int>(std::ceil(
                    static_cast<double>(source.height() - windowSize) / stride)) + 1);
                const int totalTiles = stepsX * stepsY;
                QTemporaryDir temporaryTiles;
                if (!temporaryTiles.isValid()) throw std::runtime_error("A temporary tile directory could not be created.");

                QVector<AiDetection> tileDetections;
                qint64 totalElapsed = 0;
                AiExecutionProvider actualProvider = selectedProvider;
                int completedTiles = 0;
                for (int tileY = 0; tileY < stepsY; ++tileY) {
                    const int y = std::max(0, std::min(tileY * stride, source.height() - windowSize));
                    for (int tileX = 0; tileX < stepsX; ++tileX) {
                        const int x = std::max(0, std::min(tileX * stride, source.width() - windowSize));
                        const int width = std::min(windowSize, source.width() - x);
                        const int height = std::min(windowSize, source.height() - y);
                        const QString tilePath = temporaryTiles.filePath(
                            QStringLiteral("tile_%1_%2.png").arg(tileY).arg(tileX));
                        if (!source.copy(x, y, width, height).save(tilePath, "PNG"))
                            throw std::runtime_error("A temporary inference tile could not be written.");

                        AiRasterObjectDetectionRequest tileRequest;
                        tileRequest.modelPackagePath = packageDirectory;
                        tileRequest.rasterPath = tilePath;
                        tileRequest.sessionOptions.provider = selectedProvider;
                        try {
                            auto tileResult = AiRasterObjectDetectionExecutor::run(tileRequest);
                            actualProvider = tileResult.inference.provider;
                            totalElapsed += tileResult.inference.elapsedMilliseconds;
                            for (AiDetection detection : tileResult.pixelDetections) {
                                detection.minX += x;
                                detection.maxX += x;
                                detection.minY += y;
                                detection.maxY += y;
                                tileDetections.append(std::move(detection));
                            }
                        } catch (const std::exception& exception) {
                            // TorchVision detection exports use a dynamic N dimension.
                            // A tile with no detections returns [0,4], [0], [0].
                            // GeoKernel SDK 1.5.14 rejects zero runtime dimensions,
                            // so interpret only that exact condition as an empty tile.
                            const QString error = QString::fromUtf8(exception.what());
                            if (!error.contains(QStringLiteral("Runtime tensor dimensions must be positive"),
                                    Qt::CaseInsensitive))
                                throw;
                        }
                        ++completedTiles;
                        const int percent = 10 + (completedTiles * 75 / totalTiles);
                        QMetaObject::invokeMethod(progress, [progress, percent, completedTiles, totalTiles] {
                            progress->setValue(percent);
                            progress->setFormat(QStringLiteral("%1% — Tile %2 / %3")
                                .arg(percent).arg(completedTiles).arg(totalTiles));
                        }, Qt::QueuedConnection);
                    }
                }

                result->detection.inference.inference.provider = actualProvider;
                result->detection.inference.inference.elapsedMilliseconds = totalElapsed;
                result->detection.inference.rasterWidth = source.width();
                result->detection.inference.rasterHeight = source.height();
                result->detection.inference.pixelDetections = classAwareNms(std::move(tileDetections), 0.3);
                AiDetectionLayerRequest materialization;
                materialization.detections = result->detection.inference.pixelDetections;
                materialization.geoTransform = result->detection.inference.geoTransform;
                const auto isDefaultIdentity = [&materialization] {
                    if (materialization.geoTransform.size() != 6) return false;
                    const QVector<double> identity{0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
                    for (int index = 0; index < identity.size(); ++index)
                        if (std::abs(materialization.geoTransform[index] - identity[index]) > 1.0e-12)
                            return false;
                    return true;
                }();
                const bool hasNoGeoreferencing =
                    materialization.geoTransform.size() != 6 ||
                    (result->detection.inference.projectionWkt.trimmed().isEmpty() && isDefaultIdentity);
                if (hasNoGeoreferencing) {
                    // Plain JPG/PNG files do not carry a GDAL geotransform.
                    // GisLayerGdalRaster displays their pixel rows below Y=0,
                    // while detections use positive top-left pixel rows.
                    materialization.geoTransform = {
                        0.0, 1.0, 0.0,
                        0.0,
                        0.0, -1.0
                    };
                }
                materialization.coordinateSystemWkt = result->detection.inference.projectionWkt;
                materialization.layerName = QStringLiteral("object_detections");
                materialization.buildSpatialIndex = true;
                result->detection.materialization =
                    AiDetectionLayerMaterializer::materialize(materialization);
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
                style.setShowLabels(true);
                style.setLabelField(QStringLiteral("label"));
                style.setLabelFontSize(11.0F);
                style.setLabelColor(QStringLiteral("#FFFFFF"));
                style.setLabelHaloEnabled(true);
                style.setLabelHaloColor(QStringLiteral("#202020"));
                style.setLabelHaloWidth(2.0F);
                style.setLabelAllowOverlap(true);
                layer->style() = style;
                auto renderer = std::make_unique<GisCategorizedSymbolRenderer>(
                    QStringLiteral("class_id"));
                const struct DetectionClassStyle {
                    int id;
                    const char* name;
                    const char* color;
                } classStyles[] = {
                    {1, "airplane", "#E53935"},
                    {2, "ship", "#1E88E5"},
                    {3, "storage_tank", "#FB8C00"},
                    {4, "baseball_diamond", "#43A047"},
                    {5, "tennis_court", "#8E24AA"},
                    {6, "basketball_court", "#D81B60"},
                    {7, "ground_track_field", "#00ACC1"},
                    {8, "harbor", "#FDD835"},
                    {9, "bridge", "#3949AB"},
                    {10, "vehicle", "#6D4C41"},
                };
                for (const auto& definition : classStyles) {
                    GisLayerStyle classStyle = style;
                    classStyle.setFillColor(QString::fromLatin1(definition.color));
                    classStyle.setLineColor(QString::fromLatin1(definition.color));
                    renderer->addClass({
                        definition.id,
                        classStyle,
                        QString::fromLatin1(definition.name),
                        true
                    });
                }
                renderer->setDefaultStyle(style);
                layer->setSymbolRenderer(std::move(renderer));
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
    QMetaObject::invokeMethod(&window,
        [&window, modelPath, rasterPath, details, status, &sampleImages, &selectImage] {
            details->setPlainText("Downloading and extracting the NWPU-VHR-10 sample images...");
            const QString firstImage = ensureSampleFile(
                QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/object_detection_images.zip")),
                QStringLiteral("object_detection_images.zip"),
                QStringLiteral("object-detection-images"),
                QStringLiteral("1.jpg"), &window);
            if (firstImage.isEmpty()) {
                details->setPlainText("The object-detection sample images could not be prepared.");
                return;
            }

            details->setPlainText("Downloading and extracting the GeoKernel object-detection model...");
            const QString manifest = ensureSampleFile(
                QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/object_detection_model.zip")),
                QStringLiteral("object_detection_model.zip"),
                QStringLiteral("object-detection-model"),
                QStringLiteral("geokernel-model.json"), &window);
            if (manifest.isEmpty()) {
                rasterPath->setText(QDir::toNativeSeparators(firstImage));
                details->setPlainText("The images are ready, but the ONNX model package could not be prepared.");
                return;
            }

            const QDir imageDirectory(QFileInfo(firstImage).absolutePath());
            sampleImages = imageDirectory.entryList(
                QStringList() << "*.jpg" << "*.jpeg" << "*.png" << "*.tif" << "*.tiff",
                QDir::Files, QDir::Name);
            for (QString& image : sampleImages)
                image = imageDirectory.absoluteFilePath(image);
            std::sort(sampleImages.begin(), sampleImages.end(), [](const QString& left, const QString& right) {
                return QFileInfo(left).completeBaseName().toInt() < QFileInfo(right).completeBaseName().toInt();
            });
            if (sampleImages.isEmpty()) {
                details->setPlainText("The image archive did not contain supported raster images.");
                return;
            }

            modelPath->setText(QDir::toNativeSeparators(QFileInfo(manifest).absolutePath()));
            selectImage(0);
            details->setPlainText(
                QStringLiteral("%1 sample images and the ONNX model are ready. Run inference, then use Previous/Next to change images.")
                    .arg(sampleImages.size()));
            status->showMessage(QStringLiteral("NWPU-VHR-10 object-detection sample ready."));
        }, Qt::QueuedConnection);
    return app.exec();
}
