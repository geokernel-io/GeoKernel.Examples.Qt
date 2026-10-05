#include "SampleSupport.h"
#include <GeoKernel.Viewer3D/ViewerController.h>
#include <QApplication>
#include <QMainWindow>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVulkanInstance>
#include <atomic>
#include <future>

using namespace GeoKernel3D;

struct LoadResult
{
    std::unique_ptr<LoadedScene> scene;
    QString error;
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("TerrainAnalysis");
    app.setWindowIcon(QIcon(":/icons/geokernel.ico"));

    QVulkanInstance instance;
    instance.setApiVersion(QVersionNumber(1, 1, 0));
    if (!instance.create())
    {
        QMessageBox::critical(nullptr, "TerrainAnalysis", "Vulkan could not be initialized.");
        return 1;
    }

    // Lifetime: controller, container/window, then Vulkan instance.
    QMainWindow window;
    window.setWindowTitle("TerrainAnalysis — GeoKernel");
    window.resize(1200, 800);
    auto* viewport = createViewerWindow(&instance);
    auto* container = QWidget::createWindowContainer(viewport, &window);
    container->setFocusPolicy(Qt::StrongFocus);
    auto* stack = new QStackedWidget(&window);
    auto* welcome = new QLabel("Preparing Sagrada Família terrain…");
    welcome->setAlignment(Qt::AlignCenter);
    stack->addWidget(welcome);
    stack->addWidget(container);
    stack->setCurrentWidget(welcome);
    window.setCentralWidget(stack);
    
    ViewerController viewer(viewport);
    auto* dock = new QDockWidget("Terrain analysis", &window);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto* panel = new QWidget;
    auto* form = new QFormLayout(panel);
    QString demPath;
    auto* dataset = new QLabel("Sagrada Família — sample terrain");
    dataset->setWordWrap(true);
    form->addRow(dataset);
    auto* note = new QLabel(
        "Slope and aspect describe the loaded terrain mesh, not an exported analysis raster. Mesh "
        "resolution affects results. Heights have an unverified vertical reference.");
    note->setWordWrap(true);
    form->addRow(note);
    auto* load = new QPushButton("Reload sample");
    auto* cancel = new QPushButton("Cancel loading");
    cancel->setEnabled(false);
    form->addRow(load);
    form->addRow(cancel);
    auto* progress = new QProgressBar;
    progress->setRange(0, 0);
    progress->hide();
    form->addRow(progress);
    auto* quality = new QComboBox;
    quality->addItems({"256 — fast", "512 — balanced", "1024 — detailed"});
    quality->setCurrentIndex(1);
    form->addRow("Mesh samples per side", quality);
    auto* mode = new QComboBox;
    mode->addItems({"Terrain relief", "Slope (degrees)", "Aspect (downhill direction)"});
    mode->setCurrentIndex(1);
    form->addRow("Display", mode);
    auto* legend = new QLabel;
    legend->setTextFormat(Qt::RichText);
    legend->setWordWrap(true);
    form->addRow(legend);
    auto* legendCaption =
        new QLabel("Analysis uses terrain mesh faces before vertical exaggeration. Aspect is "
                   "clockwise from north in the local ENU frame.");
    legendCaption->setWordWrap(true);
    form->addRow(legendCaption);
    auto* exaggeration = new QDoubleSpinBox;
    exaggeration->setRange(.25, 10);
    exaggeration->setSingleStep(.25);
    exaggeration->setValue(1);
    exaggeration->setSuffix(" ×");
    form->addRow("Vertical exaggeration", exaggeration);
    QObject::connect(exaggeration, qOverload<double>(&QDoubleSpinBox::valueChanged), &window,
                     [&](double value) { viewer.setHeightScale(float(value)); });
    auto applyMode = [&] {
        viewer.setTerrainColorRamp(TerrainColorRamp::Terrain);
        viewer.setTerrainDisplayMode(static_cast<TerrainDisplayMode>(mode->currentIndex()));
        auto swatch = [](const QString& color, const QString& label) {
            return QString("<tr><td bgcolor='%1' width='24'>&nbsp;</td><td>%2</td></tr>")
                .arg(color, label);
        };
        QString rows;
        if (mode->currentIndex() == 1)
        {
            rows = swatch("#2c7bb6", "0 to &lt;5°") + swatch("#abd9e9", "5 to &lt;15°") +
                   swatch("#ffffbf", "15 to &lt;30°") + swatch("#fdae61", "30 to &lt;45°") +
                   swatch("#d7191c", "45 to 90°");
        }
        else if (mode->currentIndex() == 2)
        {
            const QStringList colors = {"#e6194b", "#f58230", "#ffe119", "#3cb44b",
                                        "#46f0f0", "#0082c8", "#911eb4", "#f032e6"};
            const QStringList directions = {"N — 0°",   "NE — 45°",  "E — 90°",  "SE — 135°",
                                            "S — 180°", "SW — 225°", "W — 270°", "NW — 315°"};
            for (int i = 0; i < 8; ++i)
                rows += swatch(colors[i], directions[i]);
            rows += swatch("#808080", "Flat (slope &lt;0.01°)");
            rows += "<tr><td colspan='2'>Each direction spans ±22.5°.</td></tr>";
        }
        else
        {
            rows = swatch("#185b3f", "Low") + swatch("#68984c", "↓") + swatch("#cfbe7e", "↓") +
                   swatch("#926b4c", "↓") + swatch("#fafafa", "High");
            rows += "<tr><td colspan='2'>Relative ENU relief, with lighting.</td></tr>";
        }
        legend->setText("<table cellspacing='5'>" + rows + "</table>");
    };

