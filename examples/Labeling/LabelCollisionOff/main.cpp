#include <QApplication>
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

#include "Helpers.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;

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
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(label);
    layout->addWidget(viewer, 1);
    return pane;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

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

    window.show();

    QMetaObject::invokeMethod(&window, [&window, collisionOnViewer, collisionOffViewer]
    {
        window.statusBar()->showMessage(QStringLiteral("Preparing world and city sample data..."));

        const QString worldPath = ensureSampleFile(
            QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/world_4326.zip")),
            QStringLiteral("world_4326.zip"),
            QStringLiteral("world_4326"),
            QStringLiteral("world_4326.shp"),
            &window);

        if (worldPath.isEmpty())
        {
            window.statusBar()->showMessage(QStringLiteral("World sample data could not be prepared."));
            return;
        }

        const QString citiesPath = ensureSampleFile(
            QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/world_cities_4326.zip")),
            QStringLiteral("world_cities_4326.zip"),
            QStringLiteral("world_cities_4326"),
            QStringLiteral("world_cities_4326.shp"),
            &window);

        if (citiesPath.isEmpty())
        {
            window.statusBar()->showMessage(QStringLiteral("City sample data could not be prepared."));
            return;
        }

        auto loadComparisonLayers = [](GisViewer& viewer, const QString& worldPath, const QString& citiesPath, bool allowOverlap, QString* errorMessage)
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
            auto& polygonStyle = worldLayer->style();
            polygonStyle.setFillColor(QStringLiteral("#D8E5E1"));
            polygonStyle.setFillOpacity(215);
            polygonStyle.setLineColor(QStringLiteral("#6F8380"));
            polygonStyle.setLineWidth(0.8f);

            citiesLayer->setName(allowOverlap
                ? QStringLiteral("Cities - labelAllowOverlap true")
                : QStringLiteral("Cities - labelAllowOverlap false"));
            auto& pointStyle = citiesLayer->style();
            pointStyle.setPointColor(QStringLiteral("#D56037"));
            pointStyle.setLineColor(QStringLiteral("#A23D23"));
            pointStyle.setPointSize(5.5f);
            pointStyle.setLineWidth(0.8f);
            pointStyle.setShowLabels(true);
            pointStyle.setLabelField(QStringLiteral("CITY_NAME"));
            pointStyle.setLabelFontSize(8.0f);
            pointStyle.setLabelColor(QStringLiteral("#1F2933"));
            pointStyle.setLabelHaloEnabled(true);
            pointStyle.setLabelHaloColor(QStringLiteral("#FFFFFF"));
            pointStyle.setLabelHaloWidth(1.5f);
            pointStyle.setLabelAllowOverlap(allowOverlap);
            pointStyle.setLabelPlacementMode(GeoKernel::Core::Layers::LabelPlacementMode::Point);
            pointStyle.setLabelOffsetX(7.0f);
            pointStyle.setLabelOffsetY(-7.0f);
            return true;
        };

        QString errorMessage;
        if (!loadComparisonLayers(*collisionOnViewer, worldPath, citiesPath, false, &errorMessage) ||
            !loadComparisonLayers(*collisionOffViewer, worldPath, citiesPath, true, &errorMessage))
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("LabelCollisionOff"),
                QStringLiteral("Layers could not be loaded:\n%1").arg(errorMessage));
            return;
        }

        const GisExtent continentalUsExtent(-127.0, 23.0, -66.0, 50.0);
        collisionOnViewer->setViewExtent(continentalUsExtent);
        collisionOffViewer->setViewExtent(continentalUsExtent);
        window.statusBar()->showMessage(QStringLiteral("Left: collision filtering. Right: label overlap allowed."));
    });

    return app.exec();
}
