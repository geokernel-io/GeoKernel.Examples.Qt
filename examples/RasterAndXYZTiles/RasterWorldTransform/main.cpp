#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSize>
#include <QSpinBox>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTextEdit>
#include <QToolBar>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <memory>

#include "Layers/Defs/GisRasterWorldTransform.h"
#include "Layers/GisLayerStyle.h"
#include "Layers/GisLayerVector.h"
#include "Common/Gdal/GdalRasterMetadata.h"
#include "Raster/Tiff/GisLayerTIFF.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShapePoint.h"
#include "Viewer/GisViewer.h"

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Layers::Defs;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Formats::Common::Gdal;
using namespace GeoKernel::Formats::Raster::Tiff;
using namespace GeoKernel::Viewer;

namespace
{
    struct PixelCoordinate
    {
        double x = 0.0;
        double y = 0.0;
        bool valid = false;
    };

    QString sampleDataPath(const QString& fileName)
    {
        const QDir appDir(QCoreApplication::applicationDirPath());
        return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/data/%1").arg(fileName)));
    }

    GisShapePoint pixelToWorld(const GisRasterWorldTransform& transform, double pixelX, double pixelY)
    {
        return GisShapePoint(
            transform.upperLeftCenterX + pixelX * transform.pixelSizeX + pixelY * transform.rotationY,
            transform.upperLeftCenterY + pixelX * transform.rotationX + pixelY * transform.pixelSizeY);
    }

    PixelCoordinate worldToPixel(const GisRasterWorldTransform& transform, const GisShapePoint& worldPoint)
    {
        const double dx = worldPoint.x() - transform.upperLeftCenterX;
        const double dy = worldPoint.y() - transform.upperLeftCenterY;
        const double determinant = transform.pixelSizeX * transform.pixelSizeY - transform.rotationY * transform.rotationX;

        if (std::abs(determinant) < 1e-12)
            return {};

        return PixelCoordinate {
            (dx * transform.pixelSizeY - transform.rotationY * dy) / determinant,
            (transform.pixelSizeX * dy - dx * transform.rotationX) / determinant,
            true
        };
    }

    GisLayerStyle markerStyle()
    {
        GisLayerStyle style;
        style.setPointColor(QStringLiteral("#D95D39"));
        style.setLineColor(QStringLiteral("#8C321D"));
        style.setPointSize(13.0f);
        style.setLineWidth(1.5f);
        return style;
    }

    void replaceMarker(GisLayerVector& markerLayer, const GisShapePoint& point)
    {
        if (!markerLayer.isEditing())
            markerLayer.beginEdit();

        while (markerLayer.count() > 0)
        {
            const QVector<GisShape*> shapes = markerLayer.items();
            if (shapes.isEmpty() || shapes.first() == nullptr)
                break;

            markerLayer.deleteShapeEdit(shapes.first()->id());
        }

        markerLayer.addShapeEdit(std::make_unique<GisShapePoint>(point));
        markerLayer.commitEdit();
        markerLayer.clearEditHistory();
    }

