// What is in this area? — pick an area of an aerial photo, have it described, mark objects named
// in plain language, and ask questions about it. All application C++ is here.
//
//   Oosterwold 2025 aerial photo (PDOK, 0.5 m, GeoKernel.SampleData)
//     ─► GisVlmAssistant: Florence-2 on this computer describes the area and marks objects;
//                          a remote vision model (optional, GEOKERNEL_LLM_*) answers questions
//     ─► GisVlmPanel:     pick an area on the map, Describe / In detail / Ask / Mark, the answer
#include "ModelDownload.h"
#include "Semantic/GisVlmAssistant.h"
#include "Viewer/GisViewer.h"
#include <QtWidgets>
#include <atomic>

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Viewer::Semantic;
using GeoKernel::Core::Shapes::GisExtent;

// Example areas in EPSG:25831, the CRS of the photo (and of the map): 512 x 512 pixels each.
struct Preset
{
    const char* name;
    const char* note;
    GisExtent area;
};
const Preset Presets[] = {
    {"Houses", "Detached houses with gardens", GisExtent(657883, 5800850, 658139, 5801106)},
    {"Road crossing", "Houses along a road crossing", GisExtent(657683, 5800550, 657939, 5800806)},
    {"Round garden", "A circular building and garden plots", GisExtent(658233, 5800450, 658489, 5800706)},
};

class Window final : public QMainWindow
{
  public:
    GisViewer* viewer = new GisViewer(this);
    std::unique_ptr<GisVlmAssistant> assistant;
    QLabel* status = new QLabel("Preparing the aerial photo (56 MB) and the Florence-2 model (1 GB) on the first run…");
    QProgressBar* progress = new QProgressBar;
    QVector<QPushButton*> presetButtons;
    QVBoxLayout* side = nullptr;
    QJsonObject catalog;
    std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
    QThread* worker = nullptr;
    QString screenshot;  // --screenshot <png>: describe the houses, mark cars, save this window and a .txt summary, quit

    Window()
    {
        resize(1400, 900);
        setWindowTitle("What is in this area? — GeoKernel");
        setWindowIcon(QIcon(":/geokernel.ico"));
        setCentralWidget(viewer);
        viewer->setActiveTool(GisViewerTool::Pan);
        QFile assets(":/vlm-assets.json");
        Sam3Download::require(assets.open(QIODevice::ReadOnly), "Missing pinned asset catalog");
        catalog = QJsonDocument::fromJson(assets.readAll()).object();

        auto dock = new QDockWidget("What is in this area?", this);
        auto body = new QWidget;
        side = new QVBoxLayout(body);
        dock->setWidget(body);
        dock->setMinimumWidth(420);
        addDockWidget(Qt::LeftDockWidgetArea, dock);

        auto presets = new QGroupBox("Example areas");
        auto row = new QHBoxLayout(presets);
        for (int i = 0; i < int(std::size(Presets)); ++i)
        {
            auto button = new QPushButton(Presets[i].name);
            button->setToolTip(Presets[i].note);
            button->setEnabled(false);
            connect(button, &QPushButton::clicked, this, [this, i] { pickPreset(i); });
            row->addWidget(button);
            presetButtons.append(button);
        }
        side->addWidget(presets);
        side->addWidget(progress);
        status->setWordWrap(true);
        side->addWidget(status);
        auto help = new QLabel(
            "Pick an example or turn on \"Pick an area on the map\" and drag a box. Describe and Mark run Florence-2 on this "
            "computer; nothing leaves it. Questions need a remote vision model: set GEOKERNEL_LLM_PROVIDER (anthropic, openai "
            "or ollama), GEOKERNEL_LLM_MODEL and the provider's API key before starting; the area's pixels are then sent to "
            "that provider.\n\nAerial photo: Beeldmateriaal Nederland / PDOK (CC BY 4.0). Florence-2: Microsoft (MIT).");
        help->setWordWrap(true);
        side->addWidget(help);
        prepare();
    }

