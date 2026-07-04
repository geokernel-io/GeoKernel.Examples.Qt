#include <memory>

#include <QAction>
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
#include <QVector>

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
        QStringLiteral("AddPolylineProgrammatic"),
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
        QStringLiteral("Programmatic Polylines"),
        GisShapeType::Polyline,
        GisExtent(-180.0, -90.0, 180.0, 90.0));
    layer->style() = polylineStyle();
    return layer;
}

QVector<GisShapePoint> samplePolylineAt(int index)
{
    static constexpr double startX = -124.0;
    static constexpr double startY = 29.0;
    static constexpr double xStep = 7.0;
    static constexpr double yStep = 3.0;
    static constexpr int columns = 7;

    const int column = index % columns;
    const int row = index / columns;
    const double x = startX + column * xStep;
    const double y = startY + row * yStep;

    return {
        GisShapePoint(x, y),
        GisShapePoint(x + 2.2, y + 1.4),
        GisShapePoint(x + 4.8, y + 0.4),
        GisShapePoint(x + 6.4, y + 2.2)
    };
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

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("AddPolylineProgrammatic"));
    window.statusBar()->showMessage(QStringLiteral("Click Add Polyline to call addPolylineToEditLayer(index, worldPoints)."));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QAction* addPolylineAction = toolbar->addAction(sampleIcon(QStringLiteral("Polyline.svg")), QStringLiteral("Add Polyline"));
    QAction* clearAction = toolbar->addAction(sampleIcon(QStringLiteral("Delete.svg")), QStringLiteral("Clear Lines"));
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));

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

    int polylineCursor = 0;

    const auto updateCount = [&] {
        countLabel->setText(QStringLiteral("Polyline count: %1").arg(polylineLayerPtr != nullptr ? polylineLayerPtr->count() : 0));
    };

    const auto activatePolylineEditing = [&]() -> int {
        const int polylineLayerIndex = layerIndexOf(*viewer, polylineLayerPtr);
        if (polylineLayerIndex < 0)
        {
            window.statusBar()->showMessage(QStringLiteral("Programmatic Polylines layer is not in the viewer."));
            return -1;
        }

        if (!viewer->isLayerEditing(polylineLayerIndex) && !viewer->beginEditLayer(polylineLayerIndex))
        {
            window.statusBar()->showMessage(QStringLiteral("Programmatic Polylines layer could not enter edit mode."));
            return -1;
        }

        if (!viewer->setActiveEditLayerIndex(polylineLayerIndex))
        {
            window.statusBar()->showMessage(QStringLiteral("Programmatic Polylines layer could not be activated for editing."));
            return -1;
        }

        return polylineLayerIndex;
    };

    activatePolylineEditing();
    updateCount();

    QObject::connect(addPolylineAction, &QAction::triggered, viewer, [&] {
        const int polylineLayerIndex = activatePolylineEditing();
        if (polylineLayerIndex < 0)
            return;

        const QVector<GisShapePoint> worldPoints = samplePolylineAt(polylineCursor);
        if (!viewer->addPolylineToEditLayer(polylineLayerIndex, worldPoints))
        {
            window.statusBar()->showMessage(QStringLiteral("Polyline could not be added."));
            return;
        }

        ++polylineCursor;
        refreshViewer(*viewer);
        updateCount();
        window.statusBar()->showMessage(QStringLiteral("addPolylineToEditLayer(%1, %2 vertices)")
            .arg(polylineLayerIndex)
            .arg(worldPoints.size()));
    });

    QObject::connect(clearAction, &QAction::triggered, viewer, [&] {
        const int polylineLayerIndex = layerIndexOf(*viewer, polylineLayerPtr);
        viewer->rollbackEditLayer(polylineLayerIndex);
        activatePolylineEditing();
        polylineCursor = 0;
        refreshViewer(*viewer);
        updateCount();
        window.statusBar()->showMessage(QStringLiteral("Programmatic polylines cleared."));
    });

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);

    QObject::connect(viewer, &GisViewer::layerEditStateChanged, viewer, [&](GisLayer* layer) {
        if (layer != polylineLayerPtr)
            return;

        refreshViewer(*viewer);
        updateCount();
    });

    window.show();
    viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));
   

    return app.exec();
}
