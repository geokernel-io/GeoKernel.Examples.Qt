#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
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

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Symbology;

QString sampleDataPath(const QString& fileName)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/data/%1").arg(fileName)));
}

GisLayerStyle baseStateStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#D8E5E1"));
    style.setFillOpacity(220);
    style.setLineColor(QStringLiteral("#536B68"));
    style.setLineWidth(0.9f);
    return style;
}

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
    viewer->setMapBackgroundColor(QColor(247, 248, 250));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* legendList = new QListWidget(&window);
    auto* legendDock = new QDockWidget(QStringLiteral("STATE categories"), &window);
    legendDock->setWidget(legendList);
    legendDock->setMinimumWidth(180);
    window.addDockWidget(Qt::LeftDockWidgetArea, legendDock);

    viewer->addOpenStreetMapLayer();

    const QString path = sampleDataPath(QStringLiteral("shapefile/usa_states_3857.shp"));
    if (!QFileInfo::exists(path))
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("CategorizedRenderer"),
            QStringLiteral("Sample shapefile was not found:\n%1").arg(path));
        return 1;
    }

    QString errorMessage;
    if (!viewer->addLayerFromPath(path, &errorMessage))
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("CategorizedRenderer"),
            QStringLiteral("Layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? path : errorMessage));
        return 1;
    }

    auto* statesLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
    if (statesLayer == nullptr)
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("CategorizedRenderer"),
            QStringLiteral("Loaded layer is not a vector layer."));
        return 1;
    }

    statesLayer->setName(QStringLiteral("USA States - categorized by STATE"));
    statesLayer->style() = baseStateStyle();

    auto renderer = GisSymbolRendererFactory::createCategorizedRenderer(
        *statesLayer,
        QStringLiteral("STATE"),
        GisColorRampRegistry::ramp(QStringLiteral("Unique")),
        baseStateStyle(),
        64);

    if (!renderer)
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("CategorizedRenderer"),
            QStringLiteral("Could not create categorized renderer from STATE field."));
        return 1;
    }

    statesLayer->setSymbolRenderer(std::move(renderer));
    updateLegend(*legendList, *statesLayer);        

    window.statusBar()->showMessage(QStringLiteral("Categorized renderer applied: STATE"));    
    window.show();

    viewer->setViewExtent(GisExtent(-16831516.0, 1856556.0, -4631023.0, 7472472.0));

    return app.exec();
}
