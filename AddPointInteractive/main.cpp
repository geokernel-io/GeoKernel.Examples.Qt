#include "SampleSupport.h"

#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QMainWindow>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointF>
#include <QToolBar>
#include <QUrl>

#include "Layers/GisLayerVector.h"
#include "Shapes/GisExtent.h"
#include "Viewer/GisViewer.h"

#include <memory>

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Viewer;

namespace
{
    GisLayerVector* findVectorLayer(GisViewer& viewer, const QString& name)
    {
        for (int index = 0; index < viewer.layerCount(); ++index)
        {
            auto* layer = dynamic_cast<GisLayerVector*>(viewer.mapLayerAt(index));
            if (layer != nullptr && layer->name() == name)
            {
                return layer;
            }
        }

        return nullptr;
    }

    void refreshViewer(GisViewer& viewer)
    {
        viewer.invalidateRenderCache(true, true);
        viewer.refreshLayers();
    }

    int layerIndexOf(const GisViewer& viewer, const GisLayer* layer)
    {
        if (layer == nullptr)
            return -1;

        for (int index = 0; index < viewer.layerCount(); ++index)
        {
            if (viewer.mapLayerAt(index) == layer)
                return index;
        }

        return -1;
    }

    std::unique_ptr<GisLayerVector> createPointLayer()
    {
        auto layer = GisLayerVector::createInMemory(
            QStringLiteral("Clicked Points"),
            GisShapeType::Point,
            GisExtent(-180.0, -90.0, 180.0, 90.0));

        layer->style().setPointColor(QStringLiteral("#ff3b30"));
        layer->style().setPointSize(9.0);
        layer->style().setLineColor(QStringLiteral("#ffffff"));
        layer->style().setLineWidth(1.5);
        return layer;
    }
}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("AddPointInteractive"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico")));

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("AddPointInteractive"));
    window.resize(1200, 800);

    auto* viewer = new GisViewer(&window);
    window.setCentralWidget(viewer);

    auto* toolbar = window.addToolBar(QStringLiteral("Editing"));
    toolbar->setMovable(false);

    auto* fullExtentAction = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/full-extent.svg")),
        QStringLiteral("Full Extent"));
    auto* addPointAction = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/point.svg")),
        QStringLiteral("Add Point"));
    auto* panAction = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/pan.svg")),
        QStringLiteral("Pan"));
    auto* clearAction = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/delete.svg")),
        QStringLiteral("Clear Points"));

    addPointAction->setCheckable(true);
    panAction->setCheckable(true);
    panAction->setChecked(true);
    addPointAction->setEnabled(false);
    clearAction->setEnabled(false);
    fullExtentAction->setEnabled(false);

    GisLayerVector* pointLayer = nullptr;

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer]()
    {
        viewer->fullExtent();
    });

    QObject::connect(addPointAction, &QAction::triggered, viewer,
        [viewer, addPointAction, panAction](bool checked)
    {
        if (!checked)
        {
            panAction->setChecked(true);
            viewer->setActiveTool(GisViewerTool::Pan);
            return;
        }

        panAction->setChecked(false);
        viewer->setActiveTool(GisViewerTool::AddPoint);
    });

    QObject::connect(panAction, &QAction::triggered, viewer,
        [viewer, addPointAction](bool checked)
    {
        if (!checked)
        {
            addPointAction->setChecked(true);
            viewer->setActiveTool(GisViewerTool::AddPoint);
            return;
        }

        addPointAction->setChecked(false);
        viewer->setActiveTool(GisViewerTool::Pan);
    });

    QObject::connect(clearAction, &QAction::triggered, &window,
        [&window, viewer, &pointLayer]()
    {
        if (pointLayer == nullptr)
        {
            return;
        }

        const int pointLayerIndex = layerIndexOf(*viewer, pointLayer);
        if (pointLayerIndex < 0 || !viewer->rollbackEditLayer(pointLayerIndex) ||
            !viewer->beginEditLayer(pointLayerIndex))
        {
            QMessageBox::warning(
                &window,
                QStringLiteral("AddPointInteractive"),
                QStringLiteral("The temporary points could not be cleared."));
            return;
        }

        refreshViewer(*viewer);
    });

    window.show();

    QMetaObject::invokeMethod(&window, [&window, viewer, addPointAction, clearAction,
                                         fullExtentAction, &pointLayer]()
    {
        const QString shapeFilePath = ensureSampleFile(
            QUrl(QStringLiteral(
                "https://github.com/geokernel-io/GeoKernel.SampleData/releases/"
                "download/v1/world_4326.zip")),
            QStringLiteral("world_4326.zip"),
            QStringLiteral("world_4326"),
            QStringLiteral("world_4326.shp"),
            &window);

        if (shapeFilePath.isEmpty())
        {
            return;
        }

        QString errorMessage;
        if (!viewer->addLayerFromPath(shapeFilePath, &errorMessage))
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("AddPointInteractive"),
                QStringLiteral("The world layer could not be opened:\n%1")
                    .arg(errorMessage));
            return;
        }

        auto pointLayerOwner = createPointLayer();
        pointLayer = pointLayerOwner.get();
        viewer->addLayer(std::move(pointLayerOwner));

        pointLayer = findVectorLayer(*viewer, QStringLiteral("Clicked Points"));
        const int pointLayerIndex = layerIndexOf(*viewer, pointLayer);
        if (pointLayerIndex < 0 || !viewer->beginEditLayer(pointLayerIndex))
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("AddPointInteractive"),
                QStringLiteral("The editable point layer could not be initialized."));
            return;
        }

        viewer->setActiveEditLayerIndex(pointLayerIndex);
        viewer->setActiveTool(GisViewerTool::Pan);
        viewer->fullExtent();

        addPointAction->setEnabled(true);
        clearAction->setEnabled(true);
        fullExtentAction->setEnabled(true);
    }, Qt::QueuedConnection);

    const int result = application.exec();

    if (pointLayer != nullptr && pointLayer->isEditing())
    {
        const int pointLayerIndex = layerIndexOf(*viewer, pointLayer);
        if (pointLayerIndex >= 0)
            viewer->rollbackEditLayer(pointLayerIndex);
    }

    return result;
}
