#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMainWindow>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QStatusBar>
#include <QStringList>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <memory>

#include "Viewer/GisViewer.h"
#include "Layers/GisLayerVector.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShapePolygon.h"
#include "Symbology/GisRuleBasedSymbolRenderer.h"

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Core::Symbology;

constexpr int ShapeIdRole = Qt::UserRole + 1;

GisLayerStyle parcelStyle(const QString& fillColor, const QString& lineColor)
{
    GisLayerStyle style;
    QColor fill(fillColor);
    fill.setAlpha(170);
    QColor line(lineColor);
    line.setAlpha(235);

    style.setFillColor(fill.name(QColor::HexArgb));
    style.setLineColor(line.name(QColor::HexArgb));
    style.setLineWidth(2.0f);
    return style;
}

GisSymbolRule zoneRule(const QString& zone, const QString& fillColor, const QString& lineColor)
{
    GisSymbolRule rule;
    rule.fieldName = QStringLiteral("zone");
    rule.op = GisSymbolRuleOperator::Equals;
    rule.value = zone;
    rule.label = zone;
    rule.style = parcelStyle(fillColor, lineColor);
    return rule;
}

std::unique_ptr<GisRuleBasedSymbolRenderer> createZoneRenderer()
{
    auto renderer = std::make_unique<GisRuleBasedSymbolRenderer>();
    renderer->setDefaultStyle(parcelStyle(QStringLiteral("#E5E7EB"), QStringLiteral("#6B7280")));

    renderer->addRule(zoneRule(QStringLiteral("Residential"), QStringLiteral("#F5DFA1"), QStringLiteral("#A16207")));
    renderer->addRule(zoneRule(QStringLiteral("Commercial"), QStringLiteral("#9DD7F5"), QStringLiteral("#0369A1")));
    renderer->addRule(zoneRule(QStringLiteral("Industrial"), QStringLiteral("#C4B5FD"), QStringLiteral("#6D28D9")));
    renderer->addRule(zoneRule(QStringLiteral("Park"), QStringLiteral("#9AD9A8"), QStringLiteral("#15803D")));
    renderer->addRule(zoneRule(QStringLiteral("Mixed"), QStringLiteral("#FDBA9A"), QStringLiteral("#C2410C")));

    return renderer;
}

