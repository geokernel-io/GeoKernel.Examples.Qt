#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QStatusBar>
#include <QStringList>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <memory>

#include "Viewer/GisViewer.h"
#include "FeatureSources/GdalShapefileFeatureSource.h"
#include "CoordinateSystems/Defs/GeographicCoordinateSystem.h"
#include "CoordinateSystems/Defs/KnownCoordinateSystems.h"
#include "Layers/GisLayerVector.h"
#include "Shapes/GisShape.h"
#include "Shapes/GisShapePoint.h"
#include "Shapes/GisShapePolygon.h"
#include "Shapes/GisShapePolyline.h"
#include "Symbology/GisRuleBasedSymbolRenderer.h"

#include "Helpers.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Viewer::FeatureSources;
using namespace GeoKernel::Core::CoordinateSystems::Defs;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Core::Symbology;

constexpr int ShapeRowRole = Qt::UserRole + 1;

std::unique_ptr<GisRuleBasedSymbolRenderer> createZoneRenderer(QHash<QString, GisLayerStyle>& zoneStyles)
{
    auto renderer = std::make_unique<GisRuleBasedSymbolRenderer>();

    auto makeStyle = [](const QString& fillColor, const QString& lineColor)
    {
        GisLayerStyle style;
        QColor fill(fillColor);
        fill.setAlpha(170);
        QColor line(lineColor);
        line.setAlpha(235);

        style.setFillColor(fill.name(QColor::HexArgb));
        style.setLineColor(line.name(QColor::HexArgb));
        style.setLineWidth(1.2f);
        return style;
    };

    const GisLayerStyle defaultStyle = makeStyle(QStringLiteral("#E5E7EB"), QStringLiteral("#6B7280"));
    renderer->setDefaultStyle(defaultStyle);

    struct ZoneDefinition
    {
        const char* name;
        const char* fillColor;
        const char* lineColor;
    };

    const ZoneDefinition zones[] = {
        { "Residential", "#F5DFA1", "#A16207" },
        { "Commercial", "#9DD7F5", "#0369A1" },
        { "Industrial", "#C4B5FD", "#6D28D9" },
        { "Park", "#9AD9A8", "#15803D" },
        { "Mixed", "#FDBA9A", "#C2410C" }
    };

    zoneStyles.clear();
    for (const ZoneDefinition& zone : zones)
    {
        const QString zoneName = QString::fromLatin1(zone.name);
        const GisLayerStyle style = makeStyle(QString::fromLatin1(zone.fillColor), QString::fromLatin1(zone.lineColor));

        GisSymbolRule rule;
        rule.fieldName = QStringLiteral("zone");
        rule.op = GisSymbolRuleOperator::Equals;
        rule.value = zoneName;
        rule.label = zoneName;
        rule.style = style;
        renderer->addRule(rule);
        zoneStyles.insert(zoneName, style);
    }

    return renderer;
}

QIcon styleIcon(const GisLayerStyle& style)
{
    QPixmap pixmap(46, 22);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(style.lineColor()), 2.0));
    painter.setBrush(QColor(style.fillColor()));
    painter.drawRect(QRectF(7.0, 4.0, 32.0, 14.0));
    return QIcon(pixmap);
}

QString featureName(const GisShape& shape, int fallbackIndex)
{
    const QStringList nameFields = {
        QStringLiteral("name"),
        QStringLiteral("NAME"),
        QStringLiteral("Name"),
        QStringLiteral("COUNTY"),
        QStringLiteral("COUNTY_NAME"),
        QStringLiteral("County"),
        QStringLiteral("county")
    };

    for (const QString& field : nameFields)
    {
        const QString value = shape.attributes().value(field).toString().trimmed();
        if (!value.isEmpty())
            return value;
    }

    return QStringLiteral("Feature %1").arg(fallbackIndex + 1);
}

QVector<GisShapePoint> featureGeometryPart(const GisFeatureGeometry& geometry, int partIndex)
{
    QVector<GisShapePoint> part;
    if (geometry.points.isEmpty())
        return part;

    const int pointCount = geometry.points.size();
    const int start = geometry.partStarts.isEmpty()
        ? 0
        : geometry.partStarts.value(partIndex, 0);
    const int end = geometry.partStarts.isEmpty() || partIndex >= geometry.partStarts.size() - 1
        ? pointCount
        : geometry.partStarts[partIndex + 1];

    const int first = std::clamp(start, 0, pointCount);
    const int last = std::clamp(end, first, pointCount);
    for (int i = first; i < last; ++i)
        part.append(geometry.points[i]);

    return part;
}