    QString transformText(
        const GisLayerTIFF& layer,
        int pixelX,
        int pixelY,
        const GisShapePoint& worldPoint,
        const PixelCoordinate& reversePixel)
    {
        const GdalRasterMetadata& metadata = layer.metadata();
        const GisRasterWorldTransform& transform = layer.worldTransform();

        QStringList details;
        details << QStringLiteral("RasterWorldTransform sample");
        details << QStringLiteral("");
        details << QStringLiteral("Raster");
        details << QStringLiteral("Path: %1").arg(metadata.path);
        details << QStringLiteral("Size: %1 x %2 px").arg(metadata.width).arg(metadata.height);
        details << QStringLiteral("EPSG: %1").arg(metadata.epsgCode == 0 ? QStringLiteral("unknown") : QString::number(metadata.epsgCode));
        details << QStringLiteral("");
        details << QStringLiteral("GisRasterWorldTransform");
        details << QStringLiteral("upperLeftCenterX: %1").arg(transform.upperLeftCenterX, 0, 'f', 6);
        details << QStringLiteral("upperLeftCenterY: %1").arg(transform.upperLeftCenterY, 0, 'f', 6);
        details << QStringLiteral("pixelSizeX: %1").arg(transform.pixelSizeX, 0, 'f', 6);
        details << QStringLiteral("pixelSizeY: %1").arg(transform.pixelSizeY, 0, 'f', 6);
        details << QStringLiteral("rotationX: %1").arg(transform.rotationX, 0, 'f', 6);
        details << QStringLiteral("rotationY: %1").arg(transform.rotationY, 0, 'f', 6);
        details << QStringLiteral("");
        details << QStringLiteral("Selected pixel");
        details << QStringLiteral("Pixel X/Y: %1, %2").arg(pixelX).arg(pixelY);
        details << QStringLiteral("World X/Y: %1, %2").arg(worldPoint.x(), 0, 'f', 3).arg(worldPoint.y(), 0, 'f', 3);
        details << QStringLiteral("Reverse pixel X/Y: %1, %2")
            .arg(reversePixel.valid ? QString::number(reversePixel.x, 'f', 3) : QStringLiteral("invalid"))
            .arg(reversePixel.valid ? QString::number(reversePixel.y, 'f', 3) : QStringLiteral("invalid"));
        details << QStringLiteral("");
        details << QStringLiteral("Formula");
        details << QStringLiteral("worldX = upperLeftCenterX + pixelX * pixelSizeX + pixelY * rotationY");
        details << QStringLiteral("worldY = upperLeftCenterY + pixelX * rotationX + pixelY * pixelSizeY");

        return details.join(QStringLiteral("\n"));
    }

    void refreshViewer(GisViewer& viewer)
    {
        viewer.invalidateRenderCache(true, true);
        viewer.refreshLayers();
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("RasterWorldTransform"));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    const QString tiffPath = sampleDataPath(QStringLiteral("tiff/usa_3857.tif"));
    auto rasterLayer = std::make_unique<GisLayerTIFF>(tiffPath);

