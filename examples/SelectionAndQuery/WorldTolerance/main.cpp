#include <algorithm>

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
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
            QStringLiteral("WorldTolerance"),
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
    style.setFillOpacity(155);
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

void setAttributeRow(QTableWidget& table, int row, const QString& name, const QString& value)
{
    table.setItem(row, 0, new QTableWidgetItem(name));
    table.setItem(row, 1, new QTableWidgetItem(value));
}

void clearAttributes(QTableWidget& table, const QString& message)
{
    table.setRowCount(1);
    setAttributeRow(table, 0, QStringLiteral("Info"), message);
}

void showAttributes(QTableWidget& table, const FeatureHitTestResult& hit)
{
    const QHash<QString, QVariant> attributes = hit.attributes();
    QStringList keys = attributes.keys();
    std::sort(keys.begin(), keys.end(), [](const QString& left, const QString& right) {
        return QString::localeAwareCompare(left, right) < 0;
    });

    table.setRowCount(5 + keys.size());
    setAttributeRow(table, 0, QStringLiteral("Layer"), hit.layer != nullptr ? hit.layer->name() : QStringLiteral("-"));
    setAttributeRow(table, 1, QStringLiteral("Layer index"), QString::number(hit.layerIndex));
    setAttributeRow(table, 2, QStringLiteral("Feature id"), QString::number(hit.featureId));
    setAttributeRow(table, 3, QStringLiteral("Shape type"), hit.shape != nullptr ? shapeTypeName(hit.shape->shapeType()) : QStringLiteral("-"));
    setAttributeRow(table, 4, QStringLiteral("Extent"), hit.shape != nullptr ? extentText(hit.shape->extent()) : QStringLiteral("-"));

    int row = 5;
    for (const QString& key : keys)
    {
        const QVariant value = attributes.value(key);
        setAttributeRow(table, row++, key, value.isValid() && !value.isNull() ? value.toString() : QStringLiteral("<null>"));
    }
}

