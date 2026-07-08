#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QGridLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSize>
#include <QStatusBar>
#include <QString>
#include <QTextEdit>
#include <QToolBar>
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

    QString defaultAttribution()
    {
        return QStringLiteral("© OpenStreetMap contributors");
    }

    QString cacheDirectory()
    {
        return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("XyzAttributionCache/osm"));
    }

    QString detailsText(const QString& attribution)
    {
        QStringList details;
        details << QStringLiteral("XYZ attribution sample");
        details << QStringLiteral("");
        details << QStringLiteral("URL template:");
        details << osmTemplate();
        details << QStringLiteral("");
        details << QStringLiteral("Applied attribution:");
        details << attribution;
        details << QStringLiteral("");
        details << QStringLiteral("What this sample shows:");
        details << QStringLiteral("- GisLayerXYZ stores attribution metadata on the layer.");
        details << QStringLiteral("- The sample also renders the same text as a map overlay.");
        details << QStringLiteral("- Project save/load preserves attribution for XYZ/WMS/WMTS layers.");
        details << QStringLiteral("");
        details << QStringLiteral("SDK flow:");
        details << QStringLiteral("auto layer = std::make_unique<GisLayerXYZ>(name, urlTemplate);");
        details << QStringLiteral("layer->setAttribution(attributionText);");
        details << QStringLiteral("layer->open();");
        details << QStringLiteral("viewer.addLayer(layer);");

        return details.join(QStringLiteral("\n"));
    }

    void applyAttribution(
        GisViewer& viewer,
        QLabel& attributionLabel,
        QTextEdit& detailsView,
        QStatusBar& statusBar,
        const QString& attributionText)
    {
        const GisExtent previousExtent = viewer.layerCount() > 0 ? viewer.viewExtent() : defaultExtent3857();
        const QString attribution = attributionText.trimmed().isEmpty()
            ? QStringLiteral("No attribution")
            : attributionText.trimmed();

        viewer.clearLayers();

        auto layer = std::make_unique<GisLayerXYZ>(
            QStringLiteral("OSM Attribution"),
            osmTemplate(),
            0,
            19,
            256);
        layer->setAttribution(attribution);
        layer->setLocalCacheEnabled(true);
        layer->setCacheDirectory(cacheDirectory());
        layer->open();

        viewer.addLayer(layer);
        viewer.setViewExtent(previousExtent);

        attributionLabel.setText(attribution);
        attributionLabel.setVisible(!attribution.trimmed().isEmpty());
        detailsView.setPlainText(detailsText(attribution));
        statusBar.showMessage(QStringLiteral("XYZ attribution applied."));
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("XyzAttribution"));

    auto* mapHost = new QWidget(&window);
    auto* mapLayout = new QGridLayout(mapHost);
    mapLayout->setContentsMargins(0, 0, 0, 0);
    mapLayout->setSpacing(0);

    auto* viewer = new GisViewer(mapHost);
    viewer->setActiveTool(GisViewerTool::Pan);
    mapLayout->addWidget(viewer, 0, 0);

    auto* attributionLabel = new QLabel(mapHost);
    attributionLabel->setMargin(6);
    attributionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    attributionLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        " background: rgba(255, 255, 255, 215);"
        " color: #1f2d2d;"
        " border: 1px solid rgba(120, 130, 130, 140);"
        " border-radius: 2px;"
        "}"));
    mapLayout->addWidget(attributionLabel, 0, 0, Qt::AlignRight | Qt::AlignBottom);

    window.setCentralWidget(mapHost);

    auto* detailsView = new QTextEdit(&window);
    detailsView->setReadOnly(true);
    detailsView->setMinimumWidth(350);
    auto* dock = new QDockWidget(QStringLiteral("Attribution details"), &window);
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

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Attribution:"), toolbar));

    auto* attributionEdit = new QLineEdit(toolbar);
    attributionEdit->setMinimumWidth(360);
    attributionEdit->setText(defaultAttribution());
    toolbar->addWidget(attributionEdit);

    QAction* applyAction = toolbar->addAction(QStringLiteral("Apply Attribution"));
    auto* osmButton = new QPushButton(QStringLiteral("OSM"), toolbar);
    auto* customButton = new QPushButton(QStringLiteral("Custom"), toolbar);
    toolbar->addWidget(osmButton);
    toolbar->addWidget(customButton);

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
        viewer->setViewExtent(defaultExtent3857());
    });

    QObject::connect(zoomRectAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setActiveTool(GisViewerTool::ZoomBox);
    });

    QObject::connect(panAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setActiveTool(GisViewerTool::Pan);
    });

    const auto applyText = [viewer, attributionLabel, detailsView, attributionEdit, &window]
    {
        applyAttribution(
            *viewer,
            *attributionLabel,
            *detailsView,
            *window.statusBar(),
            attributionEdit->text());
    };

    QObject::connect(applyAction, &QAction::triggered, &window, applyText);
    QObject::connect(attributionEdit, &QLineEdit::returnPressed, &window, applyText);
    QObject::connect(osmButton, &QPushButton::clicked, &window, [attributionEdit, applyText]
    {
        attributionEdit->setText(defaultAttribution());
        applyText();
    });
    QObject::connect(customButton, &QPushButton::clicked, &window, [attributionEdit, applyText]
    {
        attributionEdit->setText(QStringLiteral("Tiles © Custom Provider | Data © GeoKernel Sample"));
        applyText();
    });

    panAction->setChecked(true);

    applyText();

    window.show();
    viewer->setViewExtent(defaultExtent3857());

    return app.exec();
}
