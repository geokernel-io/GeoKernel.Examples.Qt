#include <algorithm>

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointF>
#include <QSize>
#include <QStatusBar>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolBar>
#include <QVariant>
#include <QUrl>

#include "FeatureSources/FeatureHitTestResult.h"
#include "Layers/GisLayerStyle.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShape.h"
#include "Shapes/GisShapePoint.h"
#include "Shapes/GisShapeType.h"
#include "Viewer/GisViewer.h"

#include "SampleSupport.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Viewer;
using namespace GeoKernel::Viewer::FeatureSources;

bool loadLayer(GisViewer& viewer, const QString& path, const QString& displayName, const GisLayerStyle& style)
{
    QString errorMessage;
    if (!viewer.addLayerFromPath(path, &errorMessage))
    {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("SelectClear"),
            QStringLiteral("Layer could not be loaded:\n%1")
            .arg(errorMessage.isEmpty() ? path : errorMessage));
        return false;
    }

    if (auto* layer = viewer.mapLayerAt(viewer.layerCount() - 1))
    {
        layer->setName(displayName);
        layer->style() = style;
    }

    return true;
}

GisLayerStyle worldStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#D8E5E1"));
    style.setFillOpacity(210);
    style.setLineColor(QStringLiteral("#708984"));
    style.setLineWidth(0.6f);
    style.setSelectedLineColor(QStringLiteral("#F59E0B"));
    style.setSelectedLineWidth(3.0f);
    return style;
}

GisLayerStyle stateStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#C7DEE7"));
    style.setFillOpacity(160);
    style.setLineColor(QStringLiteral("#2D6F8E"));
    style.setLineWidth(1.0f);
    style.setSelectedLineColor(QStringLiteral("#F59E0B"));
    style.setSelectedLineWidth(4.0f);
    return style;
}

GisLayerStyle cityStyle()
{
    GisLayerStyle style;
    style.setPointColor(QStringLiteral("#D95D39"));
    style.setLineColor(QStringLiteral("#8C321D"));
    style.setPointSize(8.0f);
    style.setLineWidth(1.0f);
    style.setSelectedLineColor(QStringLiteral("#F59E0B"));
    style.setSelectedLineWidth(4.0f);
    style.setShowLabels(true);
    style.setLabelField(QStringLiteral("NAME"));
    style.setLabelFontSize(9.0f);
    style.setLabelColor(QStringLiteral("#263238"));
    style.setLabelHaloEnabled(true);
    style.setLabelHaloColor(QStringLiteral("#FFFFFF"));
    style.setLabelHaloWidth(2.0f);
    style.setLabelMinVisibleScale(0.0);
    style.setLabelMaxVisibleScale(0.0);
    return style;
}

QString shapeTypeName(GisShapeType type)
{
    switch (type)
    {
        case GisShapeType::Point:
        case GisShapeType::PointZ:
            return QStringLiteral("Point");
        case GisShapeType::Polyline:
        case GisShapeType::PolylineZ:
            return QStringLiteral("Polyline");
        case GisShapeType::Polygon:
        case GisShapeType::PolygonZ:
            return QStringLiteral("Polygon");
        case GisShapeType::MultiPoint:
        case GisShapeType::MultiPointZ:
            return QStringLiteral("MultiPoint");
        default:
            return QStringLiteral("Unknown");
    }
}

QString bestDisplayName(const FeatureHitTestResult& hit)
{
    const QHash<QString, QVariant> attributes = hit.attributes();
    static const QStringList preferredKeys = {
        QStringLiteral("NAME"),
        QStringLiteral("Name"),
        QStringLiteral("STATE"),
        QStringLiteral("STATE_NAME"),
        QStringLiteral("COUNTRY"),
        QStringLiteral("ADMIN")
    };

    for (const QString& key : preferredKeys)
    {
        const QVariant value = attributes.value(key);
        if (value.isValid() && !value.toString().trimmed().isEmpty())
            return value.toString();
    }

    return QStringLiteral("Feature %1").arg(hit.featureId);
}

void showSelectedFeatures(QTableWidget& table, const QVector<FeatureHitTestResult>& selected)
{
    table.setRowCount(selected.size());

    for (int row = 0; row < selected.size(); ++row)
    {
        const FeatureHitTestResult& hit = selected[row];
        table.setItem(row, 0, new QTableWidgetItem(QString::number(row + 1)));
        table.setItem(row, 1, new QTableWidgetItem(hit.layer != nullptr ? hit.layer->name() : QStringLiteral("-")));
        table.setItem(row, 2, new QTableWidgetItem(QString::number(hit.featureId)));
        table.setItem(row, 3, new QTableWidgetItem(hit.shape != nullptr ? shapeTypeName(hit.shape->shapeType()) : QStringLiteral("-")));
        table.setItem(row, 4, new QTableWidgetItem(bestDisplayName(hit)));
    }
}