    QObject::connect(mode, qOverload<int>(&QComboBox::currentIndexChanged), &window,
                     [&] { applyMode(); });
    applyMode();
    auto* info = new QLabel("No terrain loaded.");
    info->setWordWrap(true);
    form->addRow(info);
    dock->setWidget(panel);
    window.addDockWidget(Qt::LeftDockWidgetArea, dock);
    auto* toolbar = window.addToolBar("Navigation");
    toolbar->setMovable(false);

    QObject::connect(toolbar->addAction("Reset view"), &QAction::triggered, &window,
                     [&] { viewer.resetCamera(); });

    QObject::connect(toolbar->addAction("Controls"), &QAction::triggered, &window, [&] {
        QMessageBox::information(&window, "Camera controls",
                                 "Left drag: pan\nWheel or right drag: zoom\nMiddle drag or Ctrl + "
                                 "left drag: orbit\nShift + left drag: look");
    });

    std::atomic_bool cancelled{false};
    std::future<LoadResult> pending;
    auto busy = [&](bool on) {
        load->setEnabled(!on);
        quality->setEnabled(!on);
        cancel->setEnabled(on);
        progress->setVisible(on);
    };
    auto fail = [&](const QString& error) {
        window.statusBar()->showMessage(error);
        QMessageBox::warning(&window, "Unable to load terrain", error);
    };
    QTimer poll;
    poll.setInterval(75);

    QObject::connect(cancel, &QPushButton::clicked, &window, [&] {
        cancelled = true;
        cancel->setEnabled(false);
        window.statusBar()->showMessage("Cancelling…");
    });

    QObject::connect(load, &QPushButton::clicked, &window, [&] {
        if (pending.valid())
            return;
        busy(true);
        window.statusBar()->showMessage("Preparing sample data…");
        demPath = ensureSampleFile(QUrl("https://github.com/geokernel-io/GeoKernel.SampleData/"
                                        "releases/download/v1/sagrada_familia_terrain.zip"),
                                   "sagrada_familia_terrain.zip", "sagrada_familia_terrain",
                                   "sagrada_familia_terrain.tif", &window);
        if (demPath.isEmpty())
        {
            busy(false);
            window.statusBar()->showMessage("Sample unavailable. Click Reload sample to retry.");
            return;
        }

        const QString folder = QFileInfo(demPath).absolutePath();
        const QDir dir(folder);
        SceneOptions options;
        options.demFile = dir.relativeFilePath(QFileInfo(demPath).absoluteFilePath());
        options.imageFile.clear();
        options.terrainResolution = 256 << quality->currentIndex();
        // Explicitly disable vector discovery: this example teaches raster layers only.
        options.roadFile.clear();
        options.buildingFile.clear();
        options.overrideHeightReference = true;
        options.height.datum = HeightDatum::Unknown;
        options.height.unit = "m";
        options.height.unitAssumed = true;
        cancelled = false;
        busy(true);
        window.statusBar()->showMessage("Loading terrain…");
        // Only CPU preparation happens on this worker. GUI/GPU commit stays on the GUI thread.
        pending = std::async(std::launch::async, [&, folder, options] {
            LoadResult r;
            try
            {
                LoadControl c;
                c.cancellationFlag = &cancelled;
                c.cancelled = [&] { return cancelled.load(); };
                r.scene = loadPreparedScene(folder, options, r.error, c);
            }
            catch (const std::exception& e)
            {
                r.error = QString::fromUtf8(e.what());
            }
            return r;
        });
        poll.start();
    });

    QObject::connect(&poll, &QTimer::timeout, &window, [&] {
        if (!pending.valid() ||
            pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        poll.stop();
        auto r = pending.get();
        busy(false);
        if (cancelled)
        {
            window.statusBar()->showMessage("Loading cancelled. Previous scene retained.");
            return;
        }
        if (!r.scene)
        {
            fail(r.error);
            return;
        }
        QString error;
        if (!viewer.setScene(std::move(r.scene), error))
        {
            fail(error);
            return;
        }
        viewer.resetCamera();
        stack->setCurrentWidget(container);
        applyMode();
        viewer.setHeightScale(float(exaggeration->value()));
        info->setText(QString("DEM: %1\nHeights: %2\nMesh: up to %3 samples per side")
                          .arg(QFileInfo(demPath).fileName(), "Unknown — unverified preview")
                          .arg(256 << quality->currentIndex()));
        window.statusBar()->showMessage(
            "Terrain loaded | Left: pan | Wheel/right: zoom | Middle/Ctrl+left: orbit");
    });
    window.show();
    QTimer::singleShot(0, load, &QPushButton::click);
    const int result = app.exec();
    poll.stop();
    cancelled = true;

    // Native raster reads may finish before cancellation is observed; join before SDK teardown.
    if (pending.valid())
        pending.get();
    return result;
}
