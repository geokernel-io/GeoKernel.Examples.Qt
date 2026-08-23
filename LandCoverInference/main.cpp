#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
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
#include <QSlider>
#include <QStatusBar>
#include <QTextEdit>
#include <QThread>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <memory>
#include "GeoKernelAI.h"
#include "Raster/Tiff/GisLayerTIFF.h"
#include "SampleSupport.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::AI;
using namespace GeoKernel::Formats::Raster::Tiff;
using namespace GeoKernel::Viewer;

struct JobResult {
    QString error;
    QString maskPath;
    QString previewPath;
    AiModelManifest manifest;
    AiRasterInferenceResult inference;
    std::unique_ptr<GisLayerTIFF> layer;
};

struct ClassColor { 
    int code; 
    int red; 
    int green; 
    int blue; 
};

constexpr std::array<ClassColor, 11> WorldCoverColors {
    {
        {10, 0, 100, 0}, {20, 255, 187, 34}, {30, 255, 255, 76},
        {40, 240, 150, 255}, {50, 250, 0, 0}, {60, 180, 180, 180},
        {70, 240, 240, 240}, {80, 0, 100, 200}, {90, 0, 150, 160},
        {95, 0, 207, 117}, {100, 250, 230, 160}
    }
};

QString existingPath(const QString& candidate) {
    return QFileInfo::exists(candidate) ? QDir::toNativeSeparators(candidate) : QString{};
}

QVector<int> classCodes(const AiModelManifest& manifest) {
    QVector<int> codes;
    const QVariant value = manifest.metadata.value(QStringLiteral("classCodes"));
    for (const QVariant& item : value.toList()) codes.push_back(item.toInt());
    return codes;
}

ClassColor colorForCode(int code) {
    for (const auto& color : WorldCoverColors) if (color.code == code) return color;
    return { code, 128, 128, 128 };
}

void remapClassIndices(AiRasterInferenceResult& result, const QVector<int>& codes) {
    if (result.dataType != AiTensorDataType::Int32 || result.bandCount != 1 || codes.isEmpty()) return;
    auto* pixels = reinterpret_cast<qint32*>(result.data.data());
    const qint64 count = static_cast<qint64>(result.width) * result.height;
    for (qint64 index = 0; index < count; ++index) {
        const int classIndex = pixels[index];
        if (classIndex >= 0 && classIndex < codes.size()) pixels[index] = codes[classIndex];
    }
}

AiRasterInferenceResult colorized(const AiRasterInferenceResult& mask, int opacityPercent = 100) {
    AiRasterInferenceResult preview = mask;
    preview.bandCount = 4;
    preview.dataType = AiTensorDataType::Float32;
    const qint64 count = static_cast<qint64>(mask.width) * mask.height;
    preview.data.resize(static_cast<qsizetype>(count * 4 * sizeof(float)));
    const auto* source = reinterpret_cast<const qint32*>(mask.data.constData());
    auto* destination = reinterpret_cast<float*>(preview.data.data());
    const float alpha = static_cast<float>(std::clamp(opacityPercent, 0, 100) * 255.0 / 100.0);
    for (qint64 index = 0; index < count; ++index) {
        const ClassColor color = colorForCode(source[index]);
        destination[index] = static_cast<float>(color.red);
        destination[count + index] = static_cast<float>(color.green);
        destination[count * 2 + index] = static_cast<float>(color.blue);
        destination[count * 3 + index] = alpha;
    }
    return preview;
}

