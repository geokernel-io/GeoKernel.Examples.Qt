#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QSize>
#include <QString>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include "Layers/GisLayer.h"
#include "Shapes/GisExtent.h"
#include "Viewer/GisViewer.h"
#include "Helpers.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;

QAction* addToolbarIcon(QToolBar& toolbar, const QString& iconName, const QString& caption)
{
    QAction* action = toolbar.addAction(sampleIcon(iconName), QString());
    action->setToolTip(caption);
    action->setStatusTip(caption);
    return action;
}

bool loadWorldLayer(GisViewer& viewer, const QString& title)
{
    QString errorMessage;
    if (!viewer.addLayerFromPath(sampleDataPath(QStringLiteral("shapefile/world_4326.shp")), &errorMessage))
    {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("MultiWindowSync"),
            QStringLiteral("%1 layer could not be loaded:\n%2")
                .arg(title, errorMessage.isEmpty() ? QStringLiteral("shapefile/world_4326.shp") : errorMessage));
        return false;
    }

    if (GisLayer* layer = viewer.mapLayerAt(0))
    {
        layer->setName(title);
        layer->style().setFillColor(QStringLiteral("#D8E5E1"));
        layer->style().setFillOpacity(220);
        layer->style().setLineColor(QStringLiteral("#6F8883"));
        layer->style().setLineWidth(0.8f);
    }

    viewer.refreshLayers();
    return true;
}

QWidget* createViewerPane(QWidget* parent, const QString& title, GisViewer*& viewer)
{
    auto* pane = new QWidget(parent);
    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* label = new QLabel(title, pane);
    label->setFixedHeight(28);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral("background:#eef2f1; border-bottom:1px solid #c7d1ce;"));

    viewer = new GisViewer(pane);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(label);
    layout->addWidget(viewer, 1);
    return pane;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon(QStringLiteral("GeoKernelAppIcon.ico")));

    QMainWindow window;
    window.resize(1280, 760);
    window.setWindowTitle(QStringLiteral("MultiWindowSync"));

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    window.addToolBar(toolbar);

    QAction* zoomInAction = addToolbarIcon(*toolbar, QStringLiteral("ZoomIn.svg"), QStringLiteral("Zoom In"));
    QAction* zoomOutAction = addToolbarIcon(*toolbar, QStringLiteral("ZoomOut.svg"), QStringLiteral("Zoom Out"));
    QAction* fullExtentAction = addToolbarIcon(*toolbar, QStringLiteral("FullExtent.svg"), QStringLiteral("Full Extent"));
    QAction* syncAction = addToolbarIcon(*toolbar, QStringLiteral("Refresh.svg"), QStringLiteral("Sync On"));
    syncAction->setCheckable(true);
    syncAction->setChecked(true);
    toolbar->addSeparator();

    QActionGroup toolGroup(&window);
    toolGroup.setExclusive(true);

    QAction* zoomBoxAction = addToolbarIcon(*toolbar, QStringLiteral("RectangularZoom.svg"), QStringLiteral("Zoom Rect"));
    zoomBoxAction->setCheckable(true);
    toolGroup.addAction(zoomBoxAction);

    QAction* panAction = addToolbarIcon(*toolbar, QStringLiteral("Pan.svg"), QStringLiteral("Pan"));
    panAction->setCheckable(true);
    panAction->setChecked(true);
    toolGroup.addAction(panAction);

    auto* centralWidget = new QWidget(&window);
    auto* layout = new QHBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(1);

    GisViewer* leftViewer = nullptr;
    GisViewer* rightViewer = nullptr;
    layout->addWidget(createViewerPane(centralWidget, QStringLiteral("Viewer A"), leftViewer), 1);
    layout->addWidget(createViewerPane(centralWidget, QStringLiteral("Viewer B"), rightViewer), 1);
    window.setCentralWidget(centralWidget);

    if (!loadWorldLayer(*leftViewer, QStringLiteral("World A")))
        return 1;

    if (!loadWorldLayer(*rightViewer, QStringLiteral("World B")))
        return 1;

    bool syncing = false;
    QObject::connect(leftViewer, &GisViewer::visibleExtentChanged, rightViewer, [rightViewer, syncAction, &syncing](const GisExtent& extent)
    {
        if (!syncAction->isChecked() || syncing)
            return;

        syncing = true;
        rightViewer->setViewExtent(extent);
        syncing = false;
    });

    QObject::connect(rightViewer, &GisViewer::visibleExtentChanged, leftViewer, [leftViewer, syncAction, &syncing](const GisExtent& extent)
    {
        if (!syncAction->isChecked() || syncing)
            return;

        syncing = true;
        leftViewer->setViewExtent(extent);
        syncing = false;
    });

    QObject::connect(syncAction, &QAction::toggled, syncAction, [syncAction, leftViewer, rightViewer, &syncing](bool checked)
    {
        const QString caption = checked ? QStringLiteral("Sync On") : QStringLiteral("Sync Off");
        syncAction->setToolTip(caption);
        syncAction->setStatusTip(caption);
        if (!checked)
            return;

        syncing = true;
        rightViewer->setViewExtent(leftViewer->viewExtent());
        syncing = false;
    });

    QObject::connect(zoomInAction, &QAction::triggered, leftViewer, [leftViewer]
    {
        leftViewer->zoomIn();
    });

    QObject::connect(zoomOutAction, &QAction::triggered, leftViewer, [leftViewer]
    {
        leftViewer->zoomOut();
    });

    QObject::connect(fullExtentAction, &QAction::triggered, leftViewer, [leftViewer]
    {
        leftViewer->fullExtent();
    });

    QObject::connect(zoomBoxAction, &QAction::triggered, leftViewer, [leftViewer, rightViewer]
    {
        leftViewer->setActiveTool(GisViewerTool::ZoomBox);
        rightViewer->setActiveTool(GisViewerTool::ZoomBox);
    });

    QObject::connect(panAction, &QAction::triggered, leftViewer, [leftViewer, rightViewer]
    {
        leftViewer->setActiveTool(GisViewerTool::Pan);
        rightViewer->setActiveTool(GisViewerTool::Pan);
    });

    window.show();

    const GisExtent initialExtent(-151.2, 16.4, -41.6, 55.6);
    leftViewer->setViewExtent(initialExtent);
    rightViewer->setViewExtent(initialExtent);

    return app.exec();
}
