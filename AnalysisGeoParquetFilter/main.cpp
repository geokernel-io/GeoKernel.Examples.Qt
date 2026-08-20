#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include "CoordinateSystems/CoordinateSystemFactory.h"
#include "GeoKernelAnalysis.h"
#include "Layers/GisLayerStyle.h"
#include "Viewer/GisViewer.h"
#include "SampleSupport.h"

using namespace GeoKernel::Analysis;
using namespace GeoKernel::Core::CoordinateSystems;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Viewer;

namespace
{
    QString attemptsText(const AnalysisResult& result)
    {
        QStringList lines;
        lines << QStringLiteral("ANALYSIS PLAN")
              << QStringLiteral("Requested backend: Auto")
              << QStringLiteral("Selected backend: %1").arg(analysisBackendName(result.backend))
              << QStringLiteral("Predicate pushdown: %1").arg(result.plan.usesPredicatePushdown ? "yes" : "no")
              << QStringLiteral("Projection pushdown: %1").arg(result.plan.usesProjectionPushdown ? "yes" : "no")
              << QStringLiteral("")
              << result.plan.explanation
              << QStringLiteral("")
              << QStringLiteral("EXECUTION ATTEMPTS");
        for (const auto& attempt : result.attempts)
            lines << QStringLiteral("%1: %2 (%3 ms)%4")
                .arg(analysisBackendName(attempt.backend))
                .arg(attempt.succeeded ? QStringLiteral("success") : QStringLiteral("failed"))
                .arg(attempt.elapsedMilliseconds)
                .arg(attempt.message.isEmpty() ? QString() : QStringLiteral(" — %1").arg(attempt.message));
        return lines.join('\n');
    }

    GisLayerStyle resultStyle()
    {
        GisLayerStyle style;
        style.setFillColor(QStringLiteral("#55B7E9"));
        style.setLineColor(QStringLiteral("#116A9B"));
        style.setLineWidth(0.8f);
        return style;
    }
}

class AnalysisWindow final : public QMainWindow
{
public:
    AnalysisWindow()
    {
        setWindowTitle(QStringLiteral("AnalysisGeoParquetFilter"));
        setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico")));
        resize(1220, 790);

        auto* central = new QWidget(this);
        auto* root = new QHBoxLayout(central);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto* mapPanel = new QWidget(central);
        auto* mapLayout = new QVBoxLayout(mapPanel);
        mapLayout->setContentsMargins(0, 0, 0, 0);
        m_viewer = new GisViewer(mapPanel);
        m_viewer->setActiveTool(GisViewerTool::Pan);
        createNavigationToolbar(*this, *m_viewer);
        mapLayout->addWidget(m_viewer, 1);

        auto* controls = new QWidget(central);
        controls->setFixedWidth(340);
        auto* controlsLayout = new QVBoxLayout(controls);
        auto* title = new QLabel(QStringLiteral("<b>Backend-neutral analysis</b>"), controls);
        controlsLayout->addWidget(title);

        auto* form = new QFormLayout();
        m_className = new QComboBox(controls);
        m_className->addItems({ QStringLiteral("apartments"), QStringLiteral("house"),
                               QStringLiteral("commercial"), QStringLiteral("industrial") });
        m_limit = new QSpinBox(controls);
        m_limit->setRange(1, 100000);
        m_limit->setValue(25000);
        form->addRow(QStringLiteral("Building class"), m_className);
        form->addRow(QStringLiteral("Maximum results"), m_limit);
        form->addRow(QStringLiteral("BBOX"), new QLabel(QStringLiteral("18.04, 59.30, 18.10, 59.35"), controls));
        controlsLayout->addLayout(form);

        m_run = new QPushButton(QStringLiteral("Run automatic analysis"), controls);
        m_cancel = new QPushButton(QStringLiteral("Cancel"), controls);
        m_cancel->setEnabled(false);
        auto* buttons = new QHBoxLayout();
        buttons->addWidget(m_run, 1);
        buttons->addWidget(m_cancel);
        controlsLayout->addLayout(buttons);

        m_progress = new QProgressBar(controls);
        m_progress->setRange(0, 100);
        controlsLayout->addWidget(m_progress);
        m_stage = new QLabel(QStringLiteral("Ready."), controls);
        m_stage->setWordWrap(true);
        controlsLayout->addWidget(m_stage);

        m_diagnostics = new QTextEdit(controls);
        m_diagnostics->setReadOnly(true);
        controlsLayout->addWidget(m_diagnostics, 1);

        root->addWidget(mapPanel, 1);
        root->addWidget(controls);
        setCentralWidget(central);
        statusBar()->showMessage(QStringLiteral("Ready."));

        m_poll.setInterval(40);
        connect(&m_poll, &QTimer::timeout, this, [this] { pollJob(); });
        connect(m_run, &QPushButton::clicked, this, [this] { beginAnalysis(); });
        connect(m_cancel, &QPushButton::clicked, this, [this]
        {
            if (m_job.isValid()) m_job.cancel();
        });
        QTimer::singleShot(0, this, [this] { prepareData(); });
    }

protected:
    void closeEvent(QCloseEvent* event) override
    {
        m_closing = true;
        if (m_job.isValid() && !m_job.isFinished()) m_job.cancel();
        QMainWindow::closeEvent(event);
    }

private:
    void prepareData()
    {
        m_path = ensureStockholmGeoParquet(this);
        if (m_path.isEmpty())
        {
            m_run->setEnabled(false);
            m_stage->setText(QStringLiteral("Sample data is unavailable."));
            return;
        }
        beginAnalysis();
    }

