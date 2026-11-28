// Agent Planner Basics: how a natural-language request becomes a safe, recorded map
// operation with GeoKernel's NaturalLanguageGisPlanner, one stage at a time:
//
//   1 request → 2 proposal (workflow DSL v2) → 3 validation → 4 deterministic execution → 5 provenance
//
// Five lessons show what each stage guarantees: a plan that runs, the same plan giving
// the same result, proposals that never run, confirmation bound to one exact plan, and
// a provenance record that shows any change made to it. Your own requests need an LLM
// (GEOKERNEL_LLM_PROVIDER); the lessons run offline with prepared proposals.
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>
#include <stdexcept>
#include <thread>

#include "Agent/AgentExecutionProvenance.h"
#include "Agent/GisLlmProvider.h"
#include "Agent/GisWorkflowViewerExecutor.h"
#include "Agent/NaturalLanguageGisPlanner.h"
#include "Layers/GisLayer.h"
#include "Layers/GisLayerStyle.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Agent;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Viewer;
using GeoKernel::Viewer::Agent::GisWorkflowViewerExecutor;

namespace
{
    const QString Title = QStringLiteral("Agent Planner Basics");
    const char* const DataFiles[] = {"parks.geojson", "streets.geojson", "buildings.geojson", "schools.geojson"};

    QString dataPath(const QString& name)
    {
        return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("data/") + name);
    }

    QJsonObject loadLessons()
    {
        QFile file(dataPath(QStringLiteral("planner-lessons.json")));
        if (!file.open(QIODevice::ReadOnly))
            throw std::runtime_error("data/planner-lessons.json was not found next to the executable");
        return QJsonDocument::fromJson(file.readAll()).object();
    }

    // A proposal as the planner receives it: JSON text, or the prose of a model that
    // did not follow the contract.
    QByteArray proposalText(const QJsonValue& value)
    {
        return value.isString() ? value.toString().toUtf8() : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    }

    QString shortHash(const QString& hash) { return hash.isEmpty() ? QStringLiteral("(none)") : hash.left(12) + QStringLiteral("…"); }

    QString statusName(NaturalLanguagePlanStatus status)
    {
        switch (status)
        {
        case NaturalLanguagePlanStatus::Executed: return QStringLiteral("executed");
        case NaturalLanguagePlanStatus::Rejected: return QStringLiteral("rejected");
        case NaturalLanguagePlanStatus::ConfirmationRequired: return QStringLiteral("confirmation required");
        case NaturalLanguagePlanStatus::ProposalInvalid: return QStringLiteral("invalid");
        case NaturalLanguagePlanStatus::ExecutionFailed: return QStringLiteral("failed");
        case NaturalLanguagePlanStatus::Validated: return QStringLiteral("validated");
        }
        return {};
    }

    // Layers are added bottom to top; each new layer is placed at index 0.
    void loadNeighbourhood(GisViewer& viewer)
    {
        struct Style { QString fill; QString line; float width; float point; };
        const Style styles[] = {{"#a1d99b", "#41ab5d", 1.0f, 0}, {"#bdbdbd", "#9e9e9e", 3.0f, 0},
                                {"#e7d8c3", "#a08b70", 0.8f, 0}, {"#2171b5", "#08306b", 1.2f, 12.0f}};
        for (int i = 0; i < 4; ++i)
        {
            QString error;
            if (!viewer.addLayerFromPath(dataPath(QString::fromLatin1(DataFiles[i])), &error))
                throw std::runtime_error(QStringLiteral("%1 could not be loaded: %2").arg(QString::fromLatin1(DataFiles[i]), error).toStdString());
            auto& style = viewer.mapLayerAt(0)->style();
            style.setFillColor(styles[i].fill);
            style.setFillOpacity(255);
            style.setLineColor(styles[i].line);
            style.setLineWidth(styles[i].width);
            if (styles[i].point > 0)
            {
                style.setPointColor(styles[i].fill);
                style.setPointSize(styles[i].point);
            }
        }
        viewer.refreshLayers();
    }

    // Checks an exported provenance store: the store only imports an intact chain, so
    // the first prefix it refuses ends at the first changed record (0 = intact).
    qsizetype firstBrokenRecord(const QByteArray& exported)
    {
        const auto records = QJsonDocument::fromJson(exported).object().value(QStringLiteral("records")).toArray();
        QJsonArray prefix;
        for (qsizetype i = 0; i < records.size(); ++i)
        {
            prefix.append(records.at(i));
            try
            {
                AgentExecutionProvenanceStore::importJson(QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"records", prefix}}).toJson(QJsonDocument::Compact));
            }
            catch (const std::exception&)
            {
                return i + 1;
            }
        }
        return 0;
    }
}

