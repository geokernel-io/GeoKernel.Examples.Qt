#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QSize>
#include <QStatusBar>
#include <QString>
#include <QToolBar>

#include <memory>

#include "Viewer/GisViewer.h"
#include "Shapes/GisExtent.h"
#include "Layers/GisLayerStyle.h"
#include "Layers/GisLayerVector.h"

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;

QString sampleDataPath(const QString& relativePath)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/data/%1").arg(relativePath)));
}

bool loadLayer(GisViewer& viewer, const QString& path)
{
    QString errorMessage;
    if (viewer.addLayerFromPath(path, &errorMessage))
        return true;

    QMessageBox::critical(
        nullptr,
        QStringLiteral("AddPolylineInteractive"),
        QStringLiteral("Layer could not be loaded:\n%1").arg(errorMessage.isEmpty() ? path : errorMessage));
    return false;
}

GisLayerStyle worldStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#D8E5E1"));
    style.setFillOpacity(210);
    style.setLineColor(QStringLiteral("#6F8883"));
    style.setLineWidth(0.7f);
    return style;
}

GisLayerStyle polylineStyle()
{
    GisLayerStyle style;
    style.setLineColor(QStringLiteral("#D95D39"));
    style.setLineWidth(2.6f);
    return style;
}

std::unique_ptr<GisLayerVector> createPolylineLayer()
{
    auto layer = GisLayerVector::createInMemory(
        QStringLiteral("Drawn Polylines"),
        GisShapeType::Polyline,
        GisExtent(-180.0, -90.0, 180.0, 90.0));
    layer->style() = polylineStyle();
    return layer;
}

int layerIndexOf(const GisViewer& viewer, const GisLayer* layer)
{
    if (layer == nullptr)
        return -1;

    for (int i = 0; i < viewer.layerCount(); ++i)
    {
        if (viewer.mapLayerAt(i) == layer)
            return i;
    }

    return -1;
}

void refreshViewer(GisViewer& viewer)
{
    viewer.invalidateRenderCache(true, true);
    viewer.refreshLayers();
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("AddPolylineInteractive"));
    window.statusBar()->showMessage(QStringLiteral("Choose Add Polyline, click vertices, then press Enter or double-click to finish."));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));
    toolbar->addSeparator();

    QActionGroup toolGroup(&window);
    toolGroup.setExclusive(true);

    QAction* addPolylineAction = toolbar->addAction(sampleIcon(QStringLiteral("Polyline.svg")), QStringLiteral("Add Polyline"));
    addPolylineAction->setCheckable(true);
    toolGroup.addAction(addPolylineAction);

    QAction* panAction = toolbar->addAction(sampleIcon(QStringLiteral("Pan.svg")), QStringLiteral("Pan"));
    panAction->setCheckable(true);
    toolGroup.addAction(panAction);

    toolbar->addSeparator();
    QAction* clearAction = toolbar->addAction(sampleIcon(QStringLiteral("Delete.svg")), QStringLiteral("Clear Lines"));

    auto* countLabel = new QLabel(&window);
    countLabel->setContentsMargins(12, 0, 12, 0);
    toolbar->addWidget(countLabel);

    if (!loadLayer(*viewer, sampleDataPath(QStringLiteral("shapefile/world_4326.shp"))))
        return 1;

    if (auto* worldLayer = viewer->mapLayerAt(0))
    {
        worldLayer->setName(QStringLiteral("World"));
        worldLayer->style() = worldStyle();
    }

    auto polylineLayer = createPolylineLayer();
    auto* polylineLayerPtr = polylineLayer.get();
    viewer->addLayer(polylineLayer);

    const auto updateCount = [&] {
        countLabel->setText(QStringLiteral("Polyline count: %1").arg(polylineLayerPtr != nullptr ? polylineLayerPtr->count() : 0));
    };

    const auto activatePolylineEditing = [&]() -> bool {
        const int polylineLayerIndex = layerIndexOf(*viewer, polylineLayerPtr);
        if (polylineLayerIndex < 0)
        {
            window.statusBar()->showMessage(QStringLiteral("Drawn Polylines layer is not in the viewer."));
            return false;
        }

        if (!viewer->isLayerEditing(polylineLayerIndex) && !viewer->beginEditLayer(polylineLayerIndex))
        {
            window.statusBar()->showMessage(QStringLiteral("Drawn Polylines layer could not enter edit mode."));
            return false;
        }

        if (!viewer->setActiveEditLayerIndex(polylineLayerIndex))
        {
            window.statusBar()->showMessage(QStringLiteral("Drawn Polylines layer could not be activated for editing."));
            return false;
        }

        return true;
    };

    activatePolylineEditing();

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);

    QObject::connect(addPolylineAction, &QAction::triggered, viewer, [&] {
        if (!activatePolylineEditing())
            return;

        viewer->setActiveTool(GisViewerTool::AddPolyline);
        window.statusBar()->showMessage(QStringLiteral("Add Polyline active. Click vertices, then press Enter or double-click to finish."));
    });

    QObject::connect(panAction, &QAction::triggered, viewer, [&] {
        viewer->setActiveTool(GisViewerTool::Pan);
        window.statusBar()->showMessage(QStringLiteral("Pan tool active."));
    });

    QObject::connect(clearAction, &QAction::triggered, viewer, [&] {
        const int polylineLayerIndex = layerIndexOf(*viewer, polylineLayerPtr);
        viewer->rollbackEditLayer(polylineLayerIndex);
        activatePolylineEditing();
        refreshViewer(*viewer);
        updateCount();
        window.statusBar()->showMessage(QStringLiteral("Drawn polylines cleared."));
    });

    QObject::connect(viewer, &GisViewer::layerEditStateChanged, viewer, [&](GisLayer* layer) {
        if (layer != polylineLayerPtr)
            return;

        refreshViewer(*viewer);
        updateCount();
    });

    QObject::connect(viewer, &GisViewer::mouseCoordinatesChanged, viewer, [&](const QPointF&, const QPointF& worldPoint) {
        if (viewer->activeTool() != GisViewerTool::AddPolyline)
            return;

        window.statusBar()->showMessage(QStringLiteral("Add Polyline active. x=%1, y=%2")
            .arg(worldPoint.x(), 0, 'f', 4)
            .arg(worldPoint.y(), 0, 'f', 4));
    });

    addPolylineAction->setChecked(true);
    viewer->setActiveTool(GisViewerTool::AddPolyline);
    updateCount();

    window.show();
    viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));

    return app.exec();
}