    void beginAnalysis()
    {
        if (m_path.isEmpty() || (m_job.isValid() && !m_job.isFinished())) return;

        AnalysisRequest request;
        request.operation = AnalysisOperation::SpatialFilter;
        request.backend = AnalysisBackend::Auto;
        request.inputKind = AnalysisDataKind::GeoParquet;
        request.source = m_path;
        request.hasAttributeFilter = true;
        request.hasSpatialFilter = true;
        request.projectionRequired = true;
        request.options = {
            { QStringLiteral("columns"), QStringList{ QStringLiteral("id"), QStringLiteral("class"), QStringLiteral("geometry") } },
            { QStringLiteral("predicateSql"), QStringLiteral("class = ?") },
            { QStringLiteral("predicateParameters"), QVariantList{ m_className->currentText() } },
            { QStringLiteral("extent"), QVariantList{ 18.04, 59.30, 18.10, 59.35 } },
            { QStringLiteral("limit"), m_limit->value() }
        };

        m_run->setEnabled(false);
        m_cancel->setEnabled(true);
        m_progress->setValue(0);
        m_diagnostics->clear();
        m_stage->setText(QStringLiteral("Queuing analysis..."));
        statusBar()->showMessage(QStringLiteral("Analysis queued..."));

        QPointer<AnalysisWindow> self(this);
        m_executor = AnalysisExecutor::createDefault();
        m_job = m_executor.executeAsync(request, [self](const AnalysisProgress& progress)
        {
            if (!self) return;
            QMetaObject::invokeMethod(self, [self, progress]
            {
                if (!self || self->m_closing) return;
                self->m_progress->setValue(progress.percent);
                self->m_stage->setText(QStringLiteral("%1 — %2")
                    .arg(analysisProgressStageName(progress.stage), progress.message));
                self->statusBar()->showMessage(progress.message);
            }, Qt::QueuedConnection);
        });
        m_poll.start();
    }

    void pollJob()
    {
        if (!m_job.isValid() || !m_job.isFinished()) return;
        m_poll.stop();
        const AnalysisResult result = m_job.wait();
        m_run->setEnabled(true);
        m_cancel->setEnabled(false);

        if (result.cancelled)
        {
            m_stage->setText(QStringLiteral("Analysis cancelled."));
            statusBar()->showMessage(QStringLiteral("Analysis cancelled."));
            return;
        }
        if (!result.succeeded)
        {
            m_stage->setText(result.message);
            m_diagnostics->setPlainText(attemptsText(result));
            QMessageBox::critical(this, windowTitle(), result.message);
            return;
        }

        try
        {
            AnalysisLayerMaterializerOptions options;
            options.name = QStringLiteral("Filtered %1 buildings").arg(m_className->currentText());
            options.skipInvalidGeometries = true;
            auto materialized = AnalysisLayerMaterializer::materialize(result, options);
            materialized.layer->setCoordinateSystem(CoordinateSystemFactory::fromEpsg(4326));
            materialized.layer->style() = resultStyle();

            const int count = materialized.materializedCount;
            m_viewer->clearLayers();
            m_viewer->addLayer(std::move(materialized.layer));
            m_viewer->fullExtent();

            QString diagnostics = attemptsText(result);
            diagnostics += QStringLiteral("\n\nMATERIALIZATION\nSource rows: %1\nLayer features: %2\nSkipped: %3")
                .arg(materialized.sourceRowCount).arg(count).arg(materialized.skippedCount);
            if (!materialized.warnings.isEmpty())
                diagnostics += QStringLiteral("\nWarnings:\n") + materialized.warnings.join('\n');
            m_diagnostics->setPlainText(diagnostics);
            m_progress->setValue(100);
            m_stage->setText(QStringLiteral("%1 selected and displayed with %2.")
                .arg(count).arg(analysisBackendName(result.backend)));
            statusBar()->showMessage(QStringLiteral("Analysis completed successfully."));
        }
        catch (const std::exception& error)
        {
            QMessageBox::critical(this, windowTitle(), QString::fromUtf8(error.what()));
        }
    }

    GisViewer* m_viewer = nullptr;
    QComboBox* m_className = nullptr;
    QSpinBox* m_limit = nullptr;
    QPushButton* m_run = nullptr;
    QPushButton* m_cancel = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_stage = nullptr;
    QTextEdit* m_diagnostics = nullptr;
    QString m_path;
    AnalysisExecutor m_executor;
    AnalysisJob m_job;
    QTimer m_poll;
    bool m_closing = false;
};

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    AnalysisWindow window;
    window.show();
    return application.exec();
}