QString diagnostics(const JobResult& result) {
    return QStringLiteral(
        "GeoKernel AI land-cover inference\n\n"
        "Model: %1 %2\nProvider: %3\nRaster: %4 x %5\nTiles: %6\nElapsed: %7 ms\n\n"
        "Class mask:\n%8\n\nColor preview:\n%9")
        .arg(result.manifest.id, result.manifest.version,
            aiExecutionProviderName(result.inference.provider))
        .arg(result.inference.width).arg(result.inference.height)
        .arg(result.inference.processedTiles).arg(result.inference.elapsedMilliseconds)
        .arg(QDir::toNativeSeparators(result.maskPath), QDir::toNativeSeparators(result.previewPath));
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

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QMainWindow window;
    window.resize(1280, 820);
    window.setWindowTitle("LandCoverInference");
    window.setWindowIcon(QIcon(":/icons/geokernel.ico"));

    // Create the status bar before the viewer gets its first extent. Creating
    // it lazily after inference changes the central widget height, which makes
    // an otherwise stable map appear to pan or zoom when the prediction layer
    // is added.
    window.statusBar()->setSizeGripEnabled(false);
    window.statusBar()->showMessage("Map ready.");

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* panel = new QWidget(&window);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->addWidget(new QLabel("<b>Land-cover inference</b>", panel));
    panelLayout->addWidget(new QLabel("Run a GeoKernel model package on a georeferenced RGBNIR raster.", panel));

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
    auto* predictionOpacity = new QSlider(Qt::Horizontal, panel);
    predictionOpacity->setRange(0, 100);
    predictionOpacity->setValue(100);
    predictionOpacity->setEnabled(false);
    predictionOpacity->setToolTip("Transparency of the land-cover prediction layer");
    auto* opacityValue = new QLabel("100%", panel);
    auto* opacityEditor = new QWidget(panel);
    auto* opacityLayout = new QHBoxLayout(opacityEditor);
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    opacityLayout->addWidget(predictionOpacity, 1);
    opacityLayout->addWidget(opacityValue);
    form->addRow("Prediction opacity", opacityEditor);
    panelLayout->addLayout(form);

    auto* run = new QPushButton("Run land-cover inference", panel);
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
        "<b>ESA WorldCover classes</b><br>"
        "<font color='#006400'>●</font> Tree cover &nbsp; "
        "<font color='#ffbb22'>●</font> Shrubland &nbsp; "
        "<font color='#ffff4c'>●</font> Grassland<br>"
        "<font color='#f096ff'>●</font> Cropland &nbsp; "
        "<font color='#fa0000'>●</font> Built-up &nbsp; "
        "<font color='#b4b4b4'>●</font> Bare / sparse<br>"
        "<font color='#0064c8'>●</font> Permanent water");
    legend->setWordWrap(true);
    panelLayout->addWidget(legend);

    auto* dock = new QDockWidget("GeoKernel AI", &window);
    // Keep the map viewport stable while progress and diagnostic texts change.
    // A minimum width alone lets the dock grow from its content size hint,
    // which resizes the viewer and looks like a small zoom-out when the
    // prediction layer is added.
    dock->setFixedWidth(420);
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
    zoomBox->setCheckable(true); 
    tools.addAction(zoomBox);
    
    auto* pan = toolbar->addAction(QIcon(":/icons/pan.svg"), "Pan");
    pan->setCheckable(true);
    pan->setChecked(true);
    tools.addAction(pan);
    
    QObject::connect(zoomIn, &QAction::triggered, viewer, &GisViewer::zoomIn);
    QObject::connect(zoomOut, &QAction::triggered, viewer, &GisViewer::zoomOut);
    QObject::connect(fullExtent, &QAction::triggered, viewer, &GisViewer::fullExtent);
    QObject::connect(zoomBox, &QAction::triggered, viewer, [viewer] { viewer->setActiveTool(GisViewerTool::ZoomBox); });    
    QObject::connect(pan, &QAction::triggered, viewer, [viewer] { viewer->setActiveTool(GisViewerTool::Pan); });    
    // This progress bar belongs to inference. Updating it for every viewer
    // render causes the dock (and, on some systems, the whole top-level
    // window) to repaint during zoom and pan, which appears as a short blink.
    // The viewer already manages its own asynchronous drawing lifecycle.
    auto openBaseRaster = [viewer](const QString& path) {
        if (path.trimmed().isEmpty()) return;
        viewer->clearLayers();
        auto layer = std::make_unique<GisLayerTIFF>(path);
        layer->setName("Input RGBNIR raster");
        layer->open();
        viewer->addLayer(layer);
        viewer->fullExtent();
    };

    QObject::connect(browseModel, &QPushButton::clicked, &window, [&window, modelPath] {
        const QString path = QFileDialog::getExistingDirectory(&window, "Select GeoKernel model package", modelPath->text());
        if (!path.isEmpty()) modelPath->setText(QDir::toNativeSeparators(path));
    });
    QObject::connect(browseRaster, &QPushButton::clicked, &window, [&window, rasterPath, openBaseRaster] {
        const QString path = QFileDialog::getOpenFileName(&window, "Select input raster", rasterPath->text(), "GeoTIFF (*.tif *.tiff)");
        if (path.isEmpty()) return;
        rasterPath->setText(QDir::toNativeSeparators(path));
        openBaseRaster(path);
    });
    QObject::connect(rasterPath, &QLineEdit::editingFinished, &window,
        [rasterPath, openBaseRaster] { openBaseRaster(rasterPath->text()); });
    auto displayedResult = std::make_shared<std::shared_ptr<JobResult>>();

    QObject::connect(predictionOpacity, &QSlider::valueChanged, opacityValue,
        [opacityValue](int value) { opacityValue->setText(QStringLiteral("%1%").arg(value)); });
    QObject::connect(predictionOpacity, &QSlider::sliderReleased, &window,
        [viewer, predictionOpacity, displayedResult, details, progress] {
        if (!*displayedResult) return;
        try {
            progress->setValue(95);
            progress->setFormat("95% — Updating prediction opacity...");
            viewer->removeLayerByName("Land-cover prediction");
            const AiRasterInferenceResult preview = colorized(
                (*displayedResult)->inference, predictionOpacity->value());
            AiRasterInferenceExecutor::writeGeoTiff(preview, (*displayedResult)->previewPath);
            (*displayedResult)->layer = std::make_unique<GisLayerTIFF>((*displayedResult)->previewPath);
            (*displayedResult)->layer->setName("Land-cover prediction");
            (*displayedResult)->layer->open();
            viewer->addLayer((*displayedResult)->layer);
            progress->setValue(100);
            progress->setFormat(QStringLiteral("100% — Prediction opacity: %1%").arg(predictionOpacity->value()));
        } catch (const std::exception& exception) {
            details->append("\nOpacity update failed: " + QString::fromUtf8(exception.what()));
        }
    });
    QObject::connect(run, &QPushButton::clicked, &window,
        [&window, viewer, modelPath, rasterPath, provider, run, progress, details,
         predictionOpacity, displayedResult, openBaseRaster] {
        if (!QFileInfo::exists(modelPath->text()) || !QFileInfo::exists(rasterPath->text())) {
            QMessageBox::warning(&window, "LandCoverInference", "Select an existing model package and input raster.");
            return;
        }

        run->setEnabled(false);
        progress->setValue(0);
        progress->setFormat("0% — Opening model package...");
        details->setPlainText("Validating the model package and preparing tiled inference...");
        auto result = std::make_shared<JobResult>();
        QDir outputDirectory(QCoreApplication::applicationDirPath());
        if (!outputDirectory.mkpath("outputs") || !outputDirectory.cd("outputs")) {
            QMessageBox::critical(&window, "LandCoverInference", "The automatic output directory could not be created.");
            run->setEnabled(true);
            return;
        }
        const QFileInfo inputFile(rasterPath->text());
        const QString runId = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz");
        result->maskPath = outputDirectory.filePath(
            QStringLiteral("%1_landcover_mask_%2.tif").arg(inputFile.completeBaseName(), runId));
        const QFileInfo maskFile(result->maskPath);
        result->previewPath = maskFile.dir().filePath(maskFile.completeBaseName() + "_preview.tif");
        const QString packageDirectory = modelPath->text();
        const QString inputRaster = rasterPath->text();
        const auto selectedProvider = static_cast<AiExecutionProvider>(provider->currentData().toInt());
        const int selectedOpacity = predictionOpacity->value();

        auto* worker = QThread::create([result, packageDirectory, inputRaster, selectedProvider,
                                        selectedOpacity, progress] {
            try {
                const AiModelPackage package = AiModelPackage::open(packageDirectory);
                const QStringList errors = package.validationErrors();
                if (!errors.isEmpty()) throw std::runtime_error(errors.join("\n").toStdString());
                result->manifest = package.manifest();
                const QVector<int> outputClassCodes = classCodes(result->manifest);
                if (result->manifest.inputs.isEmpty() || result->manifest.outputs.isEmpty())
                    throw std::runtime_error("The model manifest does not define input and output tensors.");

                AiRasterInferenceRequest request;
                request.modelPath = package.modelPath();
                request.rasterPath = inputRaster;
                request.inputName = result->manifest.inputs.first().name;
                request.outputName = result->manifest.outputs.first().name;
                request.bands = {1, 2, 3, 4};
                request.tiling = result->manifest.tiling;
                request.normalization = result->manifest.normalization;
                request.sessionOptions.provider = selectedProvider;
                request.outputMode = AiRasterOutputMode::ClassMask;
                result->inference = AiRasterInferenceExecutor::run(request,
                    [progress](const AiRasterInferenceProgress& state) {
                        QMetaObject::invokeMethod(progress, [progress, state] {
                            progress->setValue(state.percent);
                            progress->setFormat(QStringLiteral("%1% — %2").arg(state.percent).arg(state.message));
                        }, Qt::QueuedConnection);
                    });
                remapClassIndices(result->inference, outputClassCodes);
                AiRasterInferenceExecutor::writeGeoTiff(result->inference, result->maskPath);
                const AiRasterInferenceResult preview = colorized(result->inference, selectedOpacity);
                AiRasterInferenceExecutor::writeGeoTiff(preview, result->previewPath);
            } catch (const std::exception& exception) {
                result->error = QString::fromUtf8(exception.what());
            }
        });

        QObject::connect(worker, &QThread::finished, &window,
            [&window, viewer, run, progress, details, predictionOpacity,
             displayedResult, openBaseRaster, rasterPath, result, worker] {
            worker->deleteLater();
            run->setEnabled(true);
            if (!result->error.isEmpty()) {
                progress->setValue(0);
                progress->setFormat("Inference failed");
                details->setPlainText("Inference failed:\n" + result->error);
                QMessageBox::critical(&window, "LandCoverInference", result->error);
                return;
            }
            try {
                progress->setValue(95);
                progress->setFormat("95% — Opening color preview...");
                viewer->removeLayerByName("Land-cover prediction");
                if (viewer->layerCount() == 0)
                    openBaseRaster(rasterPath->text());
                result->layer = std::make_unique<GisLayerTIFF>(result->previewPath);
                result->layer->setName("Land-cover prediction");
                result->layer->open();
                viewer->addLayer(result->layer);
                *displayedResult = result;
                predictionOpacity->setEnabled(true);
                details->setPlainText(diagnostics(*result));
                progress->setValue(100);
                progress->setFormat("100% — Inference complete");
                window.statusBar()->showMessage(
                    QStringLiteral("Land-cover mask created in %1 ms.").arg(result->inference.elapsedMilliseconds));
            } catch (const std::exception& exception) {
                result->error = QString::fromUtf8(exception.what());
                details->setPlainText("The mask was created, but the preview could not be opened:\n" + result->error);
                QMessageBox::critical(&window, "LandCoverInference", result->error);
            }
        });

        worker->start();
    });

    window.show();
    QMetaObject::invokeMethod(&window, [&window, modelPath, rasterPath, details, openBaseRaster] {
        details->setPlainText("Preparing the Bilbao raster and land-cover model package...");
        const QString raster = ensureSampleFile(
            QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/bilbao_s2_rgbnir_2021.zip")),
            QStringLiteral("bilbao_s2_rgbnir_2021.zip"),
            QStringLiteral("bilbao_s2_rgbnir_2021"),
            QStringLiteral("bilbao_s2_rgbnir_2021.tif"), &window);
        if (raster.isEmpty()) {
            details->setPlainText("The Bilbao input raster could not be prepared.");
            return;
        }

        const QString manifest = ensureSampleFile(
            QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/landcover-bilbao-model.zip")),
            QStringLiteral("landcover-bilbao-model.zip"),
            QStringLiteral("landcover-bilbao-model"),
            QStringLiteral("geokernel-model.json"), &window);
        if (manifest.isEmpty()) {
            details->setPlainText("The Bilbao land-cover model package could not be prepared.");
            return;
        }

        rasterPath->setText(QDir::toNativeSeparators(raster));
        modelPath->setText(QDir::toNativeSeparators(QFileInfo(manifest).absolutePath()));
        try {
            openBaseRaster(raster);
            details->setPlainText("Bilbao input raster is open. Run land-cover inference to add the prediction layer.");
        } catch (const std::exception& exception) {
            details->setPlainText("The sample files are ready, but the input raster could not be opened:\n" +
                QString::fromUtf8(exception.what()));
        }
    }, Qt::QueuedConnection);
    return app.exec();
}
