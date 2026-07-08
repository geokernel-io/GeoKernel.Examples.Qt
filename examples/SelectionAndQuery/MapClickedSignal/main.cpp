#include <algorithm>

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
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

#include "FeatureSources/FeatureHitTestResult.h"
#include "Layers/GisLayerStyle.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShape.h"
#include "Shapes/GisShapePoint.h"
#include "Shapes/GisShapeType.h"
#include "Viewer/GisViewer.h"

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Viewer;
using namespace GeoKernel::Viewer::FeatureSources;

QString sampleDataPath(const QString& fileName)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/data/%1").arg(fileName)));
}

bool loadLayer(GisViewer& viewer, const QString& fileName, const QString& displayName, const GisLayerStyle& style)
{
    QString errorMessage;
    if (!viewer.addLayerFromPath(sampleDataPath(fileName), &errorMessage))
    {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("MapClickedSignal"),
            QStringLiteral("Layer could not be loaded:\n%1")
            .arg(errorMessage.isEmpty() ? fileName : errorMessage));
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

QString toolName(GisViewerTool tool)
{
    switch (tool)
    {
        case GisViewerTool::Pan:
            return QStringLiteral("Pan");
        case GisViewerTool::ZoomBox:
            return QStringLiteral("ZoomBox");
        case GisViewerTool::Info:
            return QStringLiteral("Info");
        case GisViewerTool::Select:
            return QStringLiteral("Select");
        case GisViewerTool::AddPoint:
            return QStringLiteral("AddPoint");
        case GisViewerTool::AddPolyline:
            return QStringLiteral("AddPolyline");
        case GisViewerTool::AddPolygon:
            return QStringLiteral("AddPolygon");
        case GisViewerTool::MoveFeature:
            return QStringLiteral("MoveFeature");
        case GisViewerTool::Route:
            return QStringLiteral("Route");
        case GisViewerTool::EditVertices:
            return QStringLiteral("EditVertices");
        default:
            return QStringLiteral("Unknown");
    }
}

QString modifiersText(Qt::KeyboardModifiers modifiers)
{
    QStringList parts;
    if (modifiers.testFlag(Qt::ShiftModifier))
        parts << QStringLiteral("Shift");
    if (modifiers.testFlag(Qt::ControlModifier))
        parts << QStringLiteral("Ctrl");
    if (modifiers.testFlag(Qt::AltModifier))
        parts << QStringLiteral("Alt");
    if (modifiers.testFlag(Qt::MetaModifier))
        parts << QStringLiteral("Meta");
    return parts.isEmpty() ? QStringLiteral("-") : parts.join(QStringLiteral("+"));
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

QString pointText(const QPointF& point)
{
    return QStringLiteral("(%1, %2)")
        .arg(point.x(), 0, 'f', 1)
        .arg(point.y(), 0, 'f', 1);
}

QString pointText(const GisShapePoint& point)
{
    return QStringLiteral("(%1, %2)")
        .arg(point.x(), 0, 'f', 6)
        .arg(point.y(), 0, 'f', 6);
}

void appendClickLog(
    QTableWidget& table,
    GisViewerTool tool,
    const QPointF& screenPoint,
    const GisShapePoint& worldPoint,
    Qt::KeyboardModifiers modifiers,
    const FeatureHitTestResult& hit)
{
    const int row = table.rowCount();
    table.insertRow(row);
    table.setItem(row, 0, new QTableWidgetItem(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))));
    table.setItem(row, 1, new QTableWidgetItem(toolName(tool)));
    table.setItem(row, 2, new QTableWidgetItem(pointText(screenPoint)));
    table.setItem(row, 3, new QTableWidgetItem(pointText(worldPoint)));
    table.setItem(row, 4, new QTableWidgetItem(modifiersText(modifiers)));
    table.setItem(row, 5, new QTableWidgetItem(hit.isValid() && hit.layer != nullptr ? hit.layer->name() : QStringLiteral("-")));
    table.setItem(row, 6, new QTableWidgetItem(hit.isValid() ? QString::number(hit.featureId) : QStringLiteral("-")));
    table.setItem(row, 7, new QTableWidgetItem(hit.isValid() && hit.shape != nullptr ? shapeTypeName(hit.shape->shapeType()) : QStringLiteral("-")));
    table.scrollToBottom();
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("MapClickedSignal"));
    window.statusBar()->showMessage(QStringLiteral("Click the map to log mapClicked(tool, screenPoint, worldPoint, modifiers)."));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Info);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QAction* identifyAction = toolbar->addAction(sampleIcon(QStringLiteral("Identify.svg")), QStringLiteral("Info"));
    identifyAction->setCheckable(true);
    identifyAction->setChecked(true);
    QAction* panAction = toolbar->addAction(sampleIcon(QStringLiteral("Pan.svg")), QStringLiteral("Pan"));
    panAction->setCheckable(true);
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));

    auto* stateLabel = new QLabel(QStringLiteral("Signal: mapClicked(tool, screenPoint, worldPoint, modifiers)"), &window);
    stateLabel->setContentsMargins(12, 0, 12, 0);
    toolbar->addWidget(stateLabel);

    auto* logTable = new QTableWidget(&window);
    logTable->setColumnCount(8);
    logTable->setHorizontalHeaderLabels({
        QStringLiteral("Time"),
        QStringLiteral("Tool"),
        QStringLiteral("Screen point"),
        QStringLiteral("World point"),
        QStringLiteral("Modifiers"),
        QStringLiteral("Hit layer"),
        QStringLiteral("Feature ID"),
        QStringLiteral("Shape type")
    });
    logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    logTable->horizontalHeader()->setStretchLastSection(true);
    logTable->verticalHeader()->setVisible(false);

    auto* logDock = new QDockWidget(QStringLiteral("mapClicked signal log"), &window);
    logDock->setWidget(logTable);
    window.addDockWidget(Qt::RightDockWidgetArea, logDock);

    if (!loadLayer(*viewer, QStringLiteral("shapefile/world_4326.shp"), QStringLiteral("World"), worldStyle()))
        return 1;
    if (!loadLayer(*viewer, QStringLiteral("shapefile/usa_states.shp"), QStringLiteral("USA States"), stateStyle()))
        return 1;
    if (!loadLayer(*viewer, QStringLiteral("shapefile/cities_4326.shp"), QStringLiteral("Cities"), cityStyle()))
        return 1;

    QObject::connect(identifyAction, &QAction::triggered, viewer, [&](bool checked) {
        if (checked)
        {
            panAction->setChecked(false);
            viewer->setActiveTool(GisViewerTool::Info);
            window.statusBar()->showMessage(QStringLiteral("Info tool active. Click to emit mapClicked."));
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
        const GisShapePoint& worldPoint,
        Qt::KeyboardModifiers modifiers) {
        const FeatureHitTestResult hit = viewer->hitTestTopFeatureAt(screenPoint, 8);
        appendClickLog(*logTable, tool, screenPoint, worldPoint, modifiers, hit);
        if (hit.isValid())
            viewer->setSelectedFeature(hit);
        else
            viewer->clearSelectedFeatures();

        window.statusBar()->showMessage(QStringLiteral("mapClicked: tool=%1 screen=%2 world=%3 modifiers=%4")
            .arg(toolName(tool))
            .arg(pointText(screenPoint))
            .arg(pointText(worldPoint))
            .arg(modifiersText(modifiers)));
    });

    window.show();
    viewer->setViewExtent(GisExtent(-130.0, 22.0, -65.0, 55.0));

    return app.exec();
}
