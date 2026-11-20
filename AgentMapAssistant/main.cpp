#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

#include <memory>
#include <stdexcept>

#include "Agent/GisLlmProvider.h"
#include "Agent/GisAgentPanel.h"
#include "Layers/GisLayer.h"
#include "Layers/GisLayerStyle.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Agent;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Viewer;
using GeoKernel::Viewer::Agent::GisAgentPanel;

namespace
{
    // One prepared request: the text shown to the user and the workflow DSL v2
    // proposal an LLM would return for it.
    struct DemoRequest
    {
        QString request;
        QByteArray proposal;
    };

    struct Demo
    {
        QString groundingNotes;
        QVector<DemoRequest> requests;
    };

    QString dataPath(const QString& name)
    {
        return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("data/") + name);
    }

    Demo loadDemo()
    {
        QFile file(dataPath(QStringLiteral("agent-demo.json")));
        if (!file.open(QIODevice::ReadOnly))
            throw std::runtime_error("data/agent-demo.json was not found next to the executable");
        const auto root = QJsonDocument::fromJson(file.readAll()).object();
        Demo demo;
        demo.groundingNotes = root.value(QStringLiteral("groundingNotes")).toString();
        for (const auto& value : root.value(QStringLiteral("requests")).toArray())
        {
            const auto item = value.toObject();
            demo.requests.append({item.value(QStringLiteral("request")).toString(),
                                  QJsonDocument(item.value(QStringLiteral("proposal")).toObject()).toJson(QJsonDocument::Compact)});
        }
        return demo;
    }

    // Without an LLM the sample still runs end to end: the prepared plans for the sample
    // requests go through the same validation, preview, confirmation and provenance as
    // a model's proposal would. Anything else is answered with how to enable a model.
    GisPlanProposalProvider offlineProvider(const QVector<DemoRequest>& requests)
    {
        return [requests](const QString&, const QString& intent, const QString&) -> QByteArray {
            for (const auto& item : requests)
                if (item.request.compare(intent.trimmed(), Qt::CaseInsensitive) == 0)
                    return item.proposal;
            throw std::runtime_error(
                "offline demo mode only has plans for the sample requests. Set GEOKERNEL_LLM_PROVIDER "
                "(anthropic, openai or ollama) and its API key to plan your own requests.");
        };
    }

    struct Planner
    {
        GisPlanProposalProvider provider;
        QString description;
    };

    Planner choosePlanner(const Demo& demo, QWidget* parent)
    {
        if (!qEnvironmentVariableIsEmpty("GEOKERNEL_LLM_PROVIDER"))
        {
            try
            {
                const auto options = GisLlmProvider::optionsFromEnvironment();
                const auto model = options.model.isEmpty() ? GisLlmProvider::defaultModel(options.kind) : options.model;
                return {GisLlmProvider::create(options),
                        QStringLiteral("Planner: %1 (%2)").arg(GisLlmProvider::kindName(options.kind), model)};
            }
            catch (const std::exception& error)
            {
                QMessageBox::warning(parent, QStringLiteral("Agent Map Assistant"),
                    QStringLiteral("The LLM configuration was not usable:\n%1\n\nContinuing in offline demo mode.")
                        .arg(QString::fromUtf8(error.what())));
            }
        }
        return {offlineProvider(demo.requests), QStringLiteral("Planner: offline demo (prepared plans)")};
    }

    void style(GisLayer& layer, const QString& fill, const QString& line, int fillOpacity, float lineWidth, float pointSize = 0)
    {
        auto& value = layer.style();
        value.setFillColor(fill);
        value.setFillOpacity(fillOpacity);
        value.setLineColor(line);
        value.setLineWidth(lineWidth);
        if (pointSize > 0)
        {
            value.setPointColor(fill);
            value.setPointSize(pointSize);
        }
    }

    // Layers are added bottom to top; each new layer is placed at index 0.
    bool loadNeighbourhood(GisViewer& viewer, QWidget* parent)
    {
        struct Source { const char* file; QString fill; QString line; int opacity; float width; float point; };
        const Source sources[] = {
            {"parks.geojson", "#a1d99b", "#41ab5d", 255, 1.0f, 0},
            {"streets.geojson", "#bdbdbd", "#9e9e9e", 255, 3.0f, 0},
            {"buildings.geojson", "#e7d8c3", "#a08b70", 255, 0.8f, 0},
            {"schools.geojson", "#2171b5", "#08306b", 255, 1.2f, 12.0f},
        };
        for (const auto& source : sources)
        {
            QString error;
            if (!viewer.addLayerFromPath(dataPath(QString::fromLatin1(source.file)), &error))
            {
                QMessageBox::critical(parent, QStringLiteral("Agent Map Assistant"),
                    QStringLiteral("%1 could not be loaded:\n%2").arg(QString::fromLatin1(source.file), error));
                return false;
            }
            if (auto* layer = viewer.mapLayerAt(0))
                style(*layer, source.fill, source.line, source.opacity, source.width, source.point);
        }
        viewer.refreshLayers();
        return true;
    }

    void showProvenance(const GisAgentPanel& panel, QWidget* parent)
    {
        const auto& store = panel.provenance();
        QDialog dialog(parent);
        dialog.setWindowTitle(QStringLiteral("Provenance"));
        dialog.resize(760, 560);
        auto* layout = new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel(store.records().isEmpty()
            ? QStringLiteral("No workflow has run yet.")
            : QStringLiteral("%1 executed workflow(s); hash chain %2. Each record holds the normalized plan, "
                             "its validation decision and the SHA-256 of every step output.")
                  .arg(store.records().size())
                  .arg(store.validateChain() ? QStringLiteral("verified") : QStringLiteral("BROKEN")),
            &dialog));
        auto* text = new QPlainTextEdit(QString::fromUtf8(store.exportJson()), &dialog);
        text->setReadOnly(true);
        text->setLineWrapMode(QPlainTextEdit::NoWrap);
        layout->addWidget(text);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);
        dialog.exec();
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("AgentMapAssistant"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico")));

    QMainWindow window;
    window.resize(1360, 820);
    window.setWindowTitle(QStringLiteral("Agent Map Assistant"));

    Demo demo;
    try
    {
        demo = loadDemo();
    }
    catch (const std::exception& error)
    {
        QMessageBox::critical(nullptr, QStringLiteral("Agent Map Assistant"), QString::fromUtf8(error.what()));
        return 1;
    }

    auto* viewer = new GisViewer(&window);
    window.setCentralWidget(viewer);

    auto* panel = new GisAgentPanel(*viewer);
    auto* dock = new QDockWidget(QStringLiteral("Map assistant"), &window);
    dock->setObjectName(QStringLiteral("AgentDock"));
    dock->setFeatures(QDockWidget::DockWidgetMovable);
    dock->setWidget(panel);
    dock->setMinimumWidth(420);
    window.addDockWidget(Qt::RightDockWidgetArea, dock);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    window.addToolBar(toolbar);
    toolbar->addWidget(new QLabel(QStringLiteral(" Try: "), toolbar));
    auto* requests = new QComboBox(toolbar);
    requests->setMinimumWidth(520);
    for (const auto& item : demo.requests)
        requests->addItem(item.request);
    toolbar->addWidget(requests);
    auto* ask = new QPushButton(QStringLiteral("Ask"), toolbar);
    toolbar->addWidget(ask);
    toolbar->addSeparator();
    QAction* fullExtent = toolbar->addAction(QStringLiteral("Full Extent"));
    QAction* provenance = toolbar->addAction(QStringLiteral("Provenance..."));

    auto* plannerLabel = new QLabel(&window);
    window.statusBar()->addPermanentWidget(plannerLabel);

    window.show();
    if (!loadNeighbourhood(*viewer, &window))
        return 1;
    viewer->fullExtent();

    const auto planner = choosePlanner(demo, &window);
    panel->setProposalProvider(planner.provider);
    panel->setGroundingNotes(demo.groundingNotes);
    plannerLabel->setText(planner.description);

    QObject::connect(ask, &QPushButton::clicked, panel, [panel, requests] { panel->submit(requests->currentText()); });
    QObject::connect(fullExtent, &QAction::triggered, viewer, [viewer] { viewer->fullExtent(); });
    QObject::connect(provenance, &QAction::triggered, &window, [panel, &window] { showProvenance(*panel, &window); });
    QObject::connect(panel, &GisAgentPanel::busyChanged, ask, [ask](bool busy) { ask->setEnabled(!busy); });
    QObject::connect(panel, &GisAgentPanel::planReady, &window, [&window](const QString&, const QString&, bool needsConfirmation) {
        window.statusBar()->showMessage(needsConfirmation
            ? QStringLiteral("The plan changes data: review it and click Confirm and run")
            : QStringLiteral("Plan validated: review it and click Run"));
    });
    QObject::connect(panel, &GisAgentPanel::workflowExecuted, &window, [&window](const QString& planId, bool succeeded, const QString& message) {
        window.statusBar()->showMessage(succeeded ? QStringLiteral("Executed %1").arg(planId.left(16)) : message, 8000);
    });
    QObject::connect(panel, &GisAgentPanel::workflowUndone, &window, [&window](const QStringList& steps) {
        window.statusBar()->showMessage(QStringLiteral("Undid %1").arg(steps.join(QStringLiteral(", "))), 8000);
    });

    return app.exec();
}
