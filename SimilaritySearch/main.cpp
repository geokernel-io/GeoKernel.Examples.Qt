// Find similar — pick a solar park, a lake, a town or a field and every other place in Flevoland
// is ranked by how much it looks like it. All application C++ is here.
//
//   AlphaEarth embeddings (64 values per 80 m cell, GeoKernel.SampleData)
//     ─► GisSimilaritySearch: cosine similarity of every cell ─► heat-map layer + ranked matches
//     ─► GisSimilarityPanel: click / drag on the map, threshold, match list (click to zoom)
#include "ModelDownload.h"
#include "Semantic/GisSimilaritySearch.h"
#include "Viewer/GisViewer.h"
#include <QtWidgets>
#include <atomic>

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Viewer::Semantic;
using GeoKernel::Core::Shapes::GisExtent;
using GeoKernel::Core::Shapes::GisShapePoint;

// Example places in EPSG:32631, the CRS of the embedding grid (and of the map).
struct Preset
{
    const char* name;
    const char* note;
    GisExtent area;  // a point when min == max
};
const Preset Presets[] = {
    {"Solar park", "Zonnepark Zuyderzon, Almere (OpenStreetMap outline)", GisExtent(652172, 5809772, 652868, 5810511)},
    {"Open water", "Markermeer", GisExtent(652727, 5819032, 652727, 5819032)},
    {"Town centre", "Almere Stad", GisExtent(650788, 5804723, 650788, 5804723)},
    {"Farmland", "Oostelijk Flevoland", GisExtent(683392, 5816743, 683392, 5816743)},
};

class Window final : public QMainWindow
{
  public:
    GisViewer* viewer = new GisViewer(this);
    std::unique_ptr<GisSimilaritySearch> search;
    QLabel* status = new QLabel("Preparing the Flevoland AlphaEarth embeddings (31 MB on the first run)…");
    QProgressBar* progress = new QProgressBar;
    QVector<QPushButton*> presetButtons;
    QVBoxLayout* side = nullptr;
    QJsonObject catalog;
    std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
    QThread* worker = nullptr;
    QString screenshot;  // --screenshot <png>: search the solar park, save this window, quit

    Window()
    {
        resize(1400, 900);
        setWindowTitle("Find similar — GeoKernel");
        setWindowIcon(QIcon(":/geokernel.ico"));
        setCentralWidget(viewer);
        viewer->setActiveTool(GisViewerTool::Pan);
        QFile assets(":/similarity-assets.json");
        Sam3Download::require(assets.open(QIODevice::ReadOnly), "Missing pinned asset catalog");
        catalog = QJsonDocument::fromJson(assets.readAll()).object();

        auto dock = new QDockWidget("Find similar", this);
        auto body = new QWidget;
        side = new QVBoxLayout(body);
        dock->setWidget(body);
        dock->setMinimumWidth(380);
        addDockWidget(Qt::LeftDockWidgetArea, dock);

        auto presets = new QGroupBox("Look for places like…");
        auto grid = new QGridLayout(presets);
        for (int i = 0; i < int(std::size(Presets)); ++i)
        {
            auto button = new QPushButton(Presets[i].name);
            button->setToolTip(Presets[i].note);
            button->setEnabled(false);
            connect(button, &QPushButton::clicked, this, [this, i] { searchPreset(i); });
            grid->addWidget(button, i / 2, i % 2);
            presetButtons.append(button);
        }
        side->addWidget(presets);
        side->addWidget(progress);
        status->setWordWrap(true);
        side->addWidget(status);
        auto help = new QLabel(
            "Or turn on \"Find similar\" below and click a place or drag a box on the map. Every 80 m cell is "
            "compared with it by cosine similarity of its 64-value AlphaEarth embedding; the heat map shows the "
            "most similar cells and the list the best separated matches.\n\n"
            "The AlphaEarth Foundations Satellite Embedding dataset is produced by Google and Google DeepMind "
            "(CC BY 4.0); 2024 layer, 80 m, Source Cooperative mirror, resampled by GeoKernel.");
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
        auto result = std::make_shared<QString>(), error = std::make_shared<QString>();
        auto pinned = catalog;
        worker = QThread::create([=] {
            try
            {
                *result = Sam3Download::ensure(pinned, "embeddings", cacheRoot(), stop, report);
            }
            catch (const std::exception& e)
            {
                *error = QString::fromUtf8(e.what());
            }
        });
        connect(worker, &QThread::finished, this, [this, result, error] {
            worker->deleteLater();
            worker = nullptr;
            if (!error->isEmpty())
                status->setText(*error);
            else
                load(*result);
        });
        worker->start();
    }

