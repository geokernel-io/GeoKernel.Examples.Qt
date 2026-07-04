#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#include "Viewer/GisViewer.h"
#include "Shapes/GisExtent.h"
#include "Layers/GisLayerVector.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;

QString sampleDataPath(const QString& fileName)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/data/%1").arg(fileName)));
}

GisLayerStyle worldStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#D8E5E1"));
    style.setFillOpacity(215);
    style.setLineColor(QStringLiteral("#6F8380"));
    style.setLineWidth(0.8f);
    return style;
}

GisLayerStyle cityLabelStyle(bool allowOverlap)
{
    GisLayerStyle style;
    style.setPointColor(QStringLiteral("#D56037"));
    style.setLineColor(QStringLiteral("#A23D23"));
    style.setPointSize(5.5f);
    style.setLineWidth(0.8f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("CITY_NAME"));
    style.setLabelFontSize(8.0f);
    style.setLabelColor(QStringLiteral("#1F2933"));
    style.setLabelHaloEnabled(true);
    style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
    style.setLabelHaloWidth(1.5f);
    style.setLabelAllowOverlap(allowOverlap);
    style.setLabelPlacementMode(GeoKernel::Core::Layers::LabelPlacementMode::Point);
    style.setLabelOffsetX(7.0f);
    style.setLabelOffsetY(-7.0f);
    return style;
}

GisLayerVector* vectorLayerAt(GisViewer& viewer, int index)
{
    return dynamic_cast<GisLayerVector*>(viewer.mapLayerAt(index));
}

GisLayerVector* vectorLayerByShapeType(GisViewer& viewer, GeoKernel::Core::Shapes::GisShapeType shapeType)
{
    for (int i = 0; i < viewer.layerCount(); i++)
    {
        auto* layer = vectorLayerAt(viewer, i);
        if (layer != nullptr && layer->shapeType() == shapeType)
            return layer;
    }

    return nullptr;
}

bool addComparisonLayers(
    GisViewer& viewer,
    const QString& worldPath,
    const QString& citiesPath,
    bool allowOverlap,
    QString* errorMessage)
{
    QString localError;
    if (!viewer.addLayerFromPath(worldPath, &localError))
    {
        if (errorMessage != nullptr)
            *errorMessage = localError;
        return false;
    }

    if (!viewer.addLayerFromPath(citiesPath, &localError))
    {
        if (errorMessage != nullptr)
            *errorMessage = localError;
        return false;
    }

    auto* worldLayer = vectorLayerByShapeType(viewer, GeoKernel::Core::Shapes::GisShapeType::Polygon);
    auto* citiesLayer = vectorLayerByShapeType(viewer, GeoKernel::Core::Shapes::GisShapeType::Point);
    if (worldLayer == nullptr || citiesLayer == nullptr)
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("Loaded sample layers are not vector layers.");
        return false;
    }

    worldLayer->setName(QStringLiteral("World"));
    worldLayer->style() = worldStyle();

    citiesLayer->setName(allowOverlap
        ? QStringLiteral("Cities - labelAllowOverlap true")
        : QStringLiteral("Cities - labelAllowOverlap false"));
    citiesLayer->style() = cityLabelStyle(allowOverlap);
    return true;
}

QWidget* createViewerPane(const QString& title, GisViewer*& viewer, QWidget* parent)
{
    auto* pane = new QWidget(parent);
    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* label = new QLabel(title, pane);
    label->setContentsMargins(8, 6, 8, 6);
    label->setStyleSheet(QStringLiteral("background:#eeeeee;font-weight:600;"));

    viewer = new GisViewer(pane);
    viewer->setMapBackgroundColor(QColor(247, 248, 250));
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(label);
    layout->addWidget(viewer, 1);
    return pane;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1300, 800);
    window.setWindowTitle(QStringLiteral("LabelCollisionOff"));

    auto* splitter = new QSplitter(Qt::Horizontal, &window);
    GisViewer* collisionOnViewer = nullptr;
    GisViewer* collisionOffViewer = nullptr;

    splitter->addWidget(createViewerPane(
        QStringLiteral("labelAllowOverlap = false"),
        collisionOnViewer,
        splitter));
    splitter->addWidget(createViewerPane(
        QStringLiteral("labelAllowOverlap = true"),
        collisionOffViewer,
        splitter));
    splitter->setSizes({ 650, 650 });
    window.setCentralWidget(splitter);

    const QString worldPath = sampleDataPath(QStringLiteral("shapefile/world_4326.shp"));
    const QString citiesPath = sampleDataPath(QStringLiteral("shapefile/cities_4326.shp"));
    if (!QFileInfo::exists(worldPath) || !QFileInfo::exists(citiesPath))
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("LabelCollisionOff"),
            QStringLiteral("Sample data was not found:\n%1\n%2").arg(worldPath, citiesPath));
        return 1;
    }

    QString errorMessage;
    if (!addComparisonLayers(*collisionOnViewer, worldPath, citiesPath, false, &errorMessage) ||
        !addComparisonLayers(*collisionOffViewer, worldPath, citiesPath, true, &errorMessage))
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("LabelCollisionOff"),
            QStringLiteral("Layers could not be loaded:\n%1").arg(errorMessage));
        return 1;
    }

    const GisExtent continentalUsExtent(-127.0, 23.0, -66.0, 50.0);
    collisionOnViewer->setViewExtent(continentalUsExtent);
    collisionOffViewer->setViewExtent(continentalUsExtent);

    window.statusBar()->showMessage(QStringLiteral("Left: collision filtering. Right: label overlap allowed."));
    window.show();
    return app.exec();
}
