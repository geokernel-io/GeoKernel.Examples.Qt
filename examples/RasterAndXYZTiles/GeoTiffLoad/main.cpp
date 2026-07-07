#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QIcon>
#include <QMainWindow>
#include <QMessageBox>
#include <QSize>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTextEdit>
#include <QToolBar>

#include <memory>

#include "Common/Gdal/GdalRasterMetadata.h"
#include "Raster/Tiff/GisLayerTIFF.h"
#include "Shapes/GisExtent.h"
#include "Viewer/GisViewer.h"

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Formats::Common::Gdal;
using namespace GeoKernel::Formats::Raster::Tiff;
using namespace GeoKernel::Viewer;

QString sampleDataPath(const QString& fileName)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/data/%1").arg(fileName)));
}

QString extentText(const GisExtent& extent)
{
    return QStringLiteral("(%1, %2) - (%3, %4)")
        .arg(extent.xMin(), 0, 'f', 2)
        .arg(extent.yMin(), 0, 'f', 2)
        .arg(extent.xMax(), 0, 'f', 2)
        .arg(extent.yMax(), 0, 'f', 2);
}

QString metadataText(const GisLayerTIFF& layer)
{
    const GdalRasterMetadata& metadata = layer.metadata();
    const QFileInfo fileInfo(metadata.path);

    QStringList details;
    details << QStringLiteral("GeoTIFF load sample");
    details << QStringLiteral("");
    details << QStringLiteral("File");
    details << QStringLiteral("Path: %1").arg(metadata.path);
    details << QStringLiteral("Exists: %1").arg(fileInfo.exists() ? QStringLiteral("yes") : QStringLiteral("no"));
    details << QStringLiteral("Size: %1 bytes").arg(fileInfo.exists() ? fileInfo.size() : 0);
    details << QStringLiteral("");
    details << QStringLiteral("Raster");
    details << QStringLiteral("Driver: %1").arg(metadata.driverName);
    details << QStringLiteral("Width: %1 px").arg(metadata.width);
    details << QStringLiteral("Height: %1 px").arg(metadata.height);
    details << QStringLiteral("Bands: %1").arg(metadata.bandCount);
    details << QStringLiteral("EPSG: %1").arg(metadata.epsgCode == 0 ? QStringLiteral("unknown") : QString::number(metadata.epsgCode));
    details << QStringLiteral("Has world transform: %1").arg(metadata.hasWorldTransform ? QStringLiteral("yes") : QStringLiteral("no"));
    details << QStringLiteral("Layer extent: %1").arg(extentText(layer.extent()));
    details << QStringLiteral("Overview count: %1").arg(metadata.overviews.size());
    details << QStringLiteral("");
    details << QStringLiteral("SDK flow");
    details << QStringLiteral("auto layer = std::make_unique<GisLayerTIFF>(path);");
    details << QStringLiteral("layer->open();");
    details << QStringLiteral("viewer.addLayer(layer);");

    return details.join(QStringLiteral("\n"));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("GeoTiffLoad"));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* detailsView = new QTextEdit(&window);
    detailsView->setReadOnly(true);
    detailsView->setMinimumWidth(360);

    auto* dock = new QDockWidget(QStringLiteral("GeoTIFF metadata"), &window);
    dock->setWidget(detailsView);
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

    panAction->setChecked(true);

    const QString tiffPath = sampleDataPath(QStringLiteral("tiff/usa_3857.tif"));
    auto layer = std::make_unique<GisLayerTIFF>(tiffPath);

    try
    {
        layer->open();
    }
    catch (const std::exception& ex)
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("GeoTiffLoad"),
            QStringLiteral("GeoTIFF could not be loaded:\n%1").arg(QString::fromUtf8(ex.what())));
        return 1;
    }

    GisLayerTIFF* tiffLayer = layer.get();
    viewer->addLayer(layer);

    detailsView->setPlainText(metadataText(*tiffLayer));
    window.statusBar()->showMessage(QStringLiteral("GeoTIFF loaded: usa_3857.tif"));

    window.show();
    viewer->fullExtent();

    return app.exec();
}