    void load(const QString& path)
    {
        // The embedding grid itself is the background: its first three dimensions as false colour.
        QString error;
        if (!viewer->addLayerFromPath(path, &error))
        {
            status->setText("Cannot open the embeddings: " + error);
            return;
        }
        if (auto* background = viewer->mapLayerAt(0))
            background->setOpacity(0.45);  // muted, so the heat map stands out
        search = std::make_unique<GisSimilaritySearch>(*viewer);
        if (!search->loadEmbeddings(path, &error))
        {
            status->setText(error);
            return;
        }
        search->options().maximumResults = 20;
        search->setHeatmapThreshold(0.9);
        side->insertWidget(1, new GisSimilarityPanel(*search));
        connect(search.get(), &GisSimilaritySearch::searched, this, [this](const auto& result, qint64 ms) {
            status->setText(QString("%1 matches among %2 cells in %3 ms (best similarity %4).")
                                .arg(result.matches.size())
                                .arg(result.validCells)
                                .arg(ms)
                                .arg(result.matches.isEmpty() ? 0.0 : result.matches[0].similarity, 0, 'f', 3));
        });
        for (auto* button : presetButtons)
            button->setEnabled(true);
        progress->setValue(100);
        const auto& r = search->embeddings();
        status->setText(QString("Ready: %1 × %2 cells of 80 m, %3 dimensions. Pick an example or click the map.")
                            .arg(r.width)
                            .arg(r.height)
                            .arg(r.dimensions));
        viewer->setViewExtent(r.extent());
        if (!screenshot.isEmpty())
            QTimer::singleShot(500, this, [this] {
                searchPreset(0);
                viewer->setViewExtent(GisExtent(644000, 5800500, 664000, 5815500));  // Almere: the seed park and its neighbours
                QTimer::singleShot(4000, this, [this] {
                    grab().save(screenshot);
                    close();
                });
            });
    }

    void searchPreset(int index)
    {
        const auto& preset = Presets[index];
        try
        {
            if (preset.area.width() > 0)
                search->searchExtent(preset.area);
            else
                search->searchAt(GisShapePoint(preset.area.xMin(), preset.area.yMin()));
            // Show the query place and the ten best matches.
            double x0 = preset.area.xMin(), y0 = preset.area.yMin(), x1 = preset.area.xMax(), y1 = preset.area.yMax();
            const auto& matches = search->lastResult().matches;
            for (int i = 0; i < std::min<int>(10, matches.size()); ++i)
            {
                const auto& e = matches[i].extent;
                x0 = std::min(x0, e[0]), y0 = std::min(y0, e[1]), x1 = std::max(x1, e[2]), y1 = std::max(y1, e[3]);
            }
            const double margin = std::max(x1 - x0, y1 - y0) * 0.15 + 500;
            viewer->setViewExtent(GisExtent(x0 - margin, y0 - margin, x1 + margin, y1 + margin));
        }
        catch (const std::exception& e)
        {
            status->setText(QString::fromUtf8(e.what()));
        }
    }

  protected:
    void closeEvent(QCloseEvent* event) override
    {
        cancelled->store(true);
        if (worker)
            worker->wait();
        event->accept();
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("SimilaritySearch");
    Window window;
    if (const auto arguments = QApplication::arguments(); arguments.size() == 3 && arguments[1] == "--screenshot")
        window.screenshot = arguments[2];
    window.show();
    return app.exec();
}
