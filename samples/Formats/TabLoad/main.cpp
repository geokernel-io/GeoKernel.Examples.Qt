#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QVariant>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include <exception>
#include <memory>

#include "Layers/Defs/GisAttributeDefinition.h"
#include "Layers/Defs/GisAttributeType.h"
#include "Layers/GisLayerStyle.h"
#include "Vector/Tab/GisLayerTAB.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Layers::Defs;
using namespace GeoKernel::Formats::Vector::Tab;
using namespace GeoKernel::Viewer;

QString dataPath(const QString& relativePath)
{
    const QDir appDir(QApplication::applicationDirPath());
    return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/data/%1").arg(relativePath)));
}

QString attributeTypeName(GisAttributeType type)
{
    switch (type)
    {
        case GisAttributeType::String:
            return QStringLiteral("String");
        case GisAttributeType::Integer:
            return QStringLiteral("Integer");
        case GisAttributeType::Double:
            return QStringLiteral("Double");
        case GisAttributeType::Boolean:
            return QStringLiteral("Boolean");
        case GisAttributeType::DateTime:
            return QStringLiteral("DateTime");
    }

    return QStringLiteral("Unknown");
}

QString shapeTypeName(GisShapeType type)
{
    switch (type)
    {
        case GisShapeType::Point:
            return QStringLiteral("Point");
        case GisShapeType::Polyline:
            return QStringLiteral("Polyline");
        case GisShapeType::Polygon:
            return QStringLiteral("Polygon");
        case GisShapeType::Unknown:
            break;
    }

    return QStringLiteral("Unknown");
}

GisLayerStyle tabStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#D7E5DF"));
    style.setFillOpacity(0.72f);
    style.setLineColor(QStringLiteral("#6D8C86"));
    style.setLineWidth(1.1f);
    style.setPointColor(QStringLiteral("#D95D39"));
    style.setPointSize(7.0f);
    return style;
}

QVector<QHash<QString, QVariant>> sampleAttributeRows(const GisLayerVector& layer, int maxRows)
{
    QVector<QHash<QString, QVariant>> rows;
    rows.reserve(maxRows);

    for (int row = 0; row < maxRows; ++row)
    {
        const QHash<QString, QVariant> attributes = layer.attributesForRow(row);
        if (attributes.isEmpty())
            break;

        rows.append(attributes);
    }

    return rows;
}

void fillSchemaTable(QTableWidget& table, const GisLayerVector& layer)
{
    const QVector<GisAttributeDefinition>& definitions = layer.attributeDefinitions();
    table.clear();
    table.setColumnCount(4);
    table.setRowCount(definitions.size());
    table.setHorizontalHeaderLabels({
        QStringLiteral("Field"),
        QStringLiteral("Type"),
        QStringLiteral("Length"),
        QStringLiteral("Decimals")
    });

    for (int row = 0; row < definitions.size(); ++row)
    {
        const GisAttributeDefinition& definition = definitions[row];
        table.setItem(row, 0, new QTableWidgetItem(definition.name));
        table.setItem(row, 1, new QTableWidgetItem(attributeTypeName(definition.type)));
        table.setItem(row, 2, new QTableWidgetItem(QString::number(definition.length)));
        table.setItem(row, 3, new QTableWidgetItem(QString::number(definition.decimalCount)));
    }

    table.resizeColumnsToContents();
    table.horizontalHeader()->setStretchLastSection(true);
}

void fillAttributeTable(QTableWidget& table, const GisLayerVector& layer, int maxRows)
{
    const QVector<GisAttributeDefinition>& definitions = layer.attributeDefinitions();
    const QVector<QHash<QString, QVariant>> rows = sampleAttributeRows(layer, maxRows);

    QStringList headers;
    headers << QStringLiteral("#");
    for (const GisAttributeDefinition& definition : definitions)
        headers << definition.name;

    table.clear();
    table.setColumnCount(headers.size());
    table.setRowCount(rows.size());
    table.setHorizontalHeaderLabels(headers);

    for (int row = 0; row < rows.size(); ++row)
    {
        table.setItem(row, 0, new QTableWidgetItem(QString::number(row)));
        const QHash<QString, QVariant>& attributes = rows[row];

        for (int column = 0; column < definitions.size(); ++column)
        {
            const QString& fieldName = definitions[column].name;
            table.setItem(row, column + 1, new QTableWidgetItem(attributes.value(fieldName).toString()));
        }
    }

    if (rows.isEmpty())
    {
        table.setRowCount(1);
        table.setItem(0, 0, new QTableWidgetItem(QStringLiteral("No attribute rows returned.")));
    }

    table.resizeColumnsToContents();
    table.horizontalHeader()->setStretchLastSection(false);
}