std::unique_ptr<GisShape> shapeFromGeometry(const GisFeatureGeometry& geometry)
{
    if (geometry.points.isEmpty())
        return nullptr;

    if (geometry.shapeType == GisShapeType::Point || geometry.shapeType == GisShapeType::MultiPoint)
        return std::make_unique<GisShapePoint>(geometry.points.first().x(), geometry.points.first().y());

    const int partCount = geometry.partStarts.isEmpty()
        ? 1
        : geometry.partStarts.size();

    if (geometry.shapeType == GisShapeType::Polyline || geometry.shapeType == GisShapeType::PolylineZ)
    {
        auto shape = std::make_unique<GisShapePolyline>();
        for (int partIndex = 0; partIndex < partCount; ++partIndex)
        {
            QVector<GisShapePoint> part = featureGeometryPart(geometry, partIndex);
            if (part.size() >= 2)
                shape->parts().append(std::move(part));
        }

        if (shape->isEmpty())
            return nullptr;

        return shape;
    }

    if (geometry.shapeType == GisShapeType::Polygon || geometry.shapeType == GisShapeType::PolygonZ)
    {
        auto shape = std::make_unique<GisShapePolygon>();
        for (int partIndex = 0; partIndex < partCount; ++partIndex)
        {
            QVector<GisShapePoint> part = featureGeometryPart(geometry, partIndex);
            if (part.size() >= 3)
                shape->parts().append(std::move(part));
        }

        if (shape->isEmpty())
            return nullptr;

        return shape;
    }

    return nullptr;
}

std::unique_ptr<GisLayerVector> loadCountiesAsMemoryLayer(const QString& path, QString* errorMessage)
{
    auto source = std::make_unique<GdalShapefileFeatureSource>(path);
    if (!source->open())
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("Could not open california.shp.");
        return nullptr;
    }

    auto layer = GisLayerVector::createInMemory(
        QStringLiteral("California counties - style from zone attribute"),
        source->shapeType(),
        source->extent());
    layer->setCoordinateSystem(std::make_shared<GeographicCoordinateSystem>(KnownCoordinateSystems::wgs84()));

    const QStringList zones = {
        QStringLiteral("Residential"),
        QStringLiteral("Commercial"),
        QStringLiteral("Industrial"),
        QStringLiteral("Park"),
        QStringLiteral("Mixed")
    };

    int added = 0;
    for (int row = 0; row < source->featureCount(); ++row)
    {
        GisFeatureGeometry geometry;
        if (!source->readGeometry(row, geometry) || geometry.points.isEmpty())
            continue;

        auto shape = shapeFromGeometry(geometry);
        if (!shape)
            continue;

        QHash<QString, QVariant> attributes = source->attributes(row);
        for (auto it = attributes.constBegin(); it != attributes.constEnd(); ++it)
            shape->attributes().insert(it.key(), it.value());

        shape->attributes().insert(QStringLiteral("name"), featureName(*shape, row));
        shape->attributes().insert(QStringLiteral("zone"), zones[added % zones.size()]);

        if (layer->addShape(std::move(shape)))
            ++added;
    }

    if (added == 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("No polygon features could be loaded.");
        return nullptr;
    }

    layer->open();
    return layer;
}

void refreshFeatureList(QListWidget& list, const GisLayerVector& layer, const QHash<QString, GisLayerStyle>& zoneStyles)
{
    const int currentRow = list.currentRow();

    list.clear();

    const QVector<GisShape*> shapes = layer.items();
    for (int row = 0; row < shapes.size(); ++row)
    {
        GisShape* shape = shapes[row];
        if (shape == nullptr)
            continue;

        const QString name = shape->attributes().value(QStringLiteral("name")).toString();
        const QString zone = shape->attributes().value(QStringLiteral("zone")).toString();
        auto* item = new QListWidgetItem(
            styleIcon(zoneStyles.value(zone, layer.style())),
            QStringLiteral("%1 - %2").arg(name, zone));
        item->setData(ShapeRowRole, row);
        list.addItem(item);
    }

    if (currentRow >= 0 && currentRow < list.count())
        list.setCurrentRow(currentRow);
    else if (list.count() > 0)
        list.setCurrentRow(0);
}

GisShape* selectedListShape(const QListWidget& list, const GisLayerVector& layer)
{
    if (list.currentItem() == nullptr)
        return nullptr;

    const int shapeRow = list.currentItem()->data(ShapeRowRole).toInt();
    const QVector<GisShape*> shapes = layer.items();
    return shapeRow >= 0 && shapeRow < shapes.size() ? shapes[shapeRow] : nullptr;
}

void selectFeatureListShape(QListWidget& list, const GisLayerVector& layer, const GisShape& selectedShape)
{
    const QVector<GisShape*> shapes = layer.items();
    for (int row = 0; row < shapes.size(); ++row)
    {
        if (shapes[row] != nullptr && shapes[row]->id() == selectedShape.id())
        {
            list.setCurrentRow(row);
            return;
        }
    }
}

