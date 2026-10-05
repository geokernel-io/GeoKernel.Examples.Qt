#include "SampleSupport.h"
#include "StylingPanel.h"
#include <GeoKernel.Viewer3D/Project.h>
#include <GeoKernel.Viewer3D/ViewerController.h>
#include <QApplication>
#include <QFileDialog>
#include <QMainWindow>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QScrollArea>
#include <QCheckBox>
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
    app.setApplicationName("ProjectSaveLoad");
    app.setWindowIcon(QIcon(":/icons/geokernel.ico"));

    QVulkanInstance instance;
    instance.setApiVersion(QVersionNumber(1, 1, 0));
    if (!instance.create())
    {
        QMessageBox::critical(nullptr, "ProjectSaveLoad", "Vulkan could not be initialized.");
        return 1;
    }

    // Lifetime: controller, container/window, then Vulkan instance.
    QMainWindow window;
    window.setWindowTitle("ProjectSaveLoad — GeoKernel");
    window.resize(1400, 850);
    auto* viewport = createViewerWindow(&instance);
    auto* container = QWidget::createWindowContainer(viewport, &window);
    container->setFocusPolicy(Qt::StrongFocus);
    auto* stack = new QStackedWidget(&window);
    auto* welcome = new QLabel("Preparing Sagrada Família terrain and orthophoto…");
    welcome->setAlignment(Qt::AlignCenter);
    stack->addWidget(welcome);
    stack->addWidget(container);
    stack->setCurrentWidget(welcome);
    window.setCentralWidget(stack);
    ViewerController viewer(viewport);
    auto* stylingDock = new QDockWidget("Layer styling and filtering", &window);
    auto* styling = new StylingPanel(viewer, stylingDock);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(styling);
    stylingDock->setWidget(scroll);
    stylingDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    window.addDockWidget(Qt::RightDockWidgetArea, stylingDock);
    auto* dock = new QDockWidget("Terrain data", &window);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto* panel = new QWidget;
    auto* form = new QFormLayout(panel);
    QString demPath, photoPath, buildingPath, roadPath;
    auto* dataset = new QLabel("Sagrada Família — terrain and orthophoto");
    dataset->setWordWrap(true);
    form->addRow(dataset);
    auto* note = new QLabel("DEM datum is unverified. Building footprints use illustrative 9 m "
                            "heights, not measured heights.");
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
    auto* imagery = new QCheckBox("Show orthophoto");
    imagery->setChecked(true);
    imagery->setEnabled(false);
    form->addRow(imagery);
    auto* exaggeration = new QDoubleSpinBox;
    exaggeration->setRange(0.25, 10.0);
    exaggeration->setSingleStep(0.25);
    exaggeration->setDecimals(2);
    exaggeration->setValue(1.0);
    exaggeration->setSuffix(" ×");
    exaggeration->setToolTip(
        "Scales displayed terrain height only. Source elevations are unchanged.");
    form->addRow("Vertical exaggeration", exaggeration);
    QObject::connect(exaggeration, qOverload<double>(&QDoubleSpinBox::valueChanged), &window,
                     [&](double value) { viewer.setHeightScale(static_cast<float>(value)); });
    auto* info = new QLabel("No terrain loaded.");
    info->setWordWrap(true);
    form->addRow(info);
    dock->setWidget(panel);
    window.addDockWidget(Qt::LeftDockWidgetArea, dock);
    SceneOptions activeOptions, pendingOptions;
    QString projectPath, pendingPath;
    std::optional<Project> pendingProject;
    auto* projectInfo = new QLabel("Unsaved sample scene");
    projectInfo->setWordWrap(true);
    form->addRow(projectInfo);
    auto* projectNote = new QLabel(
        "Projects reference local data files; they do not embed them. Keep the cached data when "
        "moving or sharing a project. Open/reload replaces the current scene; save changes first.");
    projectNote->setWordWrap(true);
    form->addRow(projectNote);
    auto* projectBar = window.addToolBar("Project");
    projectBar->setMovable(false);
    auto* openProject = projectBar->addAction("Open project…");
    auto* save = projectBar->addAction("Save");
    save->setShortcut(QKeySequence::Save);
    save->setEnabled(false);
    auto* saveAs = projectBar->addAction("Save as…");
    saveAs->setEnabled(false);
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
        openProject->setEnabled(!on);
        save->setEnabled(!on && bool(viewer.scene()));
        saveAs->setEnabled(!on && bool(viewer.scene()));
        styling->setEnabled(!on && bool(viewer.scene()));
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
    auto startLoad = [&](const QString& folder, const SceneOptions& options) {
        pendingOptions = options;
        pendingPhoto = !options.imageFile.isEmpty();
        cancelled = false;
        busy(true);
        window.statusBar()->showMessage("Loading scene…");
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
    };
    auto saveProject = [&](bool choosePath) {
        if (!viewer.scene() || pending.valid())
            return;
        styling->flushFilter();
        QString path = projectPath;
        if (choosePath || path.isEmpty())
            path = QFileDialog::getSaveFileName(
                &window, "Save project",
                path.isEmpty() ? QDir::homePath() + "/SagradaFamilia.gk3d" : path,
                "GeoKernel 3D project (*.gk3d)");
        if (path.isEmpty())
            return;
        if (QFileInfo(path).suffix().isEmpty())
            path += ".gk3d";
        Project p;
        p.options = activeOptions;
        p.camera = viewer.camera();
        p.exaggeration = viewer.heightScale();
        const DisplayLayer layers[] = {DisplayLayer::Imagery, DisplayLayer::Roads,
                                       DisplayLayer::Buildings};
        bool* visibility[] = {&p.imagery, &p.roads, &p.buildings};
        for (int i = 0; i < 3; ++i)
        {
            auto a = viewer.layerAppearance(layers[i]);
            *visibility[i] = a.visible;
            p.opacity[i] = a.opacity;
            if (i > 0)
                p.colors[i - 1] = a.color;
        }
        for (const auto& layer : viewer.scene()->layers)
        {
            int i = layer.type == LayerType::Roads       ? 0
                    : layer.type == LayerType::Buildings ? 1
                                                         : -1;
            if (i >= 0)
            {
                p.filters[i] = viewer.filter(layer.id);
                p.themeFields[i] = viewer.theme(layer.id);
            }
        }
        QString error;
        if (!writeProject(path, p, error))
        {
            QMessageBox::warning(&window, "Save failed", error);
            return;
        }
        projectPath = QFileInfo(path).absoluteFilePath();
        projectInfo->setText(projectPath);
        window.statusBar()->showMessage("Project saved. Local source data remains separate.");
    };

    QObject::connect(save, &QAction::triggered, &window, [&] { saveProject(false); });

    QObject::connect(saveAs, &QAction::triggered, &window, [&] { saveProject(true); });

    QObject::connect(openProject, &QAction::triggered, &window, [&] {
        if (pending.valid())
            return;
        const auto path = QFileDialog::getOpenFileName(
            &window, "Open project", projectPath.isEmpty() ? QDir::homePath() : projectPath,
            "GeoKernel 3D project (*.gk3d *.json)");
        if (path.isEmpty())
            return;
        Project p;
        QString error;
        if (!readProject(path, p, error))
        {
            fail(error);
            return;
        }
        if (p.model || p.pointCloud || !p.tileset.isEmpty() || p.globeXyz || p.globeTerrain ||
            p.globeWms || p.globeWmts || p.timeSeries || p.timeFilter.enabled ||
            !p.analysisResults.isEmpty() || !p.booleanResult.isEmpty() ||
            p.options.height.datum == HeightDatum::Orthometric)
        {
            fail("This example opens local DEM/imagery/roads/buildings projects without geoid "
                 "conversion or advanced layers.");
            return;
        }
        if (p.options.demFile.isEmpty())
        {
            fail("This example requires a DEM project.");
            return;
        }
        for (const auto& file :
             {p.options.demFile, p.options.imageFile, p.options.roadFile, p.options.buildingFile})
            if (!file.isEmpty() && !QFileInfo::exists(file))
            {
                fail("Project source is missing: " + file);
                return;
            }
        pendingProject = p;
        pendingPath = QFileInfo(path).absoluteFilePath();
        startLoad(QFileInfo(path).absolutePath(), p.options);
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
        // Load the downloaded vector layers explicitly; no source Z or measured heights.
        options.roadFile = dir.relativeFilePath(roadPath);
        options.buildingFile = dir.relativeFilePath(buildingPath);
        options.roadPlacement.mode = HeightMode::ClampToTerrain;
        options.roadPlacement.useSourceZ = false;
        options.buildingPlacement.mode = HeightMode::ClampToTerrain;
        options.buildingPlacement.useSourceZ = false;
        options.buildingHeight = 9;
        options.overrideHeightReference = true;
        options.height.datum = HeightDatum::Unknown;
        options.height.unit = "m";
        options.height.unitAssumed = true;
        options.demFile = demPath;
        options.imageFile = photoPath;
        options.roadFile = roadPath;
        options.buildingFile = buildingPath;
        pendingProject.reset();
        pendingPath.clear();
        startLoad(folder, options);
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
        activeOptions = pendingOptions;
        activeOptions.sceneId = viewer.scene()->id;
        activeOptions.layerIds.clear();
        for (const auto role :
             {LayerType::Terrain, LayerType::Imagery, LayerType::Roads, LayerType::Buildings})
        {
            QString id;
            for (const auto& l : viewer.scene()->layers)
                if (l.type == role)
                {
                    id = l.id;
                    break;
                }
            activeOptions.layerIds.append(
                id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id);
        }
        projectPath = pendingPath;
        if (pendingProject)
        {
            const auto& p = *pendingProject;
            const DisplayLayer layers[] = {DisplayLayer::Imagery, DisplayLayer::Roads,
                                           DisplayLayer::Buildings};
            const bool visibility[] = {p.imagery, p.roads, p.buildings};
            for (int i = 0; i < 3; ++i)
            {
                if (i > 0)
                    viewer.setLayerColor(layers[i], p.colors[i - 1]);
                auto a = viewer.layerAppearance(layers[i]);
                a.visible = visibility[i];
                a.opacity = p.opacity[i];
                viewer.setLayerAppearance(layers[i], a);
            }
            for (const auto& l : viewer.scene()->layers)
            {
                int i = l.type == LayerType::Roads ? 0 : l.type == LayerType::Buildings ? 1 : -1;
                if (i >= 0)
                {
                    viewer.setFilter(l.id, p.filters[i]);
                    viewer.setTheme(l.id, p.themeFields[i]);
                }
            }
        }
        styling->resetScene();
        viewer.setHeightScale(static_cast<float>(exaggeration->value()));
        viewer.resetCamera();
        stack->setCurrentWidget(container);
        imagery->setEnabled(pendingPhoto);
        imagery->setChecked(pendingPhoto);
        auto appearance = viewer.layerAppearance(DisplayLayer::Imagery);
        appearance.visible = pendingPhoto;
        viewer.setLayerAppearance(DisplayLayer::Imagery, appearance);
        if (pendingProject)
        {
            QSignalBlocker a(exaggeration), b(imagery);
            exaggeration->setValue(pendingProject->exaggeration);
            imagery->setChecked(pendingProject->imagery);
            viewer.setHeightScale(pendingProject->exaggeration);
            auto appearance = viewer.layerAppearance(DisplayLayer::Imagery);
            appearance.visible = pendingProject->imagery;
            viewer.setLayerAppearance(DisplayLayer::Imagery, appearance);
            viewer.setCamera(pendingProject->camera);
        }
        demPath = activeOptions.demFile;
        photoPath = activeOptions.imageFile;
        projectInfo->setText(projectPath.isEmpty() ? "Unsaved sample scene" : projectPath);
        save->setEnabled(true);
        saveAs->setEnabled(true);
        pendingProject.reset();
        info->setText(QString("DEM: %1\nOrthophoto: %2\nHeights: %3")
                          .arg(QFileInfo(demPath).fileName(),
                               pendingPhoto ? QFileInfo(photoPath).fileName() : "None",
                               "Unknown — unverified preview"));
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
