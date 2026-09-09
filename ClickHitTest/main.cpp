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
            QStringLiteral("ClickHitTest"),
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

QString extentText(const GisExtent& extent)
{
    return QStringLiteral("(%1, %2) - (%3, %4)")
        .arg(extent.xMin(), 0, 'f', 6)
        .arg(extent.yMin(), 0, 'f', 6)
        .arg(extent.xMax(), 0, 'f', 6)
        .arg(extent.yMax(), 0, 'f', 6);
}

void setRow(QTableWidget& table, int row, const QString& name, const QString& value)
{
    table.setItem(row, 0, new QTableWidgetItem(name));
    table.setItem(row, 1, new QTableWidgetItem(value));
}

void showEmptyHit(QTableWidget& table)
{
    table.setRowCount(1);
    setRow(table, 0, QStringLiteral("Hit"), QStringLiteral("No feature at clicked point"));
}

void showHit(QTableWidget& table, const FeatureHitTestResult& hit)
{
    const QHash<QString, QVariant> attributes = hit.attributes();
    QStringList keys = attributes.keys();
    std::sort(keys.begin(), keys.end(), [](const QString& left, const QString& right) {
        return QString::localeAwareCompare(left, right) < 0;
    });

    table.setRowCount(5 + keys.size());
    setRow(table, 0, QStringLiteral("Layer"), hit.layer != nullptr ? hit.layer->name() : QStringLiteral("-"));
    setRow(table, 1, QStringLiteral("Layer index"), QString::number(hit.layerIndex));
    setRow(table, 2, QStringLiteral("Feature id"), QString::number(hit.featureId));
    setRow(table, 3, QStringLiteral("Shape type"), hit.shape != nullptr ? shapeTypeName(hit.shape->shapeType()) : QStringLiteral("-"));
    setRow(table, 4, QStringLiteral("Extent"), hit.shape != nullptr ? extentText(hit.shape->extent()) : QStringLiteral("-"));

    int row = 5;
    for (const QString& key : keys)
    {
        const QVariant value = attributes.value(key);
        setRow(table, row++, key, value.isValid() && !value.isNull() ? value.toString() : QStringLiteral("<null>"));
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ClickHitTest"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico")));

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("ClickHitTest"));
    window.statusBar()->showMessage(QStringLiteral("Click the map to run hitTestTopFeatureAt and select the top-most feature."));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Info);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QAction* identifyAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/identify.svg")), QStringLiteral("Click Hit Test"));
    identifyAction->setCheckable(true);
    identifyAction->setChecked(true);
    QAction* panAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/pan.svg")), QStringLiteral("Pan"));
    panAction->setCheckable(true);
    QAction* fullExtentAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/full-extent.svg")), QStringLiteral("Full Extent"));

    auto* stateLabel = new QLabel(QStringLiteral("Tool: hitTestTopFeatureAt"), &window);
    stateLabel->setContentsMargins(12, 0, 12, 0);
    toolbar->addWidget(stateLabel);

    auto* detailsTable = new QTableWidget(&window);
    detailsTable->setColumnCount(2);
    detailsTable->setHorizontalHeaderLabels({ QStringLiteral("Property / Field"), QStringLiteral("Value") });
    detailsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    detailsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    detailsTable->horizontalHeader()->setStretchLastSection(true);
    detailsTable->verticalHeader()->setVisible(false);
    showEmptyHit(*detailsTable);

    auto* detailsDock = new QDockWidget(QStringLiteral("Top feature hit"), &window);
    detailsDock->setWidget(detailsTable);
    window.addDockWidget(Qt::RightDockWidgetArea, detailsDock);

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

    QObject::connect(identifyAction, &QAction::triggered, viewer, [&](bool checked) {
        if (checked)
        {
            panAction->setChecked(false);
            viewer->setActiveTool(GisViewerTool::Info);
            window.statusBar()->showMessage(QStringLiteral("Click a feature to inspect the top-most hit."));
        }
        else if (viewer->activeTool() == GisViewerTool::Info)
        {
            identifyAction->setChecked(true);
        }
    });

    QObject::connect(panAction, &QAction::triggered, viewer, [&](bool checked) {
        if (checked)
        {
            identifyAction->setChecked(false);
            viewer->setActiveTool(GisViewerTool::Pan);
            window.statusBar()->showMessage(QStringLiteral("Pan mode."));
        }
        else if (viewer->activeTool() == GisViewerTool::Pan)
        {
            panAction->setChecked(true);
        }
    });

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, &GisViewer::fullExtent);

    QObject::connect(viewer, &GisViewer::mapClicked, viewer, [&](
        GisViewerTool tool,
        const QPointF& screenPoint,
        const GisShapePoint&,
        Qt::KeyboardModifiers) {
        if (tool != GisViewerTool::Info)
            return;

        const FeatureHitTestResult hit = viewer->hitTestTopFeatureAt(screenPoint, 8);
        if (!hit.isValid())
        {
            viewer->clearSelectedFeatures();
            showEmptyHit(*detailsTable);
            window.statusBar()->showMessage(QStringLiteral("No feature hit."));
            return;
        }

        viewer->setSelectedFeature(hit);
        showHit(*detailsTable, hit);
        window.statusBar()->showMessage(QStringLiteral("Top hit: %1 feature %2")
            .arg(hit.layer != nullptr ? hit.layer->name() : QStringLiteral("-"))
            .arg(hit.featureId));
    });

    window.show();
    viewer->setViewExtent(GisExtent(-130.0, 22.0, -65.0, 55.0));

    return app.exec();
}
