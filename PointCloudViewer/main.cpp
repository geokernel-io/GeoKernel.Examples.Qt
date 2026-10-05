#include "SampleSupport.h"
#include <GeoKernel.Viewer3D/LasPointCloud.h>
#include <GeoKernel.Viewer3D/ViewerController.h>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVulkanInstance>
#include <QWindow>
#include <future>

using namespace GeoKernel3D;

struct Result
{
    std::shared_ptr<LasPointCloud> cloud;
    QString error;
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("PointCloudViewer");
    app.setWindowIcon(QIcon(":/icons/geokernel.ico"));

    QVulkanInstance instance;
    instance.setApiVersion(QVersionNumber(1, 1, 0));
    if (!instance.create())
    {
        QMessageBox::critical(nullptr, "PointCloudViewer", "Vulkan could not be initialized.");
        return 1;
    }

    QMainWindow window;
    window.setWindowTitle("PointCloudViewer — GeoKernel");
    window.resize(1200, 800);
    auto* viewport = createModelPreviewWindow(&instance);
    auto* container = QWidget::createWindowContainer(viewport, &window);
    container->setFocusPolicy(Qt::StrongFocus);
    auto* stack = new QStackedWidget;
    auto* loading = new QLabel("Preparing point cloud…");
    loading->setAlignment(Qt::AlignCenter);
    stack->addWidget(loading);
    stack->addWidget(container);
    window.setCentralWidget(stack);
    ViewerController viewer(viewport);
    auto* dock = new QDockWidget("Point cloud", &window);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto* panel = new QWidget;
    auto* form = new QFormLayout(panel);
    dock->setWidget(panel);
    window.addDockWidget(Qt::LeftDockWidgetArea, dock);
    auto* title = new QLabel("EuroSDR P4 — 40 × 40 m crop");
    title->setWordWrap(true);
    form->addRow(title);
    auto* colors = new QComboBox;
    colors->addItem("RGB", int(PointStyle::Rgb));
    colors->addItem("Height", int(PointStyle::Height));
    colors->setEnabled(false);
    form->addRow("Colour", colors);
    auto* samples = new QComboBox;
    for (int n : {50000, 100000, 250000})
        samples->addItem(QString::number(n), n);
    samples->setCurrentIndex(2);
    form->addRow("Sample limit", samples);
    auto* reload = new QPushButton("Load / apply sample limit");
    auto* cancel = new QPushButton("Cancel loading");
    cancel->setEnabled(false);
    form->addRow(reload);
    form->addRow(cancel);
    auto* info = new QLabel("No points loaded.");
    info->setWordWrap(true);
    info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(info);
    auto* note = new QLabel("Local normalized preview; coordinates in the file remain EPSG:32630. "
                            "Heights are provisional. Display is sampled (up to 250,000 points); "
                            "source points are preserved. Point size: 1 pixel (SDK fixed).");
    note->setWordWrap(true);
    form->addRow(note);
    auto* toolbar = window.addToolBar("Navigation");
    toolbar->setMovable(false);

    QObject::connect(toolbar->addAction("Reset view"), &QAction::triggered, &window,
                     [&] { viewer.resetCamera(); });

    QObject::connect(toolbar->addAction("Controls"), &QAction::triggered, &window, [&] {
        QMessageBox::information(&window, "Controls",
                                 "Left drag: pan\nWheel/right drag: zoom\nMiddle/Ctrl+left drag: "
                                 "orbit\nShift+left drag: look");
    });

    std::atomic_bool cancelled{false};
    std::future<Result> pending;
    std::shared_ptr<LasPointCloud> cloud;
    auto busy = [&](bool on) {
        reload->setEnabled(!on);
        samples->setEnabled(!on);
        colors->setEnabled(!on && bool(cloud));
        cancel->setEnabled(on);
    };

    auto fail = [&](QString e) {
        loading->setText("Point cloud unavailable. Use Load to retry.");
        window.statusBar()->showMessage(e);
        QMessageBox::warning(&window, "PointCloudViewer", e);
    };

    QObject::connect(colors, &QComboBox::currentIndexChanged, &window, [&] {
        if (!cloud)
            return;
        try
        {
            viewer.setPointPreview(*cloud, PointStyle(colors->currentData().toInt()));
        }
        catch (const std::exception& e)
        {
            fail(QString::fromUtf8(e.what()));
        }
    });
    QTimer poll;
    poll.setInterval(100);

    QObject::connect(cancel, &QPushButton::clicked, &window, [&] {
        cancelled = true;
        cancel->setEnabled(false);
    });

    QObject::connect(reload, &QPushButton::clicked, &window, [&] {
        if (pending.valid())
            return;
        busy(true);
        cancel->setEnabled(false);
        auto file = ensureSampleFile(QUrl("https://github.com/geokernel-io/GeoKernel.SampleData/"
                                          "releases/download/v1/eurosdr_p4_pointcloud.zip"),
                                     "eurosdr_p4_pointcloud.zip", "eurosdr_p4_pointcloud",
                                     "eurosdr_p4_pointcloud.laz", &window);
        if (file.isEmpty())
        {
            busy(false);
            loading->setText("Download unavailable. Use Load to retry.");
            return;
        }
        cancelled = false;
        cancel->setEnabled(true);
        window.statusBar()->showMessage("Reading and sampling LAZ…");
        const auto limit = samples->currentData().toUInt();
        pending = std::async(std::launch::async, [&, file, limit] {
            Result r;
            try
            {
                r.cloud = std::make_shared<LasPointCloud>(readLasPoints(file, cancelled, limit));
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
        auto r = pending.get();
        poll.stop();
        busy(false);
        if (cancelled)
        {
            loading->setText("Loading cancelled. Use Load to retry.");
            window.statusBar()->showMessage("Cancelled; previous cloud retained.");
            return;
        }
        if (!r.cloud)
        {
            fail(r.error);
            return;
        }
        try
        {
            viewer.setPointPreview(*r.cloud, PointStyle(colors->currentData().toInt()));
            cloud = std::move(r.cloud);
            viewer.resetCamera();
            stack->setCurrentWidget(container);
            colors->setEnabled(true);
            info->setText(QString("Source: %1 points\nDisplay sample: %2 points\nSource Z: %3 – %4 "
                                  "m\nRGB available: %5")
                              .arg(qulonglong(cloud->total))
                              .arg(qulonglong(cloud->points.size()))
                              .arg(cloud->low[2], 0, 'f', 2)
                              .arg(cloud->high[2], 0, 'f', 2)
                              .arg(cloud->hasRgb ? "Yes" : "No"));
            window.statusBar()->showMessage("Point cloud loaded");
        }
        catch (const std::exception& e)
        {
            fail(QString::fromUtf8(e.what()));
        }
    });
    QTimer errors;
    errors.setInterval(1000);
    QObject::connect(&errors, &QTimer::timeout, &window, [&] {
        const auto d = viewer.renderDiagnostics();
        const auto m = viewer.modelState();
        QStringList messages{d.renderError, m.error};
        messages.removeAll(QString());
        if (!messages.empty())
            window.statusBar()->showMessage(messages.join(" | "));
    });
    errors.start();
    window.show();
    QTimer::singleShot(0, reload, &QPushButton::click);
    const int result = app.exec();
    poll.stop();
    errors.stop();
    cancelled = true;

    if (pending.valid())
        pending.get();

    return result;
}
