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
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QHeaderView>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QVulkanInstance>
#include <atomic>
#include <cmath>
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
    app.setApplicationName("FeaturePicking");
    app.setWindowIcon(QIcon(":/icons/geokernel.ico"));

    QVulkanInstance instance;
    instance.setApiVersion(QVersionNumber(1, 1, 0));
    if (!instance.create())
    {
        QMessageBox::critical(nullptr, "FeaturePicking", "Vulkan could not be initialized.");
        return 1;
    }

    // Lifetime: controller, container/window, then Vulkan instance.
    QMainWindow window;
    window.setWindowTitle("FeaturePicking — GeoKernel");
    window.resize(1200, 800);
    auto* viewport = createViewerWindow(&instance);
    auto* container = QWidget::createWindowContainer(viewport, &window);
    container->setFocusPolicy(Qt::StrongFocus);
    auto* stack = new QStackedWidget(&window);
    auto* welcome = new QLabel("Preparing Sagrada Família buildings, terrain and orthophoto…");
    welcome->setAlignment(Qt::AlignCenter);
    stack->addWidget(welcome);
    stack->addWidget(container);
    stack->setCurrentWidget(welcome);
    window.setCentralWidget(stack);

    ViewerController viewer(viewport);
    auto* dock = new QDockWidget("Feature picking", &window);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto* panel = new QWidget;
    auto* form = new QFormLayout(panel);
    QString demPath, photoPath, buildingPath, roadPath;
    double pendingHeight = 9;
    int heightFeatureCount = 0;
    auto* dataset = new QLabel("Click a building or road to inspect it.");
    dataset->setWordWrap(true);
    form->addRow(dataset);
    auto* note = new QLabel(
        "Sample DEM heights are an unverified preview. No vertical datum conversion is applied.");
    note->setWordWrap(true);
    form->addRow(note);
    auto* heightNote =
        new QLabel("Buildings use an illustrative 9 m height, not measured heights.");
    heightNote->setWordWrap(true);
    form->addRow(heightNote);
    auto* load = new QPushButton("Reload sample");
    auto* cancel = new QPushButton("Cancel loading");
    cancel->setEnabled(false);
    form->addRow(load);
    form->addRow(cancel);
    auto* progress = new QProgressBar;
    progress->setRange(0, 0);
    progress->hide();
    form->addRow(progress);
    auto* imagery = new QCheckBox("Show orthophoto");
    imagery->setChecked(true);
    imagery->setEnabled(false);
    form->addRow(imagery);
    auto* buildingsVisible = new QCheckBox("Show buildings");
    buildingsVisible->setChecked(true);
    form->addRow(buildingsVisible);
    auto* roadsVisible = new QCheckBox("Show roads");
    roadsVisible->setChecked(true);
    form->addRow(roadsVisible);
    auto applyLayerVisibility = [&] {
        auto b = viewer.layerAppearance(DisplayLayer::Buildings);
        b.visible = buildingsVisible->isChecked();
        viewer.setLayerAppearance(DisplayLayer::Buildings, b);
        auto r = viewer.layerAppearance(DisplayLayer::Roads);
        r.visible = roadsVisible->isChecked();
        viewer.setLayerAppearance(DisplayLayer::Roads, r);
    };

    QObject::connect(buildingsVisible, &QCheckBox::toggled, &window,
                     [&] { applyLayerVisibility(); });

    QObject::connect(roadsVisible, &QCheckBox::toggled, &window, [&] { applyLayerVisibility(); });
    auto* multiple = new QCheckBox("Multiple selection (click to add/remove)");
    form->addRow(multiple);
    QObject::connect(multiple, &QCheckBox::toggled, &window,
                     [&](bool on) { viewer.setMultipleSelectionEnabled(on); });
    auto* selectionCount = new QLabel("No selection");
    form->addRow(selectionCount);
    auto* selected = new QComboBox;
    selected->setEnabled(false);
    form->addRow("Selected feature", selected);
    auto* attributes = new QTableWidget(0, 2);
    attributes->setHorizontalHeaderLabels({"Property", "Value"});
    attributes->setEditTriggers(QAbstractItemView::NoEditTriggers);
    attributes->setSelectionBehavior(QAbstractItemView::SelectRows);
    attributes->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    attributes->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    attributes->verticalHeader()->hide();
    attributes->setMinimumHeight(220);
    form->addRow(attributes);
    auto* zoomSelection = new QPushButton("Zoom to selection");
    zoomSelection->setEnabled(false);
    form->addRow(zoomSelection);
    auto* clear = new QPushButton("Clear selection (Esc)");
    clear->setEnabled(false);
    form->addRow(clear);
    auto refreshSelection = [&] {
        const auto snapshot = viewer.vectorSelection();
        QSignalBlocker blocker(selected);
        selected->clear();
        attributes->setRowCount(0);
        const bool has = snapshot.scene && !snapshot.entries.empty();
        selected->setEnabled(has);
        zoomSelection->setEnabled(has);
        clear->setEnabled(has);
        selectionCount->setText(has ? QString("%1 selected").arg(snapshot.entries.size())
                                    : "No selection");
        if (!has)
            return;
        const VectorSelectionEntry* active = &snapshot.entries.front();
        for (const auto& entry : snapshot.entries)
        {
            const auto* layer = snapshot.scene->layer(entry.range.layerId);
            const auto* feature =
                snapshot.scene->feature(entry.range.layerId, entry.range.featureId);
            if (!layer || !feature)
                continue;
            QString name = feature->attributes.value("name").toString();
            if (name.isEmpty())
                name = feature->sourceId.isEmpty() ? feature->id : feature->sourceId;
            selected->addItem(layer->name + " / " + name,
                              QVariant::fromValue<qulonglong>(entry.index));
            if (snapshot.activeIndex && entry.index == *snapshot.activeIndex)
            {
                active = &entry;
                selected->setCurrentIndex(selected->count() - 1);
            }
        }
        const auto* layer = snapshot.scene->layer(active->range.layerId);
        const auto* feature =
            snapshot.scene->feature(active->range.layerId, active->range.featureId);
        if (!layer || !feature)
            return;
        auto row = [&](QString key, QString value) {
            int n = attributes->rowCount();
            attributes->insertRow(n);
            attributes->setItem(n, 0, new QTableWidgetItem(key));
            attributes->setItem(n, 1, new QTableWidgetItem(value));
        };
        row("Layer", layer->name);
        row("Type", layer->type == LayerType::Buildings ? "Building" : "Road");
        row("Scene feature ID", feature->id);
        if (feature->sourceFid)
            row("Source FID", QString::number(*feature->sourceFid));
        if (feature->buildingHeight)
            row("Display height (estimated)",
                QString::number(feature->buildingHeight->metres) + " m");
        auto keys = feature->attributes.keys();
        keys.sort(Qt::CaseInsensitive);
        for (const auto& key : keys)
        {
            const auto value = feature->attributes.value(key);
            row(key, value.isNull() ? "(null)" : value.toString());
        }
    };

    // SDK callbacks may not reenter the viewer. Refresh after notification returns.
    viewer.addSelectionChangedHandler([&] { QTimer::singleShot(0, &window, refreshSelection); });
    QObject::connect(selected, qOverload<int>(&QComboBox::activated), &window, [&](int index) {
        if (index >= 0)
            viewer.activateSelection(size_t(selected->itemData(index).toULongLong()));
    });
    QObject::connect(zoomSelection, &QPushButton::clicked, &window,
                     [&] { viewer.focusSelection(); });
    QObject::connect(clear, &QPushButton::clicked, &window, [&] { viewer.clearSelection(); });
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), &window);
    QObject::connect(escape, &QShortcut::activated, &window, [&] { viewer.clearSelection(); });
    auto* exaggeration = new QDoubleSpinBox;
    exaggeration->setRange(0.25, 10.0);
    exaggeration->setSingleStep(0.25);
    exaggeration->setDecimals(2);
    exaggeration->setValue(1.0);
    exaggeration->setSuffix(" ×");
    exaggeration->setToolTip("Scales displayed scene height. Source elevations are unchanged.");
    form->addRow("Vertical exaggeration", exaggeration);
    QObject::connect(exaggeration, qOverload<double>(&QDoubleSpinBox::valueChanged), &window,
                     [&](double value) { viewer.setHeightScale(static_cast<float>(value)); });
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

    QObject::connect(imagery, &QCheckBox::toggled, &window, [&](bool visible) {
        auto a = viewer.layerAppearance(DisplayLayer::Imagery);
        a.visible = visible;
        viewer.setLayerAppearance(DisplayLayer::Imagery, a);
    });

    std::atomic_bool cancelled{false};
    std::future<LoadResult> pending;
    bool pendingPhoto = false;
    auto busy = [&](bool on) {
        load->setEnabled(!on);
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
        cancel->setEnabled(false);
        window.statusBar()->showMessage("Preparing sample data…");
        auto unavailable = [&] {
            busy(false);
            window.statusBar()->showMessage("Sample unavailable. Click Reload sample to retry.");
        };
        demPath = ensureSampleFile(QUrl("https://github.com/geokernel-io/GeoKernel.SampleData/"
                                        "releases/download/v1/sagrada_familia_terrain.zip"),
                                   "sagrada_familia_terrain.zip", "sagrada_familia_terrain",
                                   "sagrada_familia_terrain.tif", &window);
        if (demPath.isEmpty())
        {
            unavailable();
            return;
        }
        photoPath = ensureSampleFile(QUrl("https://github.com/geokernel-io/GeoKernel.SampleData/"
                                          "releases/download/v1/sagrada_familia_ortophoto.zip"),
                                     "sagrada_familia_ortophoto.zip", "sagrada_familia_ortophoto",
                                     "sagrada_familia_ortophoto.tif", &window);
        if (photoPath.isEmpty())
        {
            unavailable();
            return;
        }
        buildingPath = ensureSampleFile(QUrl("https://github.com/geokernel-io/GeoKernel.SampleData/"
                                             "releases/download/v1/sagrada_familia_buildings.zip"),
                                        "sagrada_familia_buildings.zip",
                                        "sagrada_familia_buildings", "buildings.shp", &window);
        if (buildingPath.isEmpty())
        {
            unavailable();
            return;
        }
        roadPath = ensureSampleFile(QUrl("https://github.com/geokernel-io/GeoKernel.SampleData/"
                                         "releases/download/v1/sagrada_familia_roads.zip"),
                                    "sagrada_familia_roads.zip", "sagrada_familia_roads",
                                    "roads.shp", &window);
        if (roadPath.isEmpty())
        {
            unavailable();
            return;
        }
        const QString folder = QFileInfo(demPath).absolutePath();
        const QDir dir(folder);
        SceneOptions options;
        options.demFile = dir.relativeFilePath(QFileInfo(demPath).absoluteFilePath());
        options.imageFile = photoPath.isEmpty()
                                ? QString()
                                : dir.relativeFilePath(QFileInfo(photoPath).absoluteFilePath());
        // Extrude footprints from the terrain. This sample has no source height attributes.
        options.roadFile = dir.relativeFilePath(roadPath);
        options.roadPlacement.mode = HeightMode::ClampToTerrain;
        options.roadPlacement.useSourceZ = false;
        options.buildingFile = dir.relativeFilePath(buildingPath);
        options.buildingPlacement.mode = HeightMode::ClampToTerrain;
        options.buildingPlacement.useSourceZ = false;
        options.buildingHeight = pendingHeight;
        options.overrideHeightReference = true;
        options.height.datum = HeightDatum::Unknown;
        options.height.unit = "m";
        options.height.unitAssumed = true;
        pendingPhoto = !options.imageFile.isEmpty();
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
        heightFeatureCount = 0;
        if (auto scene = r.scene->scene())
            for (const auto& layer : scene->layers)
                if (layer.type == LayerType::Buildings)
                    for (const auto& feature : layer.features)
                        if (feature.buildingHeight)
                        {
                            ++heightFeatureCount;
                        }
        QString error;
        if (!viewer.setScene(std::move(r.scene), error))
        {
            fail(error);
            return;
        }
        applyLayerVisibility();
        refreshSelection();
        viewer.setHeightScale(static_cast<float>(exaggeration->value()));
        viewer.resetCamera();
        stack->setCurrentWidget(container);
        imagery->setEnabled(pendingPhoto);
        imagery->setChecked(pendingPhoto);
        auto appearance = viewer.layerAppearance(DisplayLayer::Imagery);
        appearance.visible = pendingPhoto;
        viewer.setLayerAppearance(DisplayLayer::Imagery, appearance);
        info->setText(
            QString(
                "Building features: %1\nIllustrative height: %2 m\nDEM datum: unknown — preview")
                .arg(heightFeatureCount)
                .arg(pendingHeight));
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
