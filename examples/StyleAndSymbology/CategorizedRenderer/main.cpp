#include <QApplication>
#include <QColor>
#include <QDockWidget>
#include <QIcon>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QStatusBar>
#include <QString>

#include <memory>

#include "Viewer/GisViewer.h"
#include "Layers/GisLayerVector.h"
#include "Symbology/GisColorRampRegistry.h"
#include "Symbology/GisSymbolRendererFactory.h"

#include "Helpers.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Symbology;

QIcon legendIcon(const GisLayerStyle& style)
{
    QPixmap pixmap(38, 22);
    pixmap.fill(Qt::transparent);

    QColor fill(style.fillColor());
    fill.setAlpha(style.fillOpacity());

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(style.lineColor()), 1.5));
    painter.setBrush(fill);
    painter.drawRect(QRectF(5, 4, 28, 14));
    return QIcon(pixmap);
}

void updateLegend(QListWidget& legend, const GisLayerVector& layer)
{
    legend.clear();
    if (layer.symbolRenderer() == nullptr)
        return;

    for (const GisSymbolLegendItem& item : layer.symbolRenderer()->legendItems())
    {
        if (!item.enabled)
            continue;

        auto* row = new QListWidgetItem(
            legendIcon(item.style),
            item.label.isEmpty() ? QStringLiteral("(empty)") : item.label);
        legend.addItem(row);
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("CategorizedRenderer"));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* legendList = new QListWidget(&window);
    auto* legendDock = new QDockWidget(QStringLiteral("STATE categories"), &window);
    legendDock->setWidget(legendList);
    legendDock->setMinimumWidth(180);
    window.addDockWidget(Qt::LeftDockWidgetArea, legendDock);

    window.show();

    QMetaObject::invokeMethod(&window, [&window, viewer, legendList]
    {
        legendList->clear();
        legendList->addItem(QStringLiteral("Preparing USA states sample data..."));

        const QString path = ensureSampleFile(
            QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/usa_states_3857.zip")),
            QStringLiteral("usa_states_3857.zip"),
            QStringLiteral("usa_states_3857"),
            QStringLiteral("usa_states_3857.shp"),
            &window);
        if (path.isEmpty())
        {
            legendList->clear();
            legendList->addItem(QStringLiteral("Sample data could not be prepared."));
            window.statusBar()->showMessage(QStringLiteral("Sample data could not be prepared."));
            return;
        }

        viewer->addOpenStreetMapLayer();

        QString errorMessage;
        if (!viewer->addLayerFromPath(path, &errorMessage))
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("CategorizedRenderer"),
                QStringLiteral("Layer could not be loaded:\n%1")
                    .arg(errorMessage.isEmpty() ? path : errorMessage));
            legendList->clear();
            legendList->addItem(QStringLiteral("Layer could not be loaded."));
            window.statusBar()->showMessage(QStringLiteral("Layer could not be loaded."));
            return;
        }

        auto* statesLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
        if (statesLayer == nullptr)
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("CategorizedRenderer"),
                QStringLiteral("Loaded layer is not a vector layer."));
            legendList->clear();
            legendList->addItem(QStringLiteral("Loaded layer is not a vector layer."));
            window.statusBar()->showMessage(QStringLiteral("Loaded layer is not a vector layer."));
            return;
        }

        GisLayerStyle stateStyle;
        stateStyle.setFillColor(QStringLiteral("#D8E5E1"));
        stateStyle.setFillOpacity(220);
        stateStyle.setLineColor(QStringLiteral("#536B68"));
        stateStyle.setLineWidth(0.9f);

        statesLayer->setName(QStringLiteral("USA States - categorized by STATE"));
        statesLayer->style() = stateStyle;

        auto renderer = GisSymbolRendererFactory::createCategorizedRenderer(
            *statesLayer,
            QStringLiteral("STATE"),
            GisColorRampRegistry::ramp(QStringLiteral("Unique")),
            stateStyle,
            64);

        if (!renderer)
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("CategorizedRenderer"),
                QStringLiteral("Could not create categorized renderer from STATE field."));
            legendList->clear();
            legendList->addItem(QStringLiteral("Categorized renderer could not be created."));
            window.statusBar()->showMessage(QStringLiteral("Categorized renderer could not be created."));
            return;
        }

        statesLayer->setSymbolRenderer(std::move(renderer));
        updateLegend(*legendList, *statesLayer);

        window.statusBar()->showMessage(QStringLiteral("Categorized renderer applied: STATE"));
        viewer->setViewExtent(GisExtent(-16831516.0, 1856556.0, -4631023.0, 7472472.0));
    });

    return app.exec();
}