void updateStatusLabel(QLabel& label, const GisViewer& viewer)
{
    label.setText(QStringLiteral("selectedFeatureCount: %1 | Clear button = clearSelectedFeatures")
        .arg(viewer.selectedFeatureCount()));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SelectClear"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico")));

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("SelectClear"));
    window.statusBar()->showMessage(QStringLiteral("Click features to build a selection, then clear it."));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Info);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QAction* selectAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/select.svg")), QStringLiteral("Select"));
    selectAction->setCheckable(true);
    selectAction->setChecked(true);
    QAction* panAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/pan.svg")), QStringLiteral("Pan"));
    panAction->setCheckable(true);
    QAction* clearAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/delete.svg")), QStringLiteral("clearSelectedFeatures"));
    QAction* fullExtentAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/full-extent.svg")), QStringLiteral("Full Extent"));

    auto* stateLabel = new QLabel(&window);
    stateLabel->setContentsMargins(12, 0, 12, 0);
    toolbar->addWidget(stateLabel);

    auto* selectedTable = new QTableWidget(&window);
    selectedTable->setColumnCount(5);
    selectedTable->setHorizontalHeaderLabels({
        QStringLiteral("#"),
        QStringLiteral("Layer"),
        QStringLiteral("Feature ID"),
        QStringLiteral("Type"),
        QStringLiteral("Display")
    });
    selectedTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    selectedTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    selectedTable->setSelectionMode(QAbstractItemView::SingleSelection);
    selectedTable->horizontalHeader()->setStretchLastSection(true);
    selectedTable->verticalHeader()->setVisible(false);

    auto* selectedDock = new QDockWidget(QStringLiteral("Selection set"), &window);
    selectedDock->setWidget(selectedTable);
    window.addDockWidget(Qt::RightDockWidgetArea, selectedDock);

    const QString worldPath = ensureSampleFile(
        QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/world_4326.zip")),
        QStringLiteral("world_4326.zip"), QStringLiteral("world_4326"), QStringLiteral("world_4326.shp"), &window);
    const QString statesPath = ensureSampleFile(
        QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/usa_states.zip")),
        QStringLiteral("usa_states.zip"), QStringLiteral("usa_states"), QStringLiteral("usa_states.shp"), &window);
    const QString citiesPath = ensureSampleFile(
        QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/cities_4326.zip")),
        QStringLiteral("cities_4326.zip"), QStringLiteral("cities_4326"), QStringLiteral("cities_4326.shp"), &window);

    if (worldPath.isEmpty() || statesPath.isEmpty() || citiesPath.isEmpty())
        return 1;
    if (!loadLayer(*viewer, worldPath, QStringLiteral("World"), worldStyle()))
        return 1;
    if (!loadLayer(*viewer, statesPath, QStringLiteral("USA States"), stateStyle()))
        return 1;
    if (!loadLayer(*viewer, citiesPath, QStringLiteral("Cities"), cityStyle()))
        return 1;

    updateStatusLabel(*stateLabel, *viewer);

    QObject::connect(selectAction, &QAction::triggered, viewer, [&](bool checked) {
        if (checked)
        {
            panAction->setChecked(false);
            viewer->setActiveTool(GisViewerTool::Info);
            window.statusBar()->showMessage(QStringLiteral("Click features to add them to the selection."));
        }
        else if (viewer->activeTool() == GisViewerTool::Info)
        {
            selectAction->setChecked(true);
        }
    });

    QObject::connect(panAction, &QAction::triggered, viewer, [&](bool checked) {
        if (checked)
        {
            selectAction->setChecked(false);
            viewer->setActiveTool(GisViewerTool::Pan);
            window.statusBar()->showMessage(QStringLiteral("Pan mode."));
        }
        else if (viewer->activeTool() == GisViewerTool::Pan)
        {
            panAction->setChecked(true);
        }
    });

    QObject::connect(clearAction, &QAction::triggered, viewer, [&] {
        viewer->clearSelectedFeatures();
        showSelectedFeatures(*selectedTable, viewer->selectedFeatures());
        updateStatusLabel(*stateLabel, *viewer);
        window.statusBar()->showMessage(QStringLiteral("Selection cleared."));
    });

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);

    QObject::connect(viewer, &GisViewer::selectionChanged, viewer, [&](int) {
        showSelectedFeatures(*selectedTable, viewer->selectedFeatures());
        updateStatusLabel(*stateLabel, *viewer);
    });

    QObject::connect(viewer, &GisViewer::mapClicked, viewer, [&](
        GisViewerTool tool,
        const QPointF& screenPoint,
        const GisShapePoint&,
        Qt::KeyboardModifiers modifiers) {
        if (tool != GisViewerTool::Info)
            return;

        const FeatureHitTestResult hit = viewer->hitTestTopFeatureAt(screenPoint, 8);
        if (!hit.isValid())
        {
            window.statusBar()->showMessage(QStringLiteral("No feature hit."));
            return;
        }

        if (modifiers.testFlag(Qt::ControlModifier))
        {
            viewer->clearSelectedFeatures();
            window.statusBar()->showMessage(QStringLiteral("Ctrl+Click: clearSelectedFeatures(), selectedFeatureCount=%1")
                .arg(viewer->selectedFeatureCount()));
        }
        else
        {
            viewer->addSelectedFeature(hit);
            window.statusBar()->showMessage(QStringLiteral("Added %1 feature %2, selectedFeatureCount=%3")
                .arg(hit.layer != nullptr ? hit.layer->name() : QStringLiteral("-"))
                .arg(hit.featureId)
                .arg(viewer->selectedFeatureCount()));
        }
    });

    window.show();
    viewer->setViewExtent(GisExtent(-130.0, 22.0, -65.0, 55.0));

    return app.exec();
}