class PlannerBasics : public QObject
{
public:
    PlannerBasics(GisViewer& viewer, QTextBrowser& log, QJsonObject lessons)
        : m_viewer(viewer), m_log(log), m_lessons(std::move(lessons)), m_executor(viewer), m_planner(&m_provenance)
    {
        // Immutable inputs of every run: the SHA-256 of each data file.
        for (const auto* name : DataFiles)
        {
            QFile file(dataPath(QString::fromLatin1(name)));
            if (file.open(QIODevice::ReadOnly))
                m_inputs.insert(QString::fromLatin1(name), QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex()));
        }
    }

    // ------------------------------------------------------------------ lessons

    bool lessonBasic()
    {
        const auto lesson = m_lessons.value(QStringLiteral("basic")).toObject();
        heading(1, lesson.value(QStringLiteral("title")).toString());
        reset();
        const auto request = lesson.value(QStringLiteral("request")).toString();
        const auto proposal = proposalText(lesson.value(QStringLiteral("proposal")));
        stageRequest(request);
        stageProposal(QStringLiteral("prepared, exactly as a model returns it"), proposal);
        const auto evaluation = stageValidation(proposal, {});
        const auto run = execute(request, proposal, {});
        stageExecution(run);
        stageProvenance();
        return evaluation.status == NaturalLanguagePlanStatus::Validated && run.status == NaturalLanguagePlanStatus::Executed;
    }

    bool lessonDeterminism()
    {
        const auto lesson = m_lessons.value(QStringLiteral("basic")).toObject();
        heading(2, QStringLiteral("Same plan, same result"));
        note(QStringLiteral("The plan from lesson 1 runs, is undone, and runs again on the same data."));
        reset();
        const auto request = lesson.value(QStringLiteral("request")).toString();
        const auto proposal = proposalText(lesson.value(QStringLiteral("proposal")));
        const auto first = execute(request, proposal, {});
        const auto firstRecord = m_provenance.records().isEmpty() ? AgentExecutionRecord{} : m_provenance.records().last();
        line(QStringLiteral("④ First run"), QStringLiteral("%1 · %2 steps · record #%3").arg(statusName(first.status)).arg(first.completedSteps).arg(firstRecord.sequence));
        reset();
        const auto second = execute(request, proposal, {});
        const auto secondRecord = m_provenance.records().last();
        line(QStringLiteral("④ Second run"), QStringLiteral("%1 · %2 steps · record #%3").arg(statusName(second.status)).arg(second.completedSteps).arg(secondRecord.sequence));
        const bool sameInputs = firstRecord.inputSha256 == secondRecord.inputSha256;
        const bool sameSteps = firstRecord.stepOutputs == secondRecord.stepOutputs;
        const bool sameOutput = !firstRecord.outputSha256.isEmpty() && firstRecord.outputSha256 == secondRecord.outputSha256;
        line(QStringLiteral("⑤ Compare"), QStringLiteral("input hashes %1 · output %2 = %3 %4")
            .arg(sameInputs ? QStringLiteral("identical") : QStringLiteral("DIFFERENT"), shortHash(firstRecord.outputSha256), shortHash(secondRecord.outputSha256),
                 sameOutput && sameSteps ? QStringLiteral("✓ identical, step by step") : QStringLiteral("✗ different")));
        for (const auto& value : secondRecord.stepOutputs)
        {
            const auto step = value.toObject();
            detail(QStringLiteral("%1 %2").arg(step.value(QStringLiteral("stepId")).toString(), -14).arg(shortHash(step.value(QStringLiteral("outputSha256")).toString())));
        }
        note(QStringLiteral("Records #%1 and #%2 differ only in their execution id, time and chain position; "
                            "anyone can replay a record and compare the output hash.").arg(firstRecord.sequence).arg(secondRecord.sequence));
        return sameInputs && sameSteps && sameOutput;
    }

    bool lessonRejected()
    {
        heading(3, QStringLiteral("Proposals that never run"));
        note(QStringLiteral("A careless or manipulated model can return anything. Each proposal below goes to the same planner; none reaches the map."));
        reset();
        const auto recordsBefore = m_provenance.records().size();
        bool allStopped = true;
        int index = 0;
        for (const auto& value : m_lessons.value(QStringLiteral("rejected")).toArray())
        {
            const auto item = value.toObject();
            const auto proposal = proposalText(item.value(QStringLiteral("proposal")));
            write(QStringLiteral("<b>%1. %2</b>").arg(++index).arg(item.value(QStringLiteral("title")).toString().toHtmlEscaped()), 8);
            stageProposal(QStringLiteral("bad"), proposal);
            const auto evaluation = NaturalLanguageGisPlanner::evaluateWorkflowProposal(proposal, context({}));
            const auto run = execute(item.value(QStringLiteral("title")).toString(), proposal, {});
            const bool stopped = !run.executorInvoked && run.completedSteps == 0 && evaluation.status != NaturalLanguagePlanStatus::Validated;
            allStopped = allStopped && stopped;
            line(QStringLiteral("③ Validation"), QStringLiteral("<span style='color:#b00020'>%1</span> · %2 — %3")
                .arg(statusName(evaluation.status), evaluation.code, evaluation.message.toHtmlEscaped()), true);
            line(QStringLiteral("④ Execution"), stopped ? QStringLiteral("not started: the executor was never called") : QStringLiteral("✗ it ran"));
            detail(item.value(QStringLiteral("why")).toString());
        }
        const bool noRecords = m_provenance.records().size() == recordsBefore;
        line(QStringLiteral("⑤ Provenance"), QStringLiteral("%1 records before, %2 after: refused proposals leave nothing to record")
            .arg(recordsBefore).arg(m_provenance.records().size()));
        return allStopped && noRecords && index > 0;
    }

    bool lessonConfirmation()
    {
        const auto lesson = m_lessons.value(QStringLiteral("confirm")).toObject();
        heading(4, lesson.value(QStringLiteral("title")).toString());
        reset();
        const auto request = lesson.value(QStringLiteral("request")).toString();
        const auto proposal = proposalText(lesson.value(QStringLiteral("proposal")));
        stageRequest(request);
        stageProposal(QStringLiteral("prepared"), proposal);
        const auto evaluation = stageValidation(proposal, {});
        const auto planId = evaluation.planId;

        const auto unconfirmed = execute(request, proposal, {});
        line(QStringLiteral("④ Run, not confirmed"), QStringLiteral("%1 · %2 steps ran").arg(statusName(unconfirmed.status)).arg(unconfirmed.completedSteps));

        auto changed = lesson.value(QStringLiteral("proposal")).toObject();
        auto steps = changed.value(QStringLiteral("steps")).toArray();
        for (qsizetype i = 0; i < steps.size(); ++i)
        {
            auto step = steps[i].toObject();
            if (step.value(QStringLiteral("id")).toString() != lesson.value(QStringLiteral("changedStep")).toString())
                continue;
            auto parameters = step.value(QStringLiteral("parameters")).toObject();
            parameters.insert(QStringLiteral("values"), lesson.value(QStringLiteral("changedValues")));
            step.insert(QStringLiteral("parameters"), parameters);
            steps[i] = step;
        }
        changed.insert(QStringLiteral("steps"), steps);
        const auto changedText = QJsonDocument(changed).toJson(QJsonDocument::Compact);
        const auto changedEvaluation = NaturalLanguageGisPlanner::evaluateWorkflowProposal(changedText, context({}));
        const auto swapped = execute(request, changedText, planId);
        line(QStringLiteral("④ Swapped plan"), QStringLiteral("%1: plan %2 with the confirmation of %3 → %4 · %5 steps ran")
            .arg(lesson.value(QStringLiteral("changedTitle")).toString().toHtmlEscaped(), shortHash(changedEvaluation.planId), shortHash(planId), statusName(swapped.status))
            .arg(swapped.completedSteps), true);

        const auto confirmed = execute(request, proposal, planId);
        line(QStringLiteral("④ Confirmed run"), QStringLiteral("the user confirms plan %1").arg(shortHash(planId)));
        stageExecution(confirmed);
        stageProvenance();
        return evaluation.status == NaturalLanguagePlanStatus::ConfirmationRequired
            && unconfirmed.status == NaturalLanguagePlanStatus::ConfirmationRequired && unconfirmed.completedSteps == 0
            && changedEvaluation.planId != planId
            && swapped.status == NaturalLanguagePlanStatus::ConfirmationRequired && swapped.completedSteps == 0
            && confirmed.status == NaturalLanguagePlanStatus::Executed;
    }

    bool lessonTamper()
    {
        heading(5, QStringLiteral("A record that shows any change"));
        if (m_provenance.records().isEmpty())
        {
            note(QStringLiteral("No workflow has run yet; running lesson 1 first."));
            lessonBasic();
        }
        const auto exported = m_provenance.exportJson();
        const auto records = m_provenance.records();
        line(QStringLiteral("⑤ Export"), QStringLiteral("%1 records, %2 bytes of JSON").arg(records.size()).arg(exported.size()));
        for (const auto& record : records)
            detail(QStringLiteral("#%1 %2 %3 ← %4").arg(record.sequence).arg(record.executionId, -10).arg(shortHash(record.hash), shortHash(record.previousHash)));
        const auto intact = firstBrokenRecord(exported);
        line(QStringLiteral("⑤ Verify"), intact == 0 ? QStringLiteral("✓ every record matches its hash and the chain") : QStringLiteral("✗ broken at record %1").arg(intact));

        // Somebody edits one output hash in the saved file.
        auto root = QJsonDocument::fromJson(exported).object();
        auto list = root.value(QStringLiteral("records")).toArray();
        auto first = list[0].toObject();
        auto hash = first.value(QStringLiteral("outputSha256")).toString();
        hash[hash.size() - 1] = hash.endsWith(QLatin1Char('0')) ? QLatin1Char('1') : QLatin1Char('0');
        first.insert(QStringLiteral("outputSha256"), hash);
        list[0] = first;
        root.insert(QStringLiteral("records"), list);
        const auto broken = firstBrokenRecord(QJsonDocument(root).toJson(QJsonDocument::Compact));
        line(QStringLiteral("⑤ Edited copy"), QStringLiteral("record #1 output hash changed in one character → %1")
            .arg(broken == 1 ? QStringLiteral("✗ record 1 no longer matches its hash: the change is detected") : QStringLiteral("not detected")));
        note(QStringLiteral("Each record's hash covers the normalized plan, the validation decision, the input and output hashes and the previous record's hash."));
        return intact == 0 && broken == 1;
    }

    // ------------------------------------------------------------------ your own request

    void setLlm(GisPlanProposalProvider provider) { m_llm = std::move(provider); }
    bool hasLlm() const { return static_cast<bool>(m_llm); }

    // Stage 2 runs on a worker thread; done() is called on the GUI thread.
    void plan(const QString& request, std::function<void()> done)
    {
        heading(0, QStringLiteral("Your request"));
        reset();
        stageRequest(request);
        m_pending = {};
        const auto grounding = GisWorkflowViewerExecutor::describeMap(m_viewer) + QLatin1Char('\n') + m_lessons.value(QStringLiteral("groundingNotes")).toString();
        QPointer<PlannerBasics> self(this);
        std::thread([self, provider = m_llm, request, grounding, done]() {
            QByteArray proposal;
            QString error;
            try { proposal = provider(NaturalLanguageGisPlanner::workflowSystemContract(), request, grounding); }
            catch (const std::exception& e) { error = QString::fromUtf8(e.what()); }
            QMetaObject::invokeMethod(qApp, [self, request, proposal, error, done]() {
                if (!self)
                    return;
                if (!error.isEmpty())
                    self->line(QStringLiteral("② Proposal"), QStringLiteral("<span style='color:#b00020'>the model returned no plan: %1</span>").arg(error.toHtmlEscaped()), true);
                else
                {
                    self->stageProposal(QStringLiteral("from the model"), proposal);
                    const auto evaluation = self->stageValidation(proposal, {});
                    if (evaluation.status == NaturalLanguagePlanStatus::Validated || evaluation.status == NaturalLanguagePlanStatus::ConfirmationRequired)
                    {
                        self->m_pending = {request, proposal, evaluation.status == NaturalLanguagePlanStatus::ConfirmationRequired ? evaluation.planId : QString()};
                        self->note(evaluation.status == NaturalLanguagePlanStatus::ConfirmationRequired
                            ? QStringLiteral("The plan changes data. Clicking Confirm and run confirms this exact plan.")
                            : QStringLiteral("Click Run to execute the plan."));
                    }
                }
                done();
            });
        }).detach();
    }

    bool hasPending() const { return !m_pending.proposal.isEmpty(); }
    bool pendingNeedsConfirmation() const { return !m_pending.confirmedPlanId.isEmpty(); }

    void runPending()
    {
        if (!hasPending())
            return;
        const auto pending = m_pending;
        m_pending = {};
        stageExecution(execute(pending.request, pending.proposal, pending.confirmedPlanId));
        stageProvenance();
    }

    void reset()
    {
        while (!m_runs.isEmpty())
            m_executor.undoLastSteps(m_runs.takeLast());
    }

