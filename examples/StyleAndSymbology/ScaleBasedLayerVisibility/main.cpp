#include <QApplication>
#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include "Viewer/GisViewer.h"
#include "Shapes/GisExtent.h"
#include "Layers/GisLayer.h"

#include "Helpers.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;

QString scaleText(double value)
{
    if (value <= 0.0)
        return QStringLiteral("-");

    return QString::number(value, 'f', value < 10.0 ? 2 : 0);
}

QString prepareLayerFile(QWidget* parent, const QString& zipName)
{
    return ensureSampleFile(
        QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/%1").arg(zipName)),
        zipName,
        zipName.left(zipName.size() - 4),
        QString(zipName).replace(QStringLiteral(".zip"), QStringLiteral(".shp")),
        parent);
}

GisLayer* addLayer(GisViewer& viewer, const QString& name, const QString& path)
{
    QString errorMessage;
    if (!viewer.addLayerFromPath(path, &errorMessage))
    {
        QMessageBox::critical(
            &viewer,
            QStringLiteral("ScaleBasedLayerVisibility"),
            QStringLiteral("Layer could not be loaded:\n%1")
            .arg(errorMessage.isEmpty() ? path : errorMessage));
        return nullptr;
    }

    GisLayer* layer = viewer.mapLayerAt(0);
    if (layer != nullptr)
        layer->setName(name);

    return layer;
}

QString layerText(const GisViewer& viewer, int index)
{
    const GisLayer* layer = viewer.mapLayerAt(index);
    if (layer == nullptr)
        return QString();

    return QStringLiteral("%1  [%2 - %3]  %4")
        .arg(layer->isVisibleAtScale(viewer.pixelsPerMapUnit()) ? QStringLiteral("[x]") : QStringLiteral("[ ]"))
        .arg(scaleText(layer->minVisibleScale()))
        .arg(scaleText(layer->maxVisibleScale()))
        .arg(viewer.layerDisplayText(index));
}

void refreshLayerList(const GisViewer& viewer, QListWidget& list)
{
    list.clear();
    for (int i = 0; i < viewer.layerCount(); ++i)
        list.addItem(layerText(viewer, i));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("ScaleBasedLayerVisibility"));

    auto* centralWidget = new QWidget(&window);
    auto* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* sidePanel = new QWidget(centralWidget);
    sidePanel->setFixedWidth(280);
    auto* sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(8, 8, 8, 8);
    sideLayout->setSpacing(8);

    auto* scaleLabel = new QLabel(sidePanel);
    auto* layerList = new QListWidget(sidePanel);

    sideLayout->addWidget(scaleLabel);
    sideLayout->addWidget(new QLabel(QStringLiteral("Visible scale ranges: [min - max]"), sidePanel));
    sideLayout->addWidget(layerList, 1);

    auto* viewer = new GisViewer(centralWidget);
    viewer->setActiveTool(GisViewerTool::Pan);

    mainLayout->addWidget(sidePanel);
    mainLayout->addWidget(viewer, 1);
    window.setCentralWidget(centralWidget);

    auto refreshUi = [viewer, layerList, scaleLabel]
    {
        scaleLabel->setText(QStringLiteral("Current scale: %1 px/map unit").arg(scaleText(viewer->pixelsPerMapUnit())));
        refreshLayerList(*viewer, *layerList);
    };

    QObject::connect(viewer, &GisViewer::zoomChanged, layerList, [refreshUi](double)
    {
        refreshUi();
    });
    QObject::connect(viewer, &GisViewer::layersChanged, layerList, refreshUi);
    
    refreshUi();
    window.show();

    QMetaObject::invokeMethod(&window, [viewer, layerList, refreshUi]
    {
        layerList->clear();
        layerList->addItem(QStringLiteral("Preparing sample data..."));

        const QString worldPath = prepareLayerFile(viewer, QStringLiteral("world_4326.zip"));
        if (worldPath.isEmpty())
        {
            refreshUi();
            return;
        }

        const QString statesPath = prepareLayerFile(viewer, QStringLiteral("usa_states.zip"));
        if (statesPath.isEmpty())
        {
            refreshUi();
            return;
        }

        const QString citiesPath = prepareLayerFile(viewer, QStringLiteral("usa_cities.zip"));
        if (citiesPath.isEmpty())
        {
            refreshUi();
            return;
        }

        GisLayer* worldLayer = addLayer(*viewer, QStringLiteral("World"), worldPath);
        if (worldLayer == nullptr)
            return;

        worldLayer->setMaxVisibleScale(11.0);
        worldLayer->style().setFillColor(QStringLiteral("#D8E5E1"));
        worldLayer->style().setFillOpacity(225);
        worldLayer->style().setLineColor(QStringLiteral("#7B918D"));
        worldLayer->style().setLineWidth(0.8f);

        GisLayer* statesLayer = addLayer(*viewer, QStringLiteral("States"), statesPath);
        if (statesLayer == nullptr)
            return;

        statesLayer->setMinVisibleScale(5.0);
        statesLayer->setMaxVisibleScale(45.0);
        statesLayer->style().setFillColor(QStringLiteral("#A9C8DB"));
        statesLayer->style().setFillOpacity(135);
        statesLayer->style().setLineColor(QStringLiteral("#356780"));
        statesLayer->style().setLineWidth(1.1f);

        GisLayer* citiesLayer = addLayer(*viewer, QStringLiteral("Cities"), citiesPath);
        if (citiesLayer == nullptr)
            return;

        citiesLayer->setMinVisibleScale(28.0);
        citiesLayer->style().setPointColor(QStringLiteral("#D95D39"));
        citiesLayer->style().setLineColor(QStringLiteral("#873A24"));
        citiesLayer->style().setPointSize(7.0f);
        citiesLayer->style().setLineWidth(1.0f);

        viewer->refreshLayers();
        refreshUi();
        viewer->setViewExtent(GisExtent(-151.2, 16.4, -41.6, 55.6));
    });

    return app.exec();
}
