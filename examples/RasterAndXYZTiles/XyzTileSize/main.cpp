#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QSize>
#include <QSplitter>
#include <QStatusBar>
#include <QString>
#include <QTextEdit>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

#include "Raster/Xyz/GisLayerXYZ.h"
#include "Shapes/GisExtent.h"
#include "Viewer/GisViewer.h"

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Formats::Raster::Xyz;
using namespace GeoKernel::Viewer;

namespace
{
    constexpr int LeftTileSize = 256;
    constexpr int RightTileSize = 512;

    GisExtent defaultExtent3857()
    {
        return GisExtent(
            -1400000.0,
            4100000.0,
            4200000.0,
            7800000.0);
    }

    QString osmTemplate()
    {
        return QStringLiteral("https://tile.openstreetmap.org/{z}/{x}/{y}.png");
    }

    QString cacheDirectoryFor(int tileSize)
    {
        return QDir(QCoreApplication::applicationDirPath()).filePath(
            QStringLiteral("XyzTileSizeCache/%1").arg(tileSize));
    }

    void addTileLayer(GisViewer& viewer, int tileSize)
    {
        viewer.clearLayers();

        auto layer = std::make_unique<GisLayerXYZ>(
            QStringLiteral("OSM tileSize %1").arg(tileSize),
            osmTemplate(),
            0,
            19,
            256,
            QStringLiteral("OpenStreetMap contributors"));
        layer->setTileSize(tileSize);
        layer->setLocalCacheEnabled(true);
        layer->setCacheDirectory(cacheDirectoryFor(tileSize));
        layer->open();

        viewer.addLayer(layer);
        viewer.setViewExtent(defaultExtent3857());
    }

    QWidget* createViewerPanel(QMainWindow& window, GisViewer*& viewer, const QString& title, int tileSize)
    {
        auto* panel = new QWidget(&window);
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto* label = new QLabel(QStringLiteral("%1 | setTileSize(%2)").arg(title).arg(tileSize), panel);
        label->setMinimumHeight(26);
        label->setStyleSheet(QStringLiteral("QLabel { background: #eeeeee; padding-left: 6px; font-weight: 600; }"));
        layout->addWidget(label);

        viewer = new GisViewer(panel);
        viewer->setActiveTool(GisViewerTool::Pan);
        layout->addWidget(viewer, 1);

        addTileLayer(*viewer, tileSize);
        return panel;
    }

    QString detailsText()
    {
        QStringList details;
        details << QStringLiteral("XYZ tile size sample");
        details << QStringLiteral("");
        details << QStringLiteral("Left map:");
        details << QStringLiteral("GisLayerXYZ + setTileSize(256)");
        details << QStringLiteral("");
        details << QStringLiteral("Right map:");
        details << QStringLiteral("GisLayerXYZ + setTileSize(512)");
        details << QStringLiteral("");
        details << QStringLiteral("URL template:");
        details << osmTemplate();
        details << QStringLiteral("");
        details << QStringLiteral("Why this matters:");
        details << QStringLiteral("- tileSize is the expected pixel size of one downloaded tile.");
        details << QStringLiteral("- Standard OSM tiles are usually 256 px.");
        details << QStringLiteral("- Some services expose 512 px retina/high-DPI tiles.");
        details << QStringLiteral("- The cache key includes tileSize, so 256 and 512 variants stay separate.");
        details << QStringLiteral("");
        details << QStringLiteral("SDK flow:");
        details << QStringLiteral("auto layer = std::make_unique<GisLayerXYZ>(name, urlTemplate);");
        details << QStringLiteral("layer->setTileSize(256 or 512);");
        details << QStringLiteral("layer->open();");
        details << QStringLiteral("viewer.addLayer(layer);");

        return details.join(QStringLiteral("\n"));
    }

    void setToolForBoth(GisViewer* leftViewer, GisViewer* rightViewer, GisViewerTool tool)
    {
        leftViewer->setActiveTool(tool);
        rightViewer->setActiveTool(tool);
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1280, 800);
    window.setWindowTitle(QStringLiteral("XyzTileSize"));

    auto* splitter = new QSplitter(Qt::Horizontal, &window);
    GisViewer* leftViewer = nullptr;
    GisViewer* rightViewer = nullptr;
    splitter->addWidget(createViewerPanel(window, leftViewer, QStringLiteral("256 px tiles"), LeftTileSize));
    splitter->addWidget(createViewerPanel(window, rightViewer, QStringLiteral("512 px tiles"), RightTileSize));
    splitter->setSizes({ 640, 640 });
    window.setCentralWidget(splitter);

    auto* detailsView = new QTextEdit(&window);
    detailsView->setReadOnly(true);
    detailsView->setMinimumWidth(340);
    detailsView->setPlainText(detailsText());
    auto* dock = new QDockWidget(QStringLiteral("Tile size details"), &window);
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

    QObject::connect(zoomInAction, &QAction::triggered, &window, [leftViewer, rightViewer]
    {
        leftViewer->zoomIn();
        rightViewer->zoomIn();
    });

    QObject::connect(zoomOutAction, &QAction::triggered, &window, [leftViewer, rightViewer]
    {
        leftViewer->zoomOut();
        rightViewer->zoomOut();
    });

    QObject::connect(fullExtentAction, &QAction::triggered, &window, [leftViewer, rightViewer]
    {
        leftViewer->setViewExtent(defaultExtent3857());
        rightViewer->setViewExtent(defaultExtent3857());
    });

    QObject::connect(zoomRectAction, &QAction::triggered, &window, [leftViewer, rightViewer]
    {
        setToolForBoth(leftViewer, rightViewer, GisViewerTool::ZoomBox);
    });

    QObject::connect(panAction, &QAction::triggered, &window, [leftViewer, rightViewer]
    {
        setToolForBoth(leftViewer, rightViewer, GisViewerTool::Pan);
    });

    panAction->setChecked(true);
    window.statusBar()->showMessage(QStringLiteral("Compare GisLayerXYZ::setTileSize(256) and setTileSize(512)."));

    window.show();
    leftViewer->setViewExtent(defaultExtent3857());
    rightViewer->setViewExtent(defaultExtent3857());

    return app.exec();
}