void showHits(QTableWidget& table, const QVector<FeatureHitTestResult>& hits)
{
    table.setRowCount(hits.size());
    for (int row = 0; row < hits.size(); ++row)
    {
        const FeatureHitTestResult& hit = hits[row];
        table.setItem(row, 0, new QTableWidgetItem(QString::number(row + 1)));
        table.setItem(row, 1, new QTableWidgetItem(hit.layer != nullptr ? hit.layer->name() : QStringLiteral("-")));
        table.setItem(row, 2, new QTableWidgetItem(QString::number(hit.featureId)));
        table.setItem(row, 3, new QTableWidgetItem(hit.shape != nullptr ? shapeTypeName(hit.shape->shapeType()) : QStringLiteral("-")));
        table.setItem(row, 4, new QTableWidgetItem(bestDisplayName(hit)));
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("WorldTolerance"));
    window.statusBar()->showMessage(QStringLiteral("Click the map to call hitTestFeatures(worldPoint, worldTolerance)."));

    auto* viewer = new GisViewer(&window);
    viewer->setActiveTool(GisViewerTool::Info);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    window.addToolBar(toolbar);

    QAction* identifyAction = toolbar->addAction(sampleIcon(QStringLiteral("Identify.svg")), QStringLiteral("World Tolerance Hit Test"));
    identifyAction->setCheckable(true);
    identifyAction->setChecked(true);
    QAction* panAction = toolbar->addAction(sampleIcon(QStringLiteral("Pan.svg")), QStringLiteral("Pan"));
    panAction->setCheckable(true);
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));

    auto* toleranceLabel = new QLabel(QStringLiteral("World tolerance:"), &window);
    toleranceLabel->setContentsMargins(12, 0, 4, 0);
    toolbar->addWidget(toleranceLabel);

    auto* toleranceSpin = new QDoubleSpinBox(&window);
    toleranceSpin->setDecimals(2);
    toleranceSpin->setRange(0.0, 10.0);
    toleranceSpin->setSingleStep(0.25);
    toleranceSpin->setValue(1.00);
    toleranceSpin->setSuffix(QStringLiteral(" deg"));
    toleranceSpin->setToolTip(QStringLiteral("World coordinate tolerance passed to hitTestFeatures(worldPoint, worldTolerance)."));
    toolbar->addWidget(toleranceSpin);

    auto* stateLabel = new QLabel(QStringLiteral("API: hitTestFeatures(worldPoint, worldTolerance)"), &window);
    stateLabel->setContentsMargins(12, 0, 12, 0);
    toolbar->addWidget(stateLabel);

    auto* hitsTable = new QTableWidget(&window);
    hitsTable->setColumnCount(5);
    hitsTable->setHorizontalHeaderLabels({
        QStringLiteral("#"),
        QStringLiteral("Layer"),
        QStringLiteral("Feature ID"),
        QStringLiteral("Type"),
        QStringLiteral("Display")
    });
    hitsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    hitsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    hitsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    hitsTable->horizontalHeader()->setStretchLastSection(true);
    hitsTable->verticalHeader()->setVisible(false);

    auto* hitsDock = new QDockWidget(QStringLiteral("Features within world tolerance"), &window);
    hitsDock->setWidget(hitsTable);
    window.addDockWidget(Qt::RightDockWidgetArea, hitsDock);

    auto* attributesTable = new QTableWidget(&window);
    attributesTable->setColumnCount(2);
    attributesTable->setHorizontalHeaderLabels({ QStringLiteral("Property / Field"), QStringLiteral("Value") });
    attributesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    attributesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    attributesTable->horizontalHeader()->setStretchLastSection(true);
    attributesTable->verticalHeader()->setVisible(false);
    clearAttributes(*attributesTable, QStringLiteral("Click the map to search with world tolerance."));

    auto* attributesDock = new QDockWidget(QStringLiteral("Selected hit details"), &window);
    attributesDock->setWidget(attributesTable);
    window.addDockWidget(Qt::BottomDockWidgetArea, attributesDock);

    QVector<FeatureHitTestResult> currentHits;

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
            window.statusBar()->showMessage(QStringLiteral("Click a world point to search using the selected world tolerance."));
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

    QObject::connect(hitsTable, &QTableWidget::currentCellChanged, viewer, [&](int currentRow, int, int, int) {
        if (currentRow < 0 || currentRow >= currentHits.size())
            return;

        const FeatureHitTestResult& hit = currentHits[currentRow];
        viewer->setSelectedFeature(hit);
        showAttributes(*attributesTable, hit);
        window.statusBar()->showMessage(QStringLiteral("Selected hit %1/%2: %3 feature %4")
            .arg(currentRow + 1)
            .arg(currentHits.size())
            .arg(hit.layer != nullptr ? hit.layer->name() : QStringLiteral("-"))
            .arg(hit.featureId));
    });

    QObject::connect(viewer, &GisViewer::mapClicked, viewer, [&](
        GisViewerTool tool,
        const QPointF&,
        const GisShapePoint& worldPoint,
        Qt::KeyboardModifiers) {
        if (tool != GisViewerTool::Info)
            return;

        const double worldTolerance = toleranceSpin->value();
        currentHits = viewer->hitTestFeatures(worldPoint, worldTolerance);
        showHits(*hitsTable, currentHits);

        if (currentHits.isEmpty())
        {
            viewer->clearSelectedFeatures();
            clearAttributes(*attributesTable, QStringLiteral("No feature inside world tolerance."));
            window.statusBar()->showMessage(QStringLiteral("No feature hit at %1, %2 with tolerance %3 degrees.")
                .arg(worldPoint.x(), 0, 'f', 6)
                .arg(worldPoint.y(), 0, 'f', 6)
                .arg(worldTolerance, 0, 'f', 2));
            return;
        }

        hitsTable->selectRow(0);
        viewer->setSelectedFeature(currentHits.first());
        showAttributes(*attributesTable, currentHits.first());
        window.statusBar()->showMessage(QStringLiteral("%1 feature hit(s) at %2, %3 with tolerance %4 degrees.")
            .arg(currentHits.size())
            .arg(worldPoint.x(), 0, 'f', 6)
            .arg(worldPoint.y(), 0, 'f', 6)
            .arg(worldTolerance, 0, 'f', 2));
    });

    window.show();
    viewer->setViewExtent(GisExtent(-130.0, 22.0, -65.0, 55.0));

    return app.exec();
}