QString sidecarLine(const QString& label, const QString& path)
{
    const QFileInfo info(path);
    return QStringLiteral("%1: %2 bytes (%3)")
        .arg(label)
        .arg(info.exists() ? info.size() : 0)
        .arg(info.exists() ? QStringLiteral("exists") : QStringLiteral("missing"));
}

QString detailsText(const QString& path, const GisLayerTAB& layer)
{
    QStringList lines;
    lines << QStringLiteral("TabLoad sample");
    lines << QStringLiteral("");
    lines << QStringLiteral("API");
    lines << QStringLiteral("GisLayerTAB(path)");
    lines << QStringLiteral("layer.open()");
    lines << QStringLiteral("layer.attributeDefinitions()");
    lines << QStringLiteral("layer.attributesForRow(row)");
    lines << QStringLiteral("");
    lines << QStringLiteral("Loaded MapInfo TAB");
    lines << QDir::toNativeSeparators(path);
    lines << QStringLiteral("");
    lines << QStringLiteral("Layer");
    lines << QStringLiteral("Name: %1").arg(layer.name());
    lines << QStringLiteral("Shape type: %1").arg(shapeTypeName(layer.shapeType()));
    lines << QStringLiteral("Memory shape count: %1").arg(layer.count());
    lines << QStringLiteral("Provider-backed TAB files can render features while memory shape count remains 0.");
    lines << QStringLiteral("Field count: %1").arg(layer.attributeDefinitions().size());
    lines << QStringLiteral("Extent: %1").arg(layer.extent().toString());
    lines << QStringLiteral("");
    lines << QStringLiteral("Sidecars");
    lines << sidecarLine(QStringLiteral(".tab"), path);
    lines << sidecarLine(QStringLiteral(".dat"), layer.datPath());
    lines << sidecarLine(QStringLiteral(".map"), layer.mapPath());
    lines << sidecarLine(QStringLiteral(".id"), layer.idPath());
    lines << sidecarLine(QStringLiteral(".dbf"), layer.dbfPath());
    return lines.join(QStringLiteral("\n"));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1200, 760);
    window.setWindowTitle(QStringLiteral("TabLoad"));

    auto* splitter = new QSplitter(&window);
    splitter->setOrientation(Qt::Horizontal);

    auto* viewer = new GisViewer(splitter);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);

    auto* rightPanel = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(6, 6, 6, 6);
    rightLayout->setSpacing(6);

    auto* detailsView = new QTextEdit(rightPanel);
    detailsView->setReadOnly(true);
    detailsView->setMinimumHeight(190);

    auto* schemaTable = new QTableWidget(rightPanel);
    schemaTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    schemaTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    schemaTable->verticalHeader()->setVisible(false);

    auto* attributesTable = new QTableWidget(rightPanel);
    attributesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    attributesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    attributesTable->verticalHeader()->setVisible(false);

    rightLayout->addWidget(new QLabel(QStringLiteral("Layer metadata"), rightPanel));
    rightLayout->addWidget(detailsView);
    rightLayout->addWidget(new QLabel(QStringLiteral("Attribute schema"), rightPanel));
    rightLayout->addWidget(schemaTable, 1);
    rightLayout->addWidget(new QLabel(QStringLiteral("First 12 attribute rows"), rightPanel));
    rightLayout->addWidget(attributesTable, 2);

    splitter->addWidget(viewer);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({ 760, 440 });
    window.setCentralWidget(splitter);

    const QString path = dataPath(QStringLiteral("paris.tab"));
    auto layer = std::make_unique<GisLayerTAB>(path);
    layer->style() = tabStyle();

    try
    {
        layer->open();
    }
    catch (const std::exception& ex)
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("TabLoad"),
            QStringLiteral("MapInfo TAB could not be opened:\n%1").arg(QString::fromUtf8(ex.what())));
        return 1;
    }

    GisLayerTAB* layerPtr = layer.get();
    viewer->addLayer(layer);

    detailsView->setPlainText(detailsText(path, *layerPtr));
    fillSchemaTable(*schemaTable, *layerPtr);
    fillAttributeTable(*attributesTable, *layerPtr, 12);

    window.statusBar()->showMessage(QStringLiteral("GisLayerTAB opened %1 memory features and %2 fields.")
        .arg(layerPtr->count())
        .arg(layerPtr->attributeDefinitions().size()));

    window.show();
    viewer->fullExtent();

    return app.exec();
}
