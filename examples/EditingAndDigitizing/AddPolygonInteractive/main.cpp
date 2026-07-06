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

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;

QIcon sampleIcon(const QString& fileName)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString path = QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/images/%1").arg(fileName)));
    QIcon icon;

    for (const auto mode : { QIcon::Normal, QIcon::Active, QIcon::Selected, QIcon::Disabled })
    {
        icon.addFile(path, QSize(), mode, QIcon::Off);
        icon.addFile(path, QSize(), mode, QIcon::On);
    }

    return icon;
}

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
        QStringLiteral("AddPolygonInteractive"),
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

GisLayerStyle polygonStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#F2D27A"));
    style.setFillOpacity(160);
    style.setLineColor(QStringLiteral("#D95D39"));
    style.setLineWidth(2.0f);
    return style;
}

std::unique_ptr<GisLayerVector> createPolygonLayer()
{
    auto layer = GisLayerVector::createInMemory(
        QStringLiteral("Drawn Polygons"),
        GisShapeType::Polygon,
        GisExtent(-180.0, -90.0, 180.0, 90.0));
    layer->style() = polygonStyle();
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
    app.setWindowIcon(sampleIcon(QStringLiteral("GeoKernelAppIcon.ico")));

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("AddPolygonInteractive"));
    window.statusBar()->showMessage(QStringLiteral("Choose Add Polygon, click vertices, then press Enter or double-click to finish."));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
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

    QAction* addPolygonAction = toolbar->addAction(sampleIcon(QStringLiteral("Polygon.svg")), QStringLiteral("Add Polygon"));
    addPolygonAction->setCheckable(true);
    toolGroup.addAction(addPolygonAction);

    QAction* panAction = toolbar->addAction(sampleIcon(QStringLiteral("Pan.svg")), QStringLiteral("Pan"));
    panAction->setCheckable(true);
    toolGroup.addAction(panAction);

    toolbar->addSeparator();
    QAction* clearAction = toolbar->addAction(sampleIcon(QStringLiteral("Delete.svg")), QStringLiteral("Clear Polygons"));

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

    auto polygonLayer = createPolygonLayer();
    auto* polygonLayerPtr = polygonLayer.get();
    viewer->addLayer(polygonLayer);

    const auto updateCount = [&] {
        countLabel->setText(QStringLiteral("Polygon count: %1").arg(polygonLayerPtr != nullptr ? polygonLayerPtr->count() : 0));
    };

    const auto activatePolygonEditing = [&]() -> bool {
        const int polygonLayerIndex = layerIndexOf(*viewer, polygonLayerPtr);
        if (polygonLayerIndex < 0)
        {
            window.statusBar()->showMessage(QStringLiteral("Drawn Polygons layer is not in the viewer."));
            return false;
        }

        if (!viewer->isLayerEditing(polygonLayerIndex) && !viewer->beginEditLayer(polygonLayerIndex))
        {
            window.statusBar()->showMessage(QStringLiteral("Drawn Polygons layer could not enter edit mode."));
            return false;
        }

        if (!viewer->setActiveEditLayerIndex(polygonLayerIndex))
        {
            window.statusBar()->showMessage(QStringLiteral("Drawn Polygons layer could not be activated for editing."));
            return false;
        }

        return true;
    };

    activatePolygonEditing();

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);

    QObject::connect(addPolygonAction, &QAction::triggered, viewer, [&] {
        if (!activatePolygonEditing())
            return;

        viewer->setActiveTool(GisViewerTool::AddPolygon);
        window.statusBar()->showMessage(QStringLiteral("Add Polygon active. Click vertices, then press Enter or double-click to finish."));
    });

    QObject::connect(panAction, &QAction::triggered, viewer, [&] {
        viewer->setActiveTool(GisViewerTool::Pan);
        window.statusBar()->showMessage(QStringLiteral("Pan tool active."));
    });

    QObject::connect(clearAction, &QAction::triggered, viewer, [&] {
        const int polygonLayerIndex = layerIndexOf(*viewer, polygonLayerPtr);
        viewer->rollbackEditLayer(polygonLayerIndex);
        activatePolygonEditing();
        refreshViewer(*viewer);
        updateCount();
        window.statusBar()->showMessage(QStringLiteral("Drawn polygons cleared."));
    });

    QObject::connect(viewer, &GisViewer::layerEditStateChanged, viewer, [&](GisLayer* layer) {
        if (layer != polygonLayerPtr)
            return;

        refreshViewer(*viewer);
        updateCount();
    });

    QObject::connect(viewer, &GisViewer::mouseCoordinatesChanged, viewer, [&](const QPointF&, const QPointF& worldPoint) {
        if (viewer->activeTool() != GisViewerTool::AddPolygon)
            return;

        window.statusBar()->showMessage(QStringLiteral("Add Polygon active. x=%1, y=%2")
            .arg(worldPoint.x(), 0, 'f', 4)
            .arg(worldPoint.y(), 0, 'f', 4));
    });

    addPolygonAction->setChecked(true);
    viewer->setActiveTool(GisViewerTool::AddPolygon);
    updateCount();

    window.show();

    viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));

    return app.exec();
}
