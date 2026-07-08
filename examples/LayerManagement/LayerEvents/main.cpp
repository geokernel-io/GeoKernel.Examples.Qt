#include <QApplication>
#include <QDateTime>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

#include "Viewer/GisViewer.h"
#include "Shapes/GisExtent.h"
#include "Layers/GisLayer.h"
#include "Layers/GisLayerVector.h"

#include "Helpers.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;

struct LayerRequest
{
    QString name;
    QString zipName;
    QString targetFolder;
    QString requiredFileName;
    QString path;
    std::function<void(GisLayer&)> applyStyle;
};

QString layerName(const GisLayer* layer)
{
    return layer != nullptr && !layer->name().isEmpty()
        ? layer->name()
        : QStringLiteral("<none>");
}

void appendLog(QTextEdit& log, const QString& text)
{
    log.append(QStringLiteral("%1  %2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")))
        .arg(text));
}

bool layerMatches(const GisLayer& layer, const QString& name)
{
    return QString::compare(layer.name(), name, Qt::CaseInsensitive) == 0;
}

int layerIndex(const GisViewer& viewer, const QString& name)
{
    for (int i = 0; i < viewer.layerCount(); ++i)
    {
        const GisLayer* layer = viewer.mapLayerAt(i);
        if (layer != nullptr && layerMatches(*layer, name))
            return i;
    }

    return -1;
}

QString prepareLayerFile(QTextEdit& log, const LayerRequest& request, QWidget* parent)
{
    appendLog(log, QStringLiteral("Action: prepareSampleData(%1)").arg(request.zipName));
    return ensureSampleFile(
        QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/%1").arg(request.zipName)),
        request.zipName,
        request.targetFolder,
        request.requiredFileName,
        parent);
}

bool prepareLayerFiles(QTextEdit& log, QVector<LayerRequest>& requests, QWidget* parent)
{
    for (LayerRequest& request : requests)
    {
        request.path = prepareLayerFile(log, request, parent);
        if (request.path.isEmpty())
            return false;
    }

    return true;
}

