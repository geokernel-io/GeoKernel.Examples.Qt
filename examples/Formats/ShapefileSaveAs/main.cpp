#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <memory>

#include "Layers/Defs/GisAttributeDefinition.h"
#include "Layers/GisLayerStyle.h"
#include "Vector/Shapefile/GisLayerSHP.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Layers::Defs;
using namespace GeoKernel::Formats::Vector::Shapefile;
using namespace GeoKernel::Viewer;

namespace
{
    QString dataPath(const QString& relativePath)
    {
        const QDir appDir(QApplication::applicationDirPath());
        return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/data/%1").arg(relativePath)));
    }

    QString outputDirectory()
    {
        const QDir appDir(QApplication::applicationDirPath());
        return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("ShapefileSaveAsData")));
    }

    QString outputShapefilePath()
    {
        return QDir(outputDirectory()).absoluteFilePath(QStringLiteral("world_4326_copy.shp"));
    }

    QStringList shapefileSidecars()
    {
        return {
            QStringLiteral(".shp"),
            QStringLiteral(".shx"),
            QStringLiteral(".dbf"),
            QStringLiteral(".prj"),
            QStringLiteral(".cpg"),
            QStringLiteral(".qix")
        };
    }

    void removeExistingShapefile(const QString& path)
    {
        const QFileInfo info(path);
        const QString base = info.absolutePath() + QLatin1Char('/') + info.completeBaseName();
        for (const QString& extension : shapefileSidecars())
            QFile::remove(base + extension);
    }

    GisLayerStyle worldStyle()
    {
        GisLayerStyle style;
        style.setFillColor(QStringLiteral("#D7E5DF"));
        style.setLineColor(QStringLiteral("#6D8C86"));
        style.setLineWidth(1.0f);
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
        table.setRowCount(rows.isEmpty() ? 1 : rows.size());
        table.setHorizontalHeaderLabels(headers);

        if (rows.isEmpty())
        {
            table.setItem(0, 0, new QTableWidgetItem(QStringLiteral("No attribute rows returned.")));
        }
        else
        {
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
        }

        table.resizeColumnsToContents();
        table.horizontalHeader()->setStretchLastSection(false);
    }

    QString sidecarReport(const QString& path)
    {
        const QFileInfo info(path);
        const QString base = info.absolutePath() + QLatin1Char('/') + info.completeBaseName();

        QStringList lines;
        for (const QString& extension : shapefileSidecars())
        {
            const QFileInfo sidecar(base + extension);
            lines << QStringLiteral("%1: %2")
                .arg(extension, sidecar.exists()
                    ? QStringLiteral("%1 bytes").arg(sidecar.size())
                    : QStringLiteral("missing"));
        }

        return lines.join(QStringLiteral("\n"));
    }

    QString detailsText(
        const QString& sourcePath,
        const QString& outputPath,
        const GisLayerVector& sourceLayer,
        const GisLayerVector* savedLayer)
    {
        QStringList lines;
        lines << QStringLiteral("ShapefileSaveAs sample");
        lines << QStringLiteral("");
        lines << QStringLiteral("API");
        lines << QStringLiteral("GisLayerSHP source(path)");
        lines << QStringLiteral("source.open()");
        lines << QStringLiteral("source.saveAs(outputPath, progress)");
        lines << QStringLiteral("");
        lines << QStringLiteral("Source shapefile");
        lines << QDir::toNativeSeparators(sourcePath);
        lines << QStringLiteral("Source fields: %1").arg(sourceLayer.attributeDefinitions().size());
        lines << QStringLiteral("Source memory shape count: %1").arg(sourceLayer.count());
        lines << QStringLiteral("");
        lines << QStringLiteral("Output shapefile");
        lines << QDir::toNativeSeparators(outputPath);
        lines << sidecarReport(outputPath);

        if (savedLayer != nullptr)
        {
            lines << QStringLiteral("");
            lines << QStringLiteral("Reloaded output");
            lines << QStringLiteral("Layer name: %1").arg(savedLayer->name());
            lines << QStringLiteral("Fields: %1").arg(savedLayer->attributeDefinitions().size());
            lines << QStringLiteral("Extent: %1").arg(savedLayer->extent().toString());
            lines << QStringLiteral("Memory shape count: %1").arg(savedLayer->count());
        }

        return lines.join(QStringLiteral("\n"));
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1200, 760);
    window.setWindowTitle(QStringLiteral("ShapefileSaveAs"));

    auto* root = new QWidget(&window);
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* topBar = new QWidget(root);
    auto* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(6, 4, 6, 4);
    topLayout->setSpacing(8);

    auto* saveButton = new QPushButton(QStringLiteral("Save As Shapefile"), topBar);
    auto* progressBar = new QProgressBar(topBar);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setTextVisible(true);

    topLayout->addWidget(saveButton);
    topLayout->addWidget(progressBar, 1);

    auto* splitter = new QSplitter(root);
    splitter->setOrientation(Qt::Horizontal);

    auto* viewer = new GisViewer(splitter);
    viewer->setActiveTool(GisViewerTool::Pan);

    auto* rightPanel = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(6, 6, 6, 6);
    rightLayout->setSpacing(6);

    auto* detailsView = new QTextEdit(rightPanel);
    detailsView->setReadOnly(true);

    auto* attributesTable = new QTableWidget(rightPanel);
    attributesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    attributesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    attributesTable->verticalHeader()->setVisible(false);

    rightLayout->addWidget(new QLabel(QStringLiteral("SaveAs state"), rightPanel));
    rightLayout->addWidget(detailsView, 1);
    rightLayout->addWidget(new QLabel(QStringLiteral("Reloaded output attributes"), rightPanel));
    rightLayout->addWidget(attributesTable, 1);

    splitter->addWidget(viewer);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({ 760, 440 });

    rootLayout->addWidget(topBar);
    rootLayout->addWidget(splitter, 1);
    window.setCentralWidget(root);

    const QString sourcePath = dataPath(QStringLiteral("shapefile/world_4326.shp"));
    const QString outputPath = outputShapefilePath();

    auto sourceLayer = std::make_unique<GisLayerSHP>(sourcePath);
    sourceLayer->style() = worldStyle();

    try
    {
        sourceLayer->open();
    }
    catch (const std::exception& ex)
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("ShapefileSaveAs"),
            QStringLiteral("Source shapefile could not be opened:\n%1").arg(QString::fromUtf8(ex.what())));
        return 1;
    }

    GisLayerSHP* sourceLayerPtr = sourceLayer.get();
    viewer->addLayer(sourceLayer);

    std::unique_ptr<GisLayerSHP> savedLayer;

    const auto runSaveAs = [&]
    {
        saveButton->setEnabled(false);
        progressBar->setValue(0);
        window.statusBar()->showMessage(QStringLiteral("Saving shapefile copy..."));

        try
        {
            QDir().mkpath(outputDirectory());
            removeExistingShapefile(outputPath);

            sourceLayerPtr->saveAs(outputPath, [progressBar](int value)
            {
                progressBar->setValue(std::clamp(value, 0, 100));
                QApplication::processEvents();
            });

            savedLayer = std::make_unique<GisLayerSHP>(outputPath);
            savedLayer->style() = worldStyle();
            savedLayer->open();

            fillAttributeTable(*attributesTable, *savedLayer, 12);
            detailsView->setPlainText(detailsText(sourcePath, outputPath, *sourceLayerPtr, savedLayer.get()));
            window.statusBar()->showMessage(QStringLiteral("saveAs wrote %1").arg(QDir::toNativeSeparators(outputPath)));
        }
        catch (const std::exception& ex)
        {
            detailsView->setPlainText(detailsText(sourcePath, outputPath, *sourceLayerPtr, nullptr));
            QMessageBox::critical(
                &window,
                QStringLiteral("ShapefileSaveAs"),
                QStringLiteral("saveAs failed:\n%1").arg(QString::fromUtf8(ex.what())));
            window.statusBar()->showMessage(QStringLiteral("saveAs failed."));
        }

        progressBar->setValue(QFileInfo::exists(outputPath) ? 100 : progressBar->value());
        saveButton->setEnabled(true);
    };

    QObject::connect(saveButton, &QPushButton::clicked, &window, runSaveAs);

    detailsView->setPlainText(detailsText(sourcePath, outputPath, *sourceLayerPtr, nullptr));
    fillAttributeTable(*attributesTable, *sourceLayerPtr, 12);

    window.show();
    viewer->fullExtent();
    runSaveAs();

    return app.exec();
}