    static QString cacheRoot()
    {
        return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)).filePath("GeoKernel/examples");
    }

    void prepare()
    {
        auto stop = [flag = cancelled] { return flag->load(); };
        auto report = [this](int value, const QString& text) {
            QMetaObject::invokeMethod(this, [this, value, text] { progress->setValue(value); status->setText(text); }, Qt::QueuedConnection);
        };
        auto photo = std::make_shared<QString>(), model = std::make_shared<QString>(), error = std::make_shared<QString>();
        auto pinned = catalog;
        worker = QThread::create([=] {
            try
            {
                *photo = Sam3Download::ensure(pinned, "series", cacheRoot(), stop, report);
                // GEOKERNEL_FLORENCE2_PACKAGE: an extracted package (florence2.geokernel.json) instead of the download.
                *model = qEnvironmentVariable("GEOKERNEL_FLORENCE2_PACKAGE");
                if (model->isEmpty())
                    *model = Sam3Download::ensure(pinned, "florence2", cacheRoot(), stop, report);
            }
            catch (const std::exception& e)
            {
                *error = QString::fromUtf8(e.what());
            }
        });
        connect(worker, &QThread::finished, this, [this, photo, model, error] {
            worker->deleteLater();
            worker = nullptr;
            if (!error->isEmpty())
                status->setText(*error);
            else
                load(QFileInfo(*photo).dir().filePath("oosterwold_2025-epsg25831-50cm.tif"), *model);
        });
        worker->start();
    }

    void load(const QString& photo, const QString& model)
    {
        QString error;
        if (!viewer->addLayerFromPath(photo, &error))
        {
            status->setText("Cannot open the aerial photo: " + error);
            return;
        }
        assistant = std::make_unique<GisVlmAssistant>(*viewer);
        status->setText("Loading Florence-2…");
        QApplication::processEvents();
        if (!assistant->setRaster(photo, &error) || !assistant->loadLocalModel(model, &error))
        {
            status->setText(error);
            return;
        }
        // A remote vision model only when the user configured one.
        QString remote;
        if (!qEnvironmentVariableIsEmpty("GEOKERNEL_LLM_PROVIDER") && !assistant->setRemoteModel({}, &remote))
            remote = "Remote model not available: " + remote;
        side->insertWidget(2, new GisVlmPanel(*assistant), 1);
        connect(assistant.get(), &GisVlmAssistant::finished, this, [this](const QJsonObject& result) {
            if (!result.contains("error"))
                status->setText(QString("%1 (%2) in %3 s.")
                                    .arg(result.value("kind").toString(), result.value("model").toString())
                                    .arg(result.value("elapsedMs").toDouble() / 1000.0, 0, 'f', 1));
        });
        for (auto* button : presetButtons)
            button->setEnabled(true);
        progress->setValue(100);
        status->setText(assistant->hasRemoteModel() ? "Ready: Florence-2 on this computer, questions go to the remote model."
                                                    : (remote.isEmpty() ? "Ready: Florence-2 on this computer. Set GEOKERNEL_LLM_PROVIDER to ask questions."
                                                                        : remote));
        viewer->setViewExtent(GisExtent(657533, 5800382, 658557, 5801406));
        if (!screenshot.isEmpty())
            QTimer::singleShot(500, this, [this] { runScreenshot(); });
    }

    void pickPreset(int index)
    {
        QString error;
        if (!assistant->setArea(Presets[index].area, &error))
            status->setText(error);
        else
            assistant->zoomToArea();
    }

    // --screenshot: the houses are described and their cars marked; the window and a summary are saved.
    void runScreenshot()
    {
        pickPreset(0);
        QStringList summary;
        assistant->describe();
        assistant->wait(120000);
        summary << "describe: " + assistant->lastResult().value("text").toString();
        assistant->find("car");
        assistant->wait(300000);
        const auto found = assistant->lastResult();
        summary << QString("find car: %1 objects in %2 ms").arg(found.value("objects").toArray().size()).arg(found.value("elapsedMs").toDouble());
        if (assistant->hasRemoteModel())
        {
            assistant->ask("How many houses are in this area?");
            assistant->wait(300000);
            summary << "ask: " + assistant->lastResult().value("text").toString();
        }
        assistant->describe("paragraph");  // the answer shown in the screenshot
        assistant->wait(120000);
        summary << "paragraph: " + assistant->lastResult().value("text").toString();
        QTimer::singleShot(1500, this, [this, summary] {
            grab().save(screenshot);
            QFile text(screenshot + ".txt");
            if (text.open(QIODevice::WriteOnly | QIODevice::Text))
                text.write(summary.join('\n').toUtf8() + '\n');
            close();
        });
    }

  protected:
    void closeEvent(QCloseEvent* event) override
    {
        cancelled->store(true);
        if (worker)
            worker->wait();
        if (assistant)
            assistant->wait(600000);
        event->accept();
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("VlmImageQA");
    Window window;
    if (const auto arguments = QApplication::arguments(); arguments.size() == 3 && arguments[1] == "--screenshot")
        window.screenshot = arguments[2];
    window.show();
    return app.exec();
}
