#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextEdit>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

#include "CoordinateSystems/CoordinateSystemFactory.h"
#include "GeoKernelDuckDb.h"
#include "Layers/Defs/GisAttributeDefinition.h"
#include "Layers/Defs/GisAttributeType.h"
#include "Layers/GisLayerStyle.h"
#include "Layers/GisLayerVector.h"
#include "Serialization/Wkb/GisWkbReader.h"
#include "Viewer/GisViewer.h"
#include "SampleSupport.h"

using namespace GeoKernel::Core::CoordinateSystems;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Layers::Defs;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Data::DuckDB;
using namespace GeoKernel::Viewer;

namespace
{
    constexpr double BboxXMin = 18.04;
    constexpr double BboxYMin = 59.30;
    constexpr double BboxXMax = 18.10;
    constexpr double BboxYMax = 59.35;

    struct PathMetrics
    {
        qint64 elapsedMs = 0;
        qint64 sourceRows = 0;
        qint64 resultRows = 0;
        qint64 geometryBytes = 0;
        int transferredColumns = 0;
    };

    struct ComparisonResult
    {
        PathMetrics fullTransfer;
        PathMetrics pushedDown;
        qint64 datasetRows = 0;
        QString className;
        QString path;
        std::unique_ptr<GisLayerVector> displayLayer;
    };

    bool bboxIntersects(const DuckQueryResult& rows, qsizetype row)
    {
        return rows.value(row, QStringLiteral("xmax")).toDouble() >= BboxXMin &&
            rows.value(row, QStringLiteral("xmin")).toDouble() <= BboxXMax &&
            rows.value(row, QStringLiteral("ymax")).toDouble() >= BboxYMin &&
            rows.value(row, QStringLiteral("ymin")).toDouble() <= BboxYMax;
    }

    qint64 transferredBytes(const DuckQueryResult& rows)
    {
        qint64 bytes = 0;
        for (qsizetype row = 0; row < rows.rowCount(); ++row)
        {
            for (qsizetype column = 0; column < rows.columnCount(); ++column)
            {
                const QVariant value = rows.value(row, column);
                bytes += value.typeId() == QMetaType::QByteArray
                    ? value.toByteArray().size()
                    : value.toString().toUtf8().size();
            }
        }
        return bytes;
    }