void setComboToSelectedZone(QComboBox& combo, QListWidget& list, const GisLayerVector& layer)
{
    const GisShape* shape = selectedListShape(list, layer);
    if (shape == nullptr)
        return;

    const QString zone = shape->attributes().value(QStringLiteral("zone")).toString();
    const int index = combo.findText(zone);
    if (index >= 0)
        combo.setCurrentIndex(index);
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1100, 760);
    window.setWindowTitle(QStringLiteral("StylePerFeature"));

    auto* central = new QWidget(&window);
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* panel = new QWidget(central);
    panel->setFixedWidth(250);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(8, 8, 8, 8);

    auto* featureList = new QListWidget(panel);
    auto* zoneCombo = new QComboBox(panel);
    zoneCombo->addItems({
        QStringLiteral("Residential"),
        QStringLiteral("Commercial"),
        QStringLiteral("Industrial"),
        QStringLiteral("Park"),
        QStringLiteral("Mixed")
    });

    auto* applyButton = new QPushButton(QStringLiteral("Apply Feature Style"), panel);
    zoneCombo->setEnabled(false);
    applyButton->setEnabled(false);

    auto* controls = new QGroupBox(QStringLiteral("Selected Feature"), panel);
    auto* controlsLayout = new QVBoxLayout(controls);
    controlsLayout->addWidget(new QLabel(QStringLiteral("Zone attribute"), controls));
    controlsLayout->addWidget(zoneCombo);
    controlsLayout->addWidget(applyButton);

    panelLayout->addWidget(new QLabel(QStringLiteral("Feature attributes"), panel));
    panelLayout->addWidget(featureList, 1);
    panelLayout->addWidget(controls);

    auto* viewer = new GisViewer(central);
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(panel);
    layout->addWidget(viewer, 1);
    window.setCentralWidget(central);

    window.show();

    QMetaObject::invokeMethod(&window, [&window, viewer, featureList, zoneCombo, applyButton]
    {
        featureList->clear();
        featureList->addItem(QStringLiteral("Preparing California sample data..."));

        const QString path = ensureSampleFile(
            QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/california.zip")),
            QStringLiteral("california.zip"),
            QStringLiteral("california"),
            QStringLiteral("california.shp"),
            &window);
        if (path.isEmpty())
        {
            featureList->clear();
            featureList->addItem(QStringLiteral("Sample data could not be prepared."));
            window.statusBar()->showMessage(QStringLiteral("Sample data could not be prepared."));
            return;
        }

        QString errorMessage;
        auto countiesLayerOwner = loadCountiesAsMemoryLayer(path, &errorMessage);
        if (!countiesLayerOwner)
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("StylePerFeature"),
                errorMessage.isEmpty() ? QStringLiteral("california.shp could not be loaded.") : errorMessage);
            featureList->clear();
            featureList->addItem(QStringLiteral("Layer could not be loaded."));
            window.statusBar()->showMessage(QStringLiteral("Layer could not be loaded."));
            return;
        }

        viewer->addOpenStreetMapLayer();
        auto* countiesLayer = countiesLayerOwner.get();

        GisLayerStyle defaultStyle;
        QColor fill(QStringLiteral("#E5E7EB"));
        fill.setAlpha(170);
        QColor line(QStringLiteral("#6B7280"));
        line.setAlpha(235);
        defaultStyle.setFillColor(fill.name(QColor::HexArgb));
        defaultStyle.setLineColor(line.name(QColor::HexArgb));
        defaultStyle.setLineWidth(1.2f);

        QHash<QString, GisLayerStyle> zoneStyles;
        countiesLayer->style() = defaultStyle;
        countiesLayer->setSymbolRenderer(createZoneRenderer(zoneStyles));
        viewer->addLayer(countiesLayerOwner);

        refreshFeatureList(*featureList, *countiesLayer, zoneStyles);
        setComboToSelectedZone(*zoneCombo, *featureList, *countiesLayer);
        zoneCombo->setEnabled(true);
        applyButton->setEnabled(true);

        QObject::connect(featureList, &QListWidget::currentItemChanged, &window, [zoneCombo, featureList, countiesLayer]
        {
            setComboToSelectedZone(*zoneCombo, *featureList, *countiesLayer);
        });

        QObject::connect(viewer, &GisViewer::mapClicked, &window, [viewer, featureList, countiesLayer](
            GisViewerTool,
            const QPointF& screenPoint,
            const GisShapePoint&,
            Qt::KeyboardModifiers)
        {
            const FeatureHitTestResult hit = viewer->hitTestTopFeatureAt(screenPoint, 8);
            if (!hit.isValid() || hit.layer != countiesLayer || hit.shape == nullptr)
                return;

            viewer->setSelectedFeature(hit);
            selectFeatureListShape(*featureList, *countiesLayer, *hit.shape);
        });

        QObject::connect(applyButton, &QPushButton::clicked, &window, [viewer, featureList, zoneCombo, countiesLayer, zoneStyles, &window]
        {
            GisShape* shape = selectedListShape(*featureList, *countiesLayer);
            if (shape == nullptr)
                return;

            shape->attributes().insert(QStringLiteral("zone"), zoneCombo->currentText());
            refreshFeatureList(*featureList, *countiesLayer, zoneStyles);
            viewer->invalidateRenderCache(true, true);
            viewer->update();
            window.statusBar()->showMessage(
                QStringLiteral("Feature %1 style updated from zone=%2")
                    .arg(shape->attributes().value(QStringLiteral("name")).toString(), zoneCombo->currentText()));
        });

        viewer->refreshLayers();
        viewer->zoomToLayer(0);
        window.statusBar()->showMessage(QStringLiteral("Per-feature style is driven by each shape's zone attribute"));
    });

    return app.exec();
}