private:
    struct Pending { QString request; QByteArray proposal; QString confirmedPlanId; };

    GisOperationValidationContext context(const QString& confirmedPlanId) const
    {
        // The host decides what may run: here every operation the viewer executor supports,
        // without network access or file writes (gis.network, gis.files.write).
        GisOperationValidationContext value;
        for (const auto& operation : GisWorkflowViewerExecutor::supportedOperations())
        {
            value.allowedOperations.insert(operation);
            value.grantedPermissions.insert(QStringLiteral("gis.execute.") + operation);
        }
        value.confirmedPlanId = confirmedPlanId;
        return value;
    }

    GisWorkflowRunResult execute(const QString& request, const QByteArray& proposal, const QString& confirmedPlanId)
    {
        m_outputs.clear();
        auto step = m_executor.stepExecutor();
        auto recording = [this, step](const GisWorkflowStep& item, const QMap<QString, GisWorkflowStepOutcome>& earlier) {
            auto outcome = step(item, earlier);
            m_outputs.insert(item.id, outcome);
            return outcome;
        };
        // The proposal provider returns the proposal shown in stage 2, so the planner
        // validates and runs exactly that text.
        const auto result = m_planner.runWorkflow(QStringLiteral("run-%1").arg(++m_executions), request, {},
            [proposal](const QString&, const QString&, const QString&) { return proposal; },
            context(confirmedPlanId), recording, m_inputs, {{QStringLiteral("AgentPlannerBasics"), QStringLiteral("1")}});
        if (result.completedSteps > 0)
            m_runs.append(result.completedSteps);
        m_lastRun = result;
        return result;
    }

    // ------------------------------------------------------------------ output

    void heading(int lesson, const QString& title)
    {
        write(QStringLiteral("<span style='font-size:large; font-weight:600'>%1%2</span>")
            .arg(lesson > 0 ? QStringLiteral("Lesson %1 · ").arg(lesson) : QString(), title.toHtmlEscaped()), 14);
    }

    void line(const QString& stage, const QString& text, bool html = false)
    {
        write(QStringLiteral("<b>%1</b>&nbsp;&nbsp;%2").arg(stage.toHtmlEscaped(), html ? text : text.toHtmlEscaped()));
    }

    void detail(const QString& text)
    {
        write(QStringLiteral("<span style='color:#555; font-family:Consolas,monospace'>&nbsp;&nbsp;&nbsp;&nbsp;%1</span>").arg(text.toHtmlEscaped().replace(QLatin1Char(' '), QStringLiteral("&nbsp;"))));
    }

    void note(const QString& text) { write(QStringLiteral("<i style='color:#555'>%1</i>").arg(text.toHtmlEscaped())); }

    // One block per line, without the paragraph margins QTextBrowser::append adds.
    void write(const QString& html, int spaceBefore = 0)
    {
        QTextCursor cursor(m_log.document());
        cursor.movePosition(QTextCursor::End);
        if (!m_log.document()->isEmpty())
        {
            QTextBlockFormat format;
            format.setTopMargin(spaceBefore);
            cursor.insertBlock(format, QTextCharFormat());
        }
        cursor.insertHtml(html);
    }

    void stageRequest(const QString& request) { line(QStringLiteral("① Request"), QStringLiteral("“%1”").arg(request)); }

    void stageProposal(const QString& source, const QByteArray& proposal)
    {
        line(QStringLiteral("② Proposal"), source);
        const auto document = QJsonDocument::fromJson(NaturalLanguageGisPlanner::extractJsonObject(proposal));
        if (!document.isObject())
        {
            detail(QString::fromUtf8(proposal).left(160));
            return;
        }
        // One line per step keeps a plan readable.
        auto root = document.object();
        const auto steps = root.take(QStringLiteral("steps")).toArray();
        detail(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)).chopped(1) + QStringLiteral(",\"steps\":["));
        for (const auto& step : steps)
            detail(QStringLiteral("  ") + QString::fromUtf8(QJsonDocument(step.toObject()).toJson(QJsonDocument::Compact)));
        detail(QStringLiteral("]}"));
    }

    GisWorkflowRunResult stageValidation(const QByteArray& proposal, const QString& confirmedPlanId)
    {
        const auto evaluation = NaturalLanguageGisPlanner::evaluateWorkflowProposal(proposal, context(confirmedPlanId));
        const auto color = evaluation.status == NaturalLanguagePlanStatus::Validated ? QStringLiteral("#1b5e20")
                         : evaluation.status == NaturalLanguagePlanStatus::ConfirmationRequired ? QStringLiteral("#8a4b00") : QStringLiteral("#b00020");
        line(QStringLiteral("③ Validation"), QStringLiteral("<span style='color:%1'>%2</span> · %3 — %4")
            .arg(color, statusName(evaluation.status), evaluation.code, evaluation.message.toHtmlEscaped()), true);
        for (auto summaryLine : evaluation.summary.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            detail(summaryLine.replace(evaluation.planId, shortHash(evaluation.planId)));
        return evaluation;
    }

    void stageExecution(const GisWorkflowRunResult& run)
    {
        line(QStringLiteral("④ Execution"), QStringLiteral("%1 · %2 steps · %3").arg(statusName(run.status)).arg(run.completedSteps).arg(run.message));
        if (run.normalizedPlan.isEmpty())
            return;
        for (const auto& step : GisWorkflowCatalog::parse(run.normalizedPlan).steps)
            if (m_outputs.contains(step.id))
                if (const auto text = GisWorkflowCatalog::describeOutput(step.id, m_outputs.value(step.id).output, 6); !text.isEmpty())
                    for (const auto& outputLine : text.split(QLatin1Char('\n')))
                        detail(outputLine);
    }

    void stageProvenance()
    {
        if (m_lastRun.provenanceHash.isEmpty() || m_provenance.records().isEmpty())
        {
            line(QStringLiteral("⑤ Provenance"), QStringLiteral("nothing ran, nothing recorded"));
            return;
        }
        const auto& record = m_provenance.records().last();
        line(QStringLiteral("⑤ Provenance"), QStringLiteral("record #%1 · hash %2 ← previous %3")
            .arg(record.sequence).arg(shortHash(record.hash), shortHash(record.previousHash)));
        detail(QStringLiteral("plan    %1").arg(shortHash(record.validationAudit.value(QStringLiteral("planId")).toString())));
        for (auto it = record.inputSha256.cbegin(); it != record.inputSha256.cend(); ++it)
            detail(QStringLiteral("input   %1 %2").arg(it.key(), -18).arg(shortHash(it.value())));
        detail(QStringLiteral("output  %1 (%2 step outputs)").arg(shortHash(record.outputSha256)).arg(record.stepOutputs.size()));
    }

    GisViewer& m_viewer;
    QTextBrowser& m_log;
    QJsonObject m_lessons;
    GisWorkflowViewerExecutor m_executor;
    AgentExecutionProvenanceStore m_provenance;
    NaturalLanguageGisPlanner m_planner;
    QMap<QString, QString> m_inputs;
    QMap<QString, GisWorkflowStepOutcome> m_outputs;
    GisWorkflowRunResult m_lastRun;
    QVector<qsizetype> m_runs;
    GisPlanProposalProvider m_llm;
    Pending m_pending;
    int m_executions = 0;
};

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("AgentPlannerBasics"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico")));
    const auto arguments = app.arguments();
    const auto screenshotIndex = arguments.indexOf(QStringLiteral("--screenshot"));
    const auto screenshot = screenshotIndex > 0 && screenshotIndex + 1 < arguments.size() ? arguments.at(screenshotIndex + 1) : QString();

    QJsonObject lessons;
    try
    {
        lessons = loadLessons();
    }
    catch (const std::exception& error)
    {
        QMessageBox::critical(nullptr, Title, QString::fromUtf8(error.what()));
        return 1;
    }

    QMainWindow window;
    window.resize(1440, 900);
    window.setWindowTitle(Title);
    auto* viewer = new GisViewer(&window);
    window.setCentralWidget(viewer);

    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    auto* intro = new QLabel(QStringLiteral("Each lesson sends a request through the planner's five stages: "
                                            "① request → ② proposal → ③ validation → ④ execution → ⑤ provenance. "
                                            "A lesson starts from the original map; its result stays on the map."), panel);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    const QStringList lessonTitles{
        QStringLiteral("1. A plan that reads data and changes the view"), QStringLiteral("2. Same plan, same result"),
        QStringLiteral("3. Proposals that never run"), QStringLiteral("4. Changing data needs confirmation"),
        QStringLiteral("5. A record that shows any change")};
    QVector<QPushButton*> lessonButtons;
    for (const auto& title : lessonTitles)
    {
        auto* button = new QPushButton(title, panel);
        button->setStyleSheet(QStringLiteral("text-align:left; padding:4px 8px"));
        layout->addWidget(button);
        lessonButtons.append(button);
    }
    auto* requestRow = new QHBoxLayout;
    auto* request = new QLineEdit(panel);
    auto* planButton = new QPushButton(QStringLiteral("Plan"), panel);
    auto* runButton = new QPushButton(QStringLiteral("Run"), panel);
    requestRow->addWidget(request, 1);
    requestRow->addWidget(planButton);
    requestRow->addWidget(runButton);
    layout->addSpacing(6);
    layout->addWidget(new QLabel(QStringLiteral("Your own request:"), panel));
    layout->addLayout(requestRow);
    auto* log = new QTextBrowser(panel);
    log->setOpenLinks(false);
    layout->addWidget(log, 1);
    auto* bottomRow = new QHBoxLayout;
    auto* undoButton = new QPushButton(QStringLiteral("Undo all"), panel);
    auto* clearButton = new QPushButton(QStringLiteral("Clear log"), panel);
    bottomRow->addWidget(undoButton);
    bottomRow->addWidget(clearButton);
    bottomRow->addStretch(1);
    layout->addLayout(bottomRow);

    auto* dock = new QDockWidget(QStringLiteral("Planner, stage by stage"), &window);
    dock->setObjectName(QStringLiteral("PlannerDock"));
    dock->setFeatures(QDockWidget::DockWidgetMovable);
    dock->setWidget(panel);
    dock->setMinimumWidth(560);
    window.addDockWidget(Qt::RightDockWidgetArea, dock);
    auto* plannerLabel = new QLabel(&window);
    window.statusBar()->addPermanentWidget(plannerLabel);
    window.statusBar()->showMessage(QStringLiteral("Synthetic neighbourhood:352 buildings, 4 schools, 3 parks, 16 streets"));

    window.show();
    try
    {
        loadNeighbourhood(*viewer);
    }
    catch (const std::exception& error)
    {
        QMessageBox::critical(&window, Title, QString::fromUtf8(error.what()));
        return 1;
    }
    viewer->fullExtent();

    PlannerBasics basics(*viewer, *log, lessons);
    if (!qEnvironmentVariableIsEmpty("GEOKERNEL_LLM_PROVIDER"))
    {
        try
        {
            const auto options = GisLlmProvider::optionsFromEnvironment();
            basics.setLlm(GisLlmProvider::create(options));
            plannerLabel->setText(QStringLiteral("Your requests: %1 (%2)").arg(GisLlmProvider::kindName(options.kind),
                options.model.isEmpty() ? GisLlmProvider::defaultModel(options.kind) : options.model));
        }
        catch (const std::exception& error)
        {
            plannerLabel->setText(QStringLiteral("LLM configuration not usable: %1").arg(QString::fromUtf8(error.what())));
        }
    }
    if (!basics.hasLlm())
    {
        if (plannerLabel->text().isEmpty())
            plannerLabel->setText(QStringLiteral("Lessons run offline; set GEOKERNEL_LLM_PROVIDER to plan your own requests"));
        request->setPlaceholderText(QStringLiteral("Set GEOKERNEL_LLM_PROVIDER (anthropic, openai or ollama) to plan your own requests"));
    }
    else
        request->setText(QStringLiteral("Show the parks with more than 50 trees in dark green"));

    bool busy = false;
    const auto refresh = [&] {
        for (auto* button : lessonButtons)
            button->setEnabled(!busy);
        request->setEnabled(basics.hasLlm() && !busy);
        planButton->setEnabled(basics.hasLlm() && !busy);
        runButton->setEnabled(basics.hasPending() && !busy);
        runButton->setText(basics.pendingNeedsConfirmation() ? QStringLiteral("Confirm and run") : QStringLiteral("Run"));
        undoButton->setEnabled(!busy);
    };
    const auto scroll = [log] { log->verticalScrollBar()->setValue(log->verticalScrollBar()->maximum()); };
    const std::function<bool()> lessonFunctions[] = {
        [&] { return basics.lessonBasic(); }, [&] { return basics.lessonDeterminism(); }, [&] { return basics.lessonRejected(); },
        [&] { return basics.lessonConfirmation(); }, [&] { return basics.lessonTamper(); }};
    for (int i = 0; i < 5; ++i)
        QObject::connect(lessonButtons[i], &QPushButton::clicked, &window, [&, i] {
            const bool ok = lessonFunctions[i]();
            window.statusBar()->showMessage(QStringLiteral("Lesson %1 %2").arg(i + 1).arg(ok ? QStringLiteral("done") : QStringLiteral("did not behave as described")), 8000);
            viewer->refreshLayers();
            scroll();
            refresh();
        });
    QObject::connect(planButton, &QPushButton::clicked, &window, [&] {
        if (request->text().trimmed().isEmpty())
            return;
        busy = true;
        refresh();
        window.statusBar()->showMessage(QStringLiteral("Asking the model for a plan…"));
        basics.plan(request->text().trimmed(), [&] {
            busy = false;
            window.statusBar()->clearMessage();
            scroll();
            refresh();
        });
    });
    QObject::connect(runButton, &QPushButton::clicked, &window, [&] {
        basics.runPending();
        viewer->refreshLayers();
        scroll();
        refresh();
    });
    QObject::connect(undoButton, &QPushButton::clicked, &window, [&] {
        basics.reset();
        viewer->refreshLayers();
        window.statusBar()->showMessage(QStringLiteral("Every lesson result was undone"), 5000);
        refresh();
    });
    QObject::connect(clearButton, &QPushButton::clicked, log, &QTextBrowser::clear);
    refresh();

    if (!screenshot.isEmpty())
    {
        // --screenshot out.png: run the five lessons, save the window and a summary, exit.
        QTimer::singleShot(500, &window, [&] {
            QStringList summary;
            bool all = true;
            for (int i = 0; i < 5; ++i)
            {
                const bool ok = lessonFunctions[i]();
                all = all && ok;
                summary.append(QStringLiteral("lesson %1: %2").arg(i + 1).arg(ok ? QStringLiteral("ok") : QStringLiteral("FAILED")));
            }
            viewer->refreshLayers();
            scroll();
            QTimer::singleShot(1500, &window, [&, summary, all] {
                window.grab().save(screenshot);
                QFile text(screenshot + QStringLiteral(".txt"));
                if (text.open(QIODevice::WriteOnly | QIODevice::Text))
                    text.write((summary.join(QLatin1Char('\n')) + QStringLiteral("\n") + log->toPlainText()).toUtf8());
                QApplication::exit(all ? 0 : 2);
            });
        });
    }
    return app.exec();
}