bool addLayer(GisViewer& viewer, QTextEdit& log, const LayerRequest& request)
{
    if (layerIndex(viewer, request.name) >= 0)
    {
        appendLog(log, QStringLiteral("Action skipped: %1 already exists").arg(request.name));
        return true;
    }

    appendLog(log, QStringLiteral("Action: addLayerFromPath(%1)").arg(request.path));

    QString errorMessage;
    if (!viewer.addLayerFromPath(request.path, &errorMessage))
    {
        QMessageBox::critical(
            &viewer,
            QStringLiteral("LayerEvents"),
            QStringLiteral("Layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? request.path : errorMessage));
        return false;
    }

    if (GisLayer* layer = viewer.mapLayerAt(0))
    {
        layer->setName(request.name);
        if (request.applyStyle)
            request.applyStyle(*layer);
    }

    return true;
}

bool addPreparedLayers(GisViewer& viewer, QTextEdit& log, const QVector<LayerRequest>& requests)
{
    for (const LayerRequest& request : requests)
    {
        if (!addLayer(viewer, log, request))
            return false;
    }

    viewer.refreshLayers();
    return true;
}

bool prepareAndAddLayer(GisViewer& viewer, QTextEdit& log, LayerRequest request, QWidget* parent)
{
    if (layerIndex(viewer, request.name) >= 0)
        return addLayer(viewer, log, request);

    request.path = prepareLayerFile(log, request, parent);
    if (request.path.isEmpty())
        return false;

    if (!addLayer(viewer, log, request))
        return false;

    viewer.refreshLayers();
    return true;
}

void refreshLayerList(const GisViewer& viewer, QListWidget& list, int selectedIndex = -1)
{
    list.clear();
    for (int i = 0; i < viewer.layerCount(); ++i)
    {
        const GisLayer* layer = viewer.mapLayerAt(i);
        const QString visibleState = layer != nullptr && layer->visible() ? QStringLiteral("[x]") : QStringLiteral("[ ]");
        list.addItem(QStringLiteral("%1 %2").arg(visibleState, viewer.layerDisplayText(i)));
    }

    if (selectedIndex >= 0 && selectedIndex < list.count())
        list.setCurrentRow(selectedIndex);
    else if (list.count() > 0)
        list.setCurrentRow(0);
}

void removeSelectedLayer(GisViewer& viewer, QTextEdit& log, QListWidget& list)
{
    const int index = list.currentRow();
    const GisLayer* layer = viewer.mapLayerAt(index);
    if (layer == nullptr)
        return;

    appendLog(log, QStringLiteral("Action: removeLayer(%1)").arg(layerName(layer)));
    viewer.removeLayer(index);
}

void toggleSelectedLayer(GisViewer& viewer, QTextEdit& log, QListWidget& list)
{
    const int index = list.currentRow();
    GisLayer* layer = viewer.mapLayerAt(index);
    if (layer == nullptr)
        return;

    appendLog(log, QStringLiteral("Action: setLayerVisible(%1, %2)")
        .arg(layerName(layer), layer->visible() ? QStringLiteral("false") : QStringLiteral("true")));
    viewer.setLayerVisible(index, !layer->visible());
}

void moveSelectedLayer(GisViewer& viewer, QTextEdit& log, QListWidget& list, int delta)
{
    const int fromIndex = list.currentRow();
    const int toIndex = fromIndex + delta;
    if (fromIndex < 0 || toIndex < 0 || toIndex >= viewer.layerCount())
        return;

    appendLog(log, QStringLiteral("Action: moveLayer(%1 -> %2)").arg(fromIndex).arg(toIndex));
    if (viewer.moveLayer(fromIndex, toIndex))
        refreshLayerList(viewer, list, toIndex);
}

void connectLayerSignals(GisViewer& viewer, QTextEdit& log, QListWidget& layerList)
{
    QObject::connect(&viewer, &GisViewer::layerAdding, &log, [&log](const QString& name)
    {
        appendLog(log, QStringLiteral("Signal: layerAdding(%1)").arg(name));
    });

    QObject::connect(&viewer, &GisViewer::layerAdded, &log, [&log](GisLayer* layer)
    {
        appendLog(log, QStringLiteral("Signal: layerAdded(%1)").arg(layerName(layer)));
    });

    QObject::connect(&viewer, &GisViewer::layerRemoving, &log, [&log](GisLayer* layer)
    {
        appendLog(log, QStringLiteral("Signal: layerRemoving(%1)").arg(layerName(layer)));
    });

    QObject::connect(&viewer, &GisViewer::layerRemoved, &log, [&log](const QString& name)
    {
        appendLog(log, QStringLiteral("Signal: layerRemoved(%1)").arg(name));
    });

    QObject::connect(&viewer, &GisViewer::layerVisibilityChanged, &log, [&log](GisLayer* layer, bool visible)
    {
        appendLog(log, QStringLiteral("Signal: layerVisibilityChanged(%1, %2)")
            .arg(layerName(layer), visible ? QStringLiteral("true") : QStringLiteral("false")));
    });

    QObject::connect(&viewer, &GisViewer::layerEditStateChanged, &log, [&log](GisLayer* layer)
    {
        appendLog(log, QStringLiteral("Signal: layerEditStateChanged(%1)").arg(layerName(layer)));
    });

    QObject::connect(&viewer, &GisViewer::layerEditSessionStarted, &log, [&log](GisLayer* layer)
    {
        appendLog(log, QStringLiteral("Signal: layerEditSessionStarted(%1)").arg(layerName(layer)));
    });

    QObject::connect(&viewer, &GisViewer::layerEditSessionCommitted, &log, [&log](GisLayer* layer)
    {
        appendLog(log, QStringLiteral("Signal: layerEditSessionCommitted(%1)").arg(layerName(layer)));
    });

    QObject::connect(&viewer, &GisViewer::layerEditSessionRolledBack, &log, [&log](GisLayer* layer)
    {
        appendLog(log, QStringLiteral("Signal: layerEditSessionRolledBack(%1)").arg(layerName(layer)));
    });

    QObject::connect(&viewer, &GisViewer::layerOrderChanged, &log, [&log]
    {
        appendLog(log, QStringLiteral("Signal: layerOrderChanged()"));
    });

    QObject::connect(&viewer, &GisViewer::indexCreating, &log, [&log](const QString& name)
    {
        appendLog(log, QStringLiteral("Signal: indexCreating(%1)").arg(name));
    });

    QObject::connect(&viewer, &GisViewer::indexCreated, &log, [&log](const QString& name)
    {
        appendLog(log, QStringLiteral("Signal: indexCreated(%1)").arg(name));
    });

    QObject::connect(&viewer, &GisViewer::indexLoaded, &log, [&log](const QString& name)
    {
        appendLog(log, QStringLiteral("Signal: indexLoaded(%1)").arg(name));
    });

    QObject::connect(&viewer, &GisViewer::renderBackendChanged, &log, [&log](const QString& backend, bool hardwareAccelerated, bool fallback)
    {
        appendLog(log, QStringLiteral("Signal: renderBackendChanged(%1, hardware=%2, fallback=%3)")
            .arg(backend, hardwareAccelerated ? QStringLiteral("true") : QStringLiteral("false"), fallback ? QStringLiteral("true") : QStringLiteral("false")));
    });

    QObject::connect(&viewer, &GisViewer::layersChanged, &layerList, [&viewer, &log, &layerList]
    {
        const int selectedIndex = layerList.currentRow();
        refreshLayerList(viewer, layerList, selectedIndex);
        appendLog(log, QStringLiteral("Signal: layersChanged(count=%1)").arg(viewer.layerCount()));
    });
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1280, 820);
    window.setWindowTitle(QStringLiteral("LayerEvents"));

    auto* centralWidget = new QWidget(&window);
    auto* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* sidePanel = new QWidget(centralWidget);
    sidePanel->setFixedWidth(300);
    auto* sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(8, 8, 8, 8);
    sideLayout->setSpacing(6);

    auto* addWorldButton = new QPushButton(QStringLiteral("Add World"), sidePanel);
    auto* addStatesButton = new QPushButton(QStringLiteral("Add States"), sidePanel);
    auto* addCitiesButton = new QPushButton(QStringLiteral("Add Cities"), sidePanel);
    auto* removeSelectedButton = new QPushButton(QStringLiteral("Remove Selected"), sidePanel);
    auto* clearLayersButton = new QPushButton(QStringLiteral("Clear Layers"), sidePanel);
    auto* toggleVisibilityButton = new QPushButton(QStringLiteral("Toggle Visibility"), sidePanel);
    auto* moveUpButton = new QPushButton(QStringLiteral("Move Up"), sidePanel);
    auto* moveDownButton = new QPushButton(QStringLiteral("Move Down"), sidePanel);
    auto* refreshButton = new QPushButton(QStringLiteral("Refresh"), sidePanel);
    auto* clearLogButton = new QPushButton(QStringLiteral("Clear Log"), sidePanel);
    auto* layerList = new QListWidget(sidePanel);
    auto* eventLog = new QTextEdit(sidePanel);
    eventLog->setReadOnly(true);
    eventLog->setMinimumHeight(260);

    sideLayout->addWidget(addWorldButton);
    sideLayout->addWidget(addStatesButton);
    sideLayout->addWidget(addCitiesButton);
    sideLayout->addWidget(removeSelectedButton);
    sideLayout->addWidget(clearLayersButton);
    sideLayout->addWidget(toggleVisibilityButton);
    sideLayout->addWidget(moveUpButton);
    sideLayout->addWidget(moveDownButton);
    sideLayout->addWidget(refreshButton);
    sideLayout->addWidget(clearLogButton);
    sideLayout->addWidget(layerList, 1);
    sideLayout->addWidget(eventLog, 1);

    auto* viewer = new GisViewer(centralWidget);
    viewer->setActiveTool(GisViewerTool::Pan);

    mainLayout->addWidget(sidePanel);
    mainLayout->addWidget(viewer, 1);
    window.setCentralWidget(centralWidget);

    connectLayerSignals(*viewer, *eventLog, *layerList);

    const LayerRequest worldLayer {
        QStringLiteral("World"),
        QStringLiteral("world_4326.zip"),
        QStringLiteral("world_4326"),
        QStringLiteral("world_4326.shp"),
        QString(),
        [](GisLayer& layer)
        {
            layer.style().setFillColor(QStringLiteral("#D8E5E1"));
            layer.style().setFillOpacity(220);
            layer.style().setLineColor(QStringLiteral("#7B918D"));
            layer.style().setLineWidth(0.8f);
        }
    };

    const LayerRequest statesLayer {
        QStringLiteral("States"),
        QStringLiteral("usa_states.zip"),
        QStringLiteral("usa_states"),
        QStringLiteral("usa_states.shp"),
        QString(),
        [](GisLayer& layer)
        {
            layer.style().setFillColor(QStringLiteral("#A9C8DB"));
            layer.style().setFillOpacity(115);
            layer.style().setLineColor(QStringLiteral("#356780"));
            layer.style().setLineWidth(1.2f);
        }
    };

    const LayerRequest citiesLayer {
        QStringLiteral("Cities"),
        QStringLiteral("usa_cities.zip"),
        QStringLiteral("usa_cities"),
        QStringLiteral("usa_cities.shp"),
        QString(),
        [](GisLayer& layer)
        {
            layer.style().setPointColor(QStringLiteral("#D95D39"));
            layer.style().setPointSize(7.0f);
        }
    };

    QObject::connect(addWorldButton, &QPushButton::clicked, viewer, [viewer, eventLog, worldLayer]
    {
        prepareAndAddLayer(*viewer, *eventLog, worldLayer, viewer);
    });

    QObject::connect(addStatesButton, &QPushButton::clicked, viewer, [viewer, eventLog, statesLayer]
    {
        prepareAndAddLayer(*viewer, *eventLog, statesLayer, viewer);
    });

    QObject::connect(addCitiesButton, &QPushButton::clicked, viewer, [viewer, eventLog, citiesLayer]
    {
        prepareAndAddLayer(*viewer, *eventLog, citiesLayer, viewer);
    });

    QObject::connect(removeSelectedButton, &QPushButton::clicked, viewer, [viewer, eventLog, layerList]
    {
        removeSelectedLayer(*viewer, *eventLog, *layerList);
    });

    QObject::connect(clearLayersButton, &QPushButton::clicked, viewer, [viewer, eventLog]
    {
        appendLog(*eventLog, QStringLiteral("Action: clearLayers()"));
        viewer->clearLayers();
    });

    QObject::connect(toggleVisibilityButton, &QPushButton::clicked, viewer, [viewer, eventLog, layerList]
    {
        toggleSelectedLayer(*viewer, *eventLog, *layerList);
    });

    QObject::connect(moveUpButton, &QPushButton::clicked, viewer, [viewer, eventLog, layerList]
    {
        moveSelectedLayer(*viewer, *eventLog, *layerList, -1);
    });

    QObject::connect(moveDownButton, &QPushButton::clicked, viewer, [viewer, eventLog, layerList]
    {
        moveSelectedLayer(*viewer, *eventLog, *layerList, 1);
    });

    QObject::connect(refreshButton, &QPushButton::clicked, viewer, [viewer, eventLog]
    {
        appendLog(*eventLog, QStringLiteral("Action: refreshLayers()"));
        viewer->refreshLayers();
    });

    QObject::connect(clearLogButton, &QPushButton::clicked, eventLog, &QTextEdit::clear);

    window.show();

    QMetaObject::invokeMethod(&window, [viewer, eventLog, layerList, worldLayer, statesLayer, citiesLayer]
    {
        QVector<LayerRequest> initialLayers { worldLayer, statesLayer, citiesLayer };
        if (!prepareLayerFiles(*eventLog, initialLayers, viewer))
            return;

        if (!addPreparedLayers(*viewer, *eventLog, initialLayers))
            return;

        refreshLayerList(*viewer, *layerList);
        viewer->setViewExtent(GisExtent(-151.2, 16.4, -41.6, 55.6));
    });

    return app.exec();
}
