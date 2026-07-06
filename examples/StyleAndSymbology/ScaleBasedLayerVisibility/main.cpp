#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

#include "Viewer/GisViewer.h"
#include "Shapes/GisExtent.h"
#include "Layers/GisLayer.h"

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

QString sampleDataPath(const QString& fileName)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/data/%1").arg(fileName)));
}

QString scaleText(double value)
{
    if (value <= 0.0)
        return QStringLiteral("-");

    return QString::number(value, 'f', value < 10.0 ? 2 : 0);
}

bool addLayer(GisViewer& viewer, const QString& name, const QString& fileName, const std::function<void(GisLayer&)>& configure)
{
    QString errorMessage;
    if (!viewer.addLayerFromPath(sampleDataPath(fileName), &errorMessage))
    {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("ScaleBasedLayerVisibility"),
            QStringLiteral("Layer could not be loaded:\n%1")
            .arg(errorMessage.isEmpty() ? fileName : errorMessage));
        return false;
    }

    if (GisLayer* layer = viewer.mapLayerAt(0))
    {
        layer->setName(name);
        configure(*layer);
    }

    viewer.refreshLayers();
    return true;
}

void configureWorld(GisLayer& layer)
{
    layer.setMaxVisibleScale(11.0);
    layer.style().setFillColor(QStringLiteral("#D8E5E1"));
    layer.style().setFillOpacity(225);
    layer.style().setLineColor(QStringLiteral("#7B918D"));
    layer.style().setLineWidth(0.8f);
}

void configureStates(GisLayer& layer)
{
    layer.setMinVisibleScale(5.0);
    layer.setMaxVisibleScale(45.0);
    layer.style().setFillColor(QStringLiteral("#A9C8DB"));
    layer.style().setFillOpacity(135);
    layer.style().setLineColor(QStringLiteral("#356780"));
    layer.style().setLineWidth(1.1f);
}

void configureCities(GisLayer& layer)
{
    layer.setMinVisibleScale(28.0);
    layer.style().setPointColor(QStringLiteral("#D95D39"));
    layer.style().setLineColor(QStringLiteral("#873A24"));
    layer.style().setPointSize(7.0f);
    layer.style().setLineWidth(1.0f);
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
    app.setWindowIcon(sampleIcon(QStringLiteral("GeoKernelAppIcon.ico")));

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
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);

    mainLayout->addWidget(sidePanel);
    mainLayout->addWidget(viewer, 1);
    window.setCentralWidget(centralWidget);

    if (!addLayer(*viewer, QStringLiteral("World"), QStringLiteral("shapefile/world_4326.shp"), configureWorld))
        return 1;

    if (!addLayer(*viewer, QStringLiteral("States"), QStringLiteral("shapefile/usa_states_3857.shp"), configureStates))
        return 1;

    if (!addLayer(*viewer, QStringLiteral("Cities"), QStringLiteral("kml/usa_cities_4326.kml"), configureCities))
        return 1;

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
    viewer->setViewExtent(GisExtent(-151.2, 16.4, -41.6, 55.6));

    return app.exec();
}