    try
    {
        rasterLayer->open();
    }
    catch (const std::exception& ex)
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("RasterWorldTransform"),
            QStringLiteral("GeoTIFF could not be loaded:\n%1").arg(QString::fromUtf8(ex.what())));
        return 1;
    }

    GisLayerTIFF* tiffLayer = rasterLayer.get();
    viewer->addLayer(rasterLayer);

    auto markerLayer = GisLayerVector::createInMemory(
        QStringLiteral("Pixel Marker"),
        GisShapeType::Point,
        tiffLayer->extent());
    markerLayer->style() = markerStyle();
    markerLayer->open();
    GisLayerVector* markerLayerPtr = markerLayer.get();
    viewer->addLayer(markerLayer);

    auto* detailsView = new QTextEdit(&window);
    detailsView->setReadOnly(true);
    detailsView->setMinimumWidth(430);

    auto* pixelXSpin = new QSpinBox(&window);
    pixelXSpin->setRange(0, std::max(0, tiffLayer->width() - 1));
    pixelXSpin->setValue(tiffLayer->width() / 2);

    auto* pixelYSpin = new QSpinBox(&window);
    pixelYSpin->setRange(0, std::max(0, tiffLayer->height() - 1));
    pixelYSpin->setValue(tiffLayer->height() / 2);

    auto* panel = new QWidget(&window);
    auto* panelLayout = new QFormLayout(panel);
    panelLayout->addRow(QStringLiteral("Pixel X"), pixelXSpin);
    panelLayout->addRow(QStringLiteral("Pixel Y"), pixelYSpin);
    panelLayout->addRow(detailsView);

    auto* dock = new QDockWidget(QStringLiteral("Raster world transform"), &window);
    dock->setWidget(panel);
    window.addDockWidget(Qt::RightDockWidgetArea, dock);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QAction* zoomInAction = toolbar->addAction(sampleIcon(QStringLiteral("ZoomIn.svg")), QStringLiteral("Zoom In"));
    QAction* zoomOutAction = toolbar->addAction(sampleIcon(QStringLiteral("ZoomOut.svg")), QStringLiteral("Zoom Out"));
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));
    toolbar->addSeparator();

    QActionGroup toolGroup(&window);
    toolGroup.setExclusive(true);

    QAction* zoomRectAction = toolbar->addAction(sampleIcon(QStringLiteral("RectangularZoom.svg")), QStringLiteral("Zoom Rect"));
    zoomRectAction->setCheckable(true);
    toolGroup.addAction(zoomRectAction);

    QAction* panAction = toolbar->addAction(sampleIcon(QStringLiteral("Pan.svg")), QStringLiteral("Pan"));
    panAction->setCheckable(true);
    toolGroup.addAction(panAction);

    QAction* pickAction = toolbar->addAction(sampleIcon(QStringLiteral("Info.svg")), QStringLiteral("Pick World Point"));
    pickAction->setCheckable(true);
    toolGroup.addAction(pickAction);

    const auto updateFromPixel = [&]
    {
        const int pixelX = pixelXSpin->value();
        const int pixelY = pixelYSpin->value();
        const GisShapePoint worldPoint = pixelToWorld(tiffLayer->worldTransform(), pixelX, pixelY);
        const PixelCoordinate reversePixel = worldToPixel(tiffLayer->worldTransform(), worldPoint);

        replaceMarker(*markerLayerPtr, worldPoint);
        detailsView->setPlainText(transformText(*tiffLayer, pixelX, pixelY, worldPoint, reversePixel));
        refreshViewer(*viewer);
        window.statusBar()->showMessage(QStringLiteral("Pixel (%1, %2) -> World (%3, %4)")
            .arg(pixelX)
            .arg(pixelY)
            .arg(worldPoint.x(), 0, 'f', 2)
            .arg(worldPoint.y(), 0, 'f', 2));
    };

    QObject::connect(pixelXSpin, &QSpinBox::valueChanged, &window, updateFromPixel);
    QObject::connect(pixelYSpin, &QSpinBox::valueChanged, &window, updateFromPixel);

    QObject::connect(zoomInAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->zoomIn();
    });

    QObject::connect(zoomOutAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->zoomOut();
    });

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->fullExtent();
    });

    QObject::connect(zoomRectAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setActiveTool(GisViewerTool::ZoomBox);
    });

    QObject::connect(panAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setActiveTool(GisViewerTool::Pan);
    });

    QObject::connect(pickAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setActiveTool(GisViewerTool::Info);
    });

    QObject::connect(viewer, &GisViewer::mapClicked, viewer, [&](
        GisViewerTool tool,
        const QPointF&,
        const GisShapePoint& worldPoint,
        Qt::KeyboardModifiers)
    {
        if (tool != GisViewerTool::Info)
            return;

        const PixelCoordinate pixel = worldToPixel(tiffLayer->worldTransform(), worldPoint);
        if (!pixel.valid)
            return;

        const int clampedX = std::clamp(static_cast<int>(std::round(pixel.x)), pixelXSpin->minimum(), pixelXSpin->maximum());
        const int clampedY = std::clamp(static_cast<int>(std::round(pixel.y)), pixelYSpin->minimum(), pixelYSpin->maximum());

        {
            const QSignalBlocker blockX(pixelXSpin);
            const QSignalBlocker blockY(pixelYSpin);
            pixelXSpin->setValue(clampedX);
            pixelYSpin->setValue(clampedY);
        }

        updateFromPixel();
    });

    panAction->setChecked(true);
    updateFromPixel();

    window.show();
    viewer->fullExtent();

    return app.exec();
}