std::unique_ptr<GisShapePolygon> makeParcel(
    const QString& name,
    const QString& zone,
    double xMin,
    double yMin,
    double xMax,
    double yMax)
{
    auto polygon = std::make_unique<GisShapePolygon>();
    polygon->parts().append({
        GisShapePoint(xMin, yMin),
        GisShapePoint(xMax, yMin),
        GisShapePoint(xMax, yMax),
        GisShapePoint(xMin, yMax),
        GisShapePoint(xMin, yMin)
        });
    polygon->attributes().insert(QStringLiteral("name"), name);
    polygon->attributes().insert(QStringLiteral("zone"), zone);
    return polygon;
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

GisLayerStyle styleForZone(const QString& zone)
{
    const auto renderer = createZoneRenderer();
    for (const GisSymbolLegendItem& item : renderer->legendItems())
    {
        if (item.label == zone)
            return item.style;
    }

    return renderer->defaultStyle();
}

void refreshFeatureList(QListWidget& list, const GisLayerVector& layer)
{
    const int currentShapeId = list.currentItem() != nullptr
        ? list.currentItem()->data(ShapeIdRole).toInt()
        : -1;

    list.clear();

    int rowToSelect = -1;
    const QVector<GisShape*> shapes = layer.items();
    for (GisShape* shape : shapes)
    {
        if (shape == nullptr)
            continue;

        const QString name = shape->attributes().value(QStringLiteral("name")).toString();
        const QString zone = shape->attributes().value(QStringLiteral("zone")).toString();
        auto* item = new QListWidgetItem(
            styleIcon(styleForZone(zone)),
            QStringLiteral("%1 - %2").arg(name, zone));
        item->setData(ShapeIdRole, shape->id());
        list.addItem(item);

        if (shape->id() == currentShapeId)
            rowToSelect = list.count() - 1;
    }

    if (rowToSelect >= 0)
        list.setCurrentRow(rowToSelect);
    else if (list.count() > 0)
        list.setCurrentRow(0);
}

void setComboToSelectedZone(QComboBox& combo, QListWidget& list, const GisLayerVector& layer)
{
    if (list.currentItem() == nullptr)
        return;

    const int shapeId = list.currentItem()->data(ShapeIdRole).toInt();
    const GisShape* shape = layer.getShape(shapeId);
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
    auto* controls = new QGroupBox(QStringLiteral("Selected Feature"), panel);
    auto* controlsLayout = new QVBoxLayout(controls);
    controlsLayout->addWidget(new QLabel(QStringLiteral("Zone attribute"), controls));
    controlsLayout->addWidget(zoneCombo);
    controlsLayout->addWidget(applyButton);

    panelLayout->addWidget(new QLabel(QStringLiteral("Feature attributes"), panel));
    panelLayout->addWidget(featureList, 1);
    panelLayout->addWidget(controls);

    auto* viewer = new GisViewer(central);
    viewer->setMapBackgroundColor(QColor(247, 248, 250));
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(panel);
    layout->addWidget(viewer, 1);
    window.setCentralWidget(central);

    auto parcels = std::make_unique<GisLayerVector>();
    parcels->setName(QStringLiteral("Parcels - style from zone attribute"));
    parcels->style() = parcelStyle(QStringLiteral("#E5E7EB"), QStringLiteral("#6B7280"));
    parcels->addShape(makeParcel(QStringLiteral("Parcel A"), QStringLiteral("Residential"), 0.0, 3.0, 3.0, 5.7));
    parcels->addShape(makeParcel(QStringLiteral("Parcel B"), QStringLiteral("Commercial"), 3.4, 3.3, 6.8, 5.4));
    parcels->addShape(makeParcel(QStringLiteral("Parcel C"), QStringLiteral("Industrial"), 7.1, 3.1, 10.4, 5.7));
    parcels->addShape(makeParcel(QStringLiteral("Parcel D"), QStringLiteral("Park"), 1.0, 0.5, 4.8, 2.7));
    parcels->addShape(makeParcel(QStringLiteral("Parcel E"), QStringLiteral("Mixed"), 5.2, 0.7, 9.8, 2.8));
    parcels->open();
    parcels->setSymbolRenderer(createZoneRenderer());

    auto* parcelsLayer = parcels.get();
    viewer->addLayer(parcels);    

    refreshFeatureList(*featureList, *parcelsLayer);
    setComboToSelectedZone(*zoneCombo, *featureList, *parcelsLayer);

    QObject::connect(featureList, &QListWidget::currentItemChanged, [&]() {
        setComboToSelectedZone(*zoneCombo, *featureList, *parcelsLayer);
    });

    QObject::connect(applyButton, &QPushButton::clicked, [&]() {
        if (featureList->currentItem() == nullptr)
            return;

        const int shapeId = featureList->currentItem()->data(ShapeIdRole).toInt();
        GisShape* shape = parcelsLayer->getShape(shapeId);
        if (shape == nullptr)
            return;

        shape->attributes().insert(QStringLiteral("zone"), zoneCombo->currentText());
        refreshFeatureList(*featureList, *parcelsLayer);
        viewer->invalidateRenderCache(true, true);
        viewer->update();
        window.statusBar()->showMessage(
            QStringLiteral("Feature %1 style updated from zone=%2")
                .arg(shape->attributes().value(QStringLiteral("name")).toString(), zoneCombo->currentText()));
    });

    window.statusBar()->showMessage(QStringLiteral("Per-feature style is driven by each shape's zone attribute"));
    window.show();

    viewer->setViewExtent(GisExtent(-0.8, -0.2, 11.2, 6.4));

    return app.exec();
}