    QString humanBytes(qint64 bytes)
    {
        const double mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
        return mib >= 1.0
            ? QStringLiteral("%1 MiB").arg(mib, 0, 'f', 2)
            : QStringLiteral("%1 KiB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
    }

    double reduction(qint64 before, qint64 after)
    {
        return before <= 0 ? 0.0 : 100.0 * (1.0 - static_cast<double>(after) / static_cast<double>(before));
    }

    GisLayerStyle buildingStyle()
    {
        GisLayerStyle style;
        style.setFillColor(QStringLiteral("#65B8E8"));
        style.setLineColor(QStringLiteral("#176B9C"));
        style.setLineWidth(0.8f);
        return style;
    }

    std::shared_ptr<ComparisonResult> runComparison(
        const QString& path,
        const QString& className,
        qint64 limit)
    {
        auto output = std::make_shared<ComparisonResult>();
        output->path = path;
        output->className = className;

        DuckConnection connection;
        const DuckGeoParquetMetadata metadata = DuckGeoParquet::inspect(connection, path);
        output->datasetRows = metadata.featureCount;

        // Warm metadata and parquet page caches once. Both measured paths then
        // use the same connection and operating-system cache state.
        connection.query(QStringLiteral("SELECT count(*) FROM read_parquet(?)"), { path });

        {
            QElapsedTimer timer;
            timer.start();
            const DuckQueryResult allRows = connection.query(
                QStringLiteral(
                    "SELECT id,class,geometry,"
                    "bbox.xmin AS xmin,bbox.ymin AS ymin,bbox.xmax AS xmax,bbox.ymax AS ymax "
                    "FROM read_parquet(?)"),
                { path });

            qint64 matched = 0;
            for (qsizetype row = 0; row < allRows.rowCount() && matched < limit; ++row)
            {
                if (allRows.value(row, QStringLiteral("class")).toString() == className &&
                    bboxIntersects(allRows, row))
                {
                    ++matched;
                }
            }
            output->fullTransfer.elapsedMs = timer.elapsed();
            output->fullTransfer.sourceRows = allRows.rowCount();
            output->fullTransfer.resultRows = matched;
            output->fullTransfer.transferredColumns = allRows.columnCount();
            output->fullTransfer.geometryBytes = transferredBytes(allRows);
        }

        DuckGeoParquetQuery request;
        request.columns = { QStringLiteral("id"), QStringLiteral("class"), metadata.primaryGeometryColumn };
        request.extent = GisExtent(BboxXMin, BboxYMin, BboxXMax, BboxYMax);
        request.predicateSql = QStringLiteral("class = ?");
        request.predicateParameters = { className };
        request.limit = limit;

        QElapsedTimer optimizedTimer;
        optimizedTimer.start();
        const DuckQueryResult filteredRows = DuckGeoParquet::query(connection, path, request);

        auto layer = GisLayerVector::createInMemory(QStringLiteral("DuckDB pushdown result"));
        layer->setCoordinateSystem(CoordinateSystemFactory::fromEpsg(4326));
        layer->addAttributeDefinition({ QStringLiteral("id"), GisAttributeType::Integer, 18, 0 });
        layer->addAttributeDefinition({ QStringLiteral("class"), GisAttributeType::String, 80, 0 });

        const int geometryColumn = filteredRows.columnIndex(metadata.primaryGeometryColumn);
        for (qsizetype row = 0; row < filteredRows.rowCount(); ++row)
        {
            const QByteArray wkb = filteredRows.value(row, geometryColumn).toByteArray();
            if (wkb.isEmpty())
                continue;
            auto shape = GeoKernel::Core::Serialization::Wkb::GisWkbReader::read(wkb);
            shape->attributes().insert(QStringLiteral("id"), filteredRows.value(row, QStringLiteral("id")));
            shape->attributes().insert(QStringLiteral("class"), filteredRows.value(row, QStringLiteral("class")));
            layer->addShape(std::move(shape));
        }
        layer->style() = buildingStyle();
        layer->buildSpatialIndex();

        output->pushedDown.elapsedMs = optimizedTimer.elapsed();
        output->pushedDown.sourceRows = filteredRows.rowCount();
        output->pushedDown.resultRows = layer->count();
        output->pushedDown.transferredColumns = filteredRows.columnCount();
        output->pushedDown.geometryBytes = transferredBytes(filteredRows);
        output->displayLayer = std::move(layer);
        return output;
    }

    QString reportText(const ComparisonResult& result)
    {
        const double speedup = result.pushedDown.elapsedMs <= 0
            ? 0.0
            : static_cast<double>(result.fullTransfer.elapsedMs) / result.pushedDown.elapsedMs;
        QStringList lines;
        lines << QStringLiteral("DUCKDB GEOPARQUET ANALYTICS");
        lines << QStringLiteral("");
        lines << QStringLiteral("Dataset: stockholm_buildings.parquet");
        lines << QStringLiteral("Dataset rows: %1").arg(result.datasetRows);
        lines << QStringLiteral("Filter: class = '%1'").arg(result.className);
        lines << QStringLiteral("BBOX: %1, %2, %3, %4")
            .arg(BboxXMin).arg(BboxYMin).arg(BboxXMax).arg(BboxYMax);
        lines << QStringLiteral("Result rows: %1").arg(result.pushedDown.resultRows);
        lines << QStringLiteral("");
        lines << QStringLiteral("FULL TRANSFER + APPLICATION FILTER");
        lines << QStringLiteral("Rows transferred: %1").arg(result.fullTransfer.sourceRows);
        lines << QStringLiteral("Columns transferred: %1").arg(result.fullTransfer.transferredColumns);
        lines << QStringLiteral("Payload approximation: %1").arg(humanBytes(result.fullTransfer.geometryBytes));
        lines << QStringLiteral("Elapsed: %1 ms").arg(result.fullTransfer.elapsedMs);
        lines << QStringLiteral("");
        lines << QStringLiteral("DUCKDB PUSHDOWN");
        lines << QStringLiteral("Rows transferred: %1").arg(result.pushedDown.sourceRows);
        lines << QStringLiteral("Columns transferred: %1").arg(result.pushedDown.transferredColumns);
        lines << QStringLiteral("Payload approximation: %1").arg(humanBytes(result.pushedDown.geometryBytes));
        lines << QStringLiteral("Elapsed + Viewer materialization: %1 ms").arg(result.pushedDown.elapsedMs);
        lines << QStringLiteral("");
        lines << QStringLiteral("MEASURED GAIN");
        lines << QStringLiteral("Speedup: %1x").arg(speedup, 0, 'f', 2);
        lines << QStringLiteral("Row transfer reduction: %1%").arg(
            reduction(result.fullTransfer.sourceRows, result.pushedDown.sourceRows), 0, 'f', 2);
        lines << QStringLiteral("Payload reduction: %1%").arg(
            reduction(result.fullTransfer.geometryBytes, result.pushedDown.geometryBytes), 0, 'f', 2);
        lines << QStringLiteral("");
        lines << QStringLiteral("The optimized path pushes class, BBOX, projection and limit into DuckDB before WKB crosses into the Viewer.");
        return lines.join(QStringLiteral("\n"));
    }
}

class AnalyticsWindow final : public QMainWindow
{
public:
    explicit AnalyticsWindow(QString parquetPath)
        : m_path(std::move(parquetPath))
    {
        setWindowTitle(QStringLiteral("DuckDbGeoParquetAnalytics"));
        setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico")));
        resize(1220, 790);

        auto* central = new QWidget(this);
        auto* layout = new QHBoxLayout(central);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto* mapPanel = new QWidget(central);
        auto* mapLayout = new QVBoxLayout(mapPanel);
        mapLayout->setContentsMargins(0, 0, 0, 0);
        mapLayout->setSpacing(0);
        m_viewer = new GisViewer(mapPanel);
        m_viewer->setActiveTool(GisViewerTool::Pan);
        createNavigationToolbar(*this, *m_viewer);
        mapLayout->addWidget(m_viewer, 1);

        auto* controls = new QWidget(central);
        controls->setFixedWidth(390);
        auto* controlsLayout = new QVBoxLayout(controls);
        auto* title = new QLabel(QStringLiteral("DuckDB GeoParquet analytics"), controls);
        QFont titleFont = title->font();
        titleFont.setBold(true);
        titleFont.setPointSize(titleFont.pointSize() + 1);
        title->setFont(titleFont);
        controlsLayout->addWidget(title);

        auto* form = new QFormLayout;
        m_classEdit = new QComboBox(controls);
        m_classEdit->setEditable(true);
        m_classEdit->addItems({ QStringLiteral("apartments"), QStringLiteral("residential"), QStringLiteral("house") });
        m_limitSpin = new QSpinBox(controls);
        m_limitSpin->setRange(1, 100000);
        m_limitSpin->setValue(25000);
        m_limitSpin->setSingleStep(5000);
        form->addRow(QStringLiteral("Building class"), m_classEdit);
        form->addRow(QStringLiteral("Maximum results"), m_limitSpin);
        form->addRow(QStringLiteral("Spatial filter"), new QLabel(QStringLiteral("Central Stockholm BBOX"), controls));
        controlsLayout->addLayout(form);

        m_runButton = new QPushButton(QStringLiteral("Run measured comparison"), controls);
        controlsLayout->addWidget(m_runButton);
        m_report = new QTextEdit(controls);
        m_report->setReadOnly(true);
        m_report->setPlainText(QStringLiteral(
            "Press Run measured comparison.\n\n"
            "The baseline transfers every row and filters in the application. "
            "The optimized path pushes predicate, BBOX, projection and limit into DuckDB."));
        controlsLayout->addWidget(m_report, 1);

        layout->addWidget(mapPanel, 1);
        layout->addWidget(controls);
        setCentralWidget(central);
        statusBar()->showMessage(QStringLiteral("Ready: %1").arg(QFileInfo(m_path).fileName()));

        connect(m_runButton, &QPushButton::clicked, this, [this] { run(); });
    }

private:
    void run()
    {
        m_runButton->setEnabled(false);
        m_report->setPlainText(QStringLiteral("Running full transfer and DuckDB pushdown paths..."));
        statusBar()->showMessage(QStringLiteral("Benchmark running in background..."));
        const QString className = m_classEdit->currentText().trimmed();
        const qint64 limit = m_limitSpin->value();
        QPointer<AnalyticsWindow> self(this);

        auto* worker = QThread::create([self, path = m_path, className, limit]()
        {
            try
            {
                const auto result = runComparison(path, className, limit);
                QMetaObject::invokeMethod(self.data(), [self, result]()
                {
                    if (!self)
                        return;
                    self->m_viewer->clearLayers();
                    self->m_viewer->addLayer(std::move(result->displayLayer));
                    self->m_viewer->fullExtent();
                    self->m_report->setPlainText(reportText(*result));
                    self->m_runButton->setEnabled(true);
                    self->statusBar()->showMessage(QStringLiteral("Comparison completed."));
                }, Qt::QueuedConnection);
            }
            catch (const std::exception& ex)
            {
                const QString message = QString::fromUtf8(ex.what());
                QMetaObject::invokeMethod(self.data(), [self, message]()
                {
                    if (!self)
                        return;
                    self->m_runButton->setEnabled(true);
                    self->m_report->setPlainText(QStringLiteral("Comparison failed:\n%1").arg(message));
                    self->statusBar()->showMessage(QStringLiteral("Comparison failed."));
                }, Qt::QueuedConnection);
            }
        });
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        worker->start();
    }

    QString m_path;
    GisViewer* m_viewer = nullptr;
    QComboBox* m_classEdit = nullptr;
    QSpinBox* m_limitSpin = nullptr;
    QPushButton* m_runButton = nullptr;
    QTextEdit* m_report = nullptr;
};

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("DuckDbGeoParquetAnalytics"));

    const QString parquetPath = ensureStockholmGeoParquet(nullptr);
    if (parquetPath.isEmpty())
        return 1;

    if (application.arguments().contains(QStringLiteral("--benchmark")))
    {
        try
        {
            const auto result = runComparison(parquetPath, QStringLiteral("apartments"), 25000);
            const QString report = reportText(*result);
            qInfo().noquote() << report;
            QFile reportFile(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("benchmark-report.txt")));
            if (reportFile.open(QIODevice::WriteOnly | QIODevice::Text))
                reportFile.write(report.toUtf8());
            return result->pushedDown.resultRows > 0 ? 0 : 2;
        }
        catch (const std::exception& ex)
        {
            qCritical().noquote() << QString::fromUtf8(ex.what());
            return 3;
        }
    }

    AnalyticsWindow window(parquetPath);
    window.show();
    return application.exec();
}
