#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTextEdit>
#include <QThread>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>
#include <vector>
#include "CloudObjects.h"
#include "Vector/PMTiles/GisLayerPMTiles.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Cloud;
using namespace GeoKernel::Formats::Vector::PMTiles;
using namespace GeoKernel::Viewer;

namespace {
struct Result { QString error; QUrl url; QString path; PmTilesProbeResult probe; std::vector<std::unique_ptr<GisLayerPMTiles>> layers; };
void applyCloudOptions() {
    qputenv("GDAL_DISABLE_READDIR_ON_OPEN", "EMPTY_DIR"); qputenv("CPL_VSIL_CURL_ALLOWED_EXTENSIONS", ".parquet,.pmtiles");
    qputenv("GDAL_CACHEMAX", "256"); qputenv("VSI_CACHE", "TRUE"); qputenv("VSI_CACHE_SIZE", "67108864");
    qputenv("CPL_VSIL_CURL_CHUNK_SIZE", "1048576"); qputenv("CPL_VSIL_CURL_CACHE_SIZE", "67108864");
    qputenv("GDAL_HTTP_MULTIRANGE", "YES"); qputenv("GDAL_HTTP_MERGE_CONSECUTIVE_RANGES", "YES");
    qputenv("GDAL_HTTP_CONNECTTIMEOUT", "10"); qputenv("GDAL_HTTP_TIMEOUT", "30");
}
QString diagnostics(const PmTilesProbeResult& p) {
    return QStringLiteral("Cloud PMTiles streaming\n\nURL: %1\nContent length: %2 bytes\nContent type: %3\nAccept-Ranges: %4\nPMTiles header: %5\nSpecification: v%6\nZoom range: %7-%8\nRoot directory: %9 bytes\nGDAL source: /vsicurl/\n\n%10\n\nOnly metadata and requested byte ranges are transferred; the complete PMTiles archive is not downloaded.")
        .arg(p.url.toString()).arg(p.contentLength).arg(QString::fromUtf8(p.contentType))
        .arg(p.acceptsRanges ? "yes" : "no").arg(p.headerValid ? "valid" : "invalid")
        .arg(p.specificationVersion).arg(p.minimumZoom).arg(p.maximumZoom).arg(p.rootDirectoryLength).arg(p.diagnostic);
}

void applyBasemapStyle(GisLayerPMTiles& layer, const QString& sourceName) {
    auto& style = layer.style();
    const QString name = sourceName.toCaseFolded();

    if (name == "earth") { style.setFillColor("#f1eee8"); style.setFillOpacity(255); style.setLineWidth(0.0f); }
    else if (name == "landcover") { style.setFillColor("#dce8d5"); style.setFillOpacity(255); style.setLineWidth(0.0f); }
    else if (name == "landuse") { style.setFillColor("#e7e1d5"); style.setFillOpacity(255); style.setLineWidth(0.0f); }
    else if (name == "water") { style.setFillColor("#b9d9eb"); style.setFillOpacity(255); style.setLineColor("#9bc6df"); style.setLineWidth(0.35f); }
    else if (name == "buildings") { style.setFillColor("#d4ccc2"); style.setFillOpacity(255); style.setLineColor("#b8aea3"); style.setLineWidth(0.25f); }
    else if (name == "roads") { style.setLineColor("#ffffff"); style.setLineWidth(1.15f); style.setFillOpacity(0); }
    else if (name == "transit") { style.setLineColor("#d28a54"); style.setLineWidth(1.0f); style.setFillOpacity(0); }
    else if (name == "boundaries") { style.setLineColor("#9a8f84"); style.setLineWidth(0.55f); style.setFillOpacity(0); }
    else if (name == "physical_line") { style.setLineColor("#91a69a"); style.setLineWidth(0.5f); style.setFillOpacity(0); }
    else if (name == "natural") { style.setFillColor("#cfe3c4"); style.setFillOpacity(255); style.setLineColor("#9fbea0"); style.setLineWidth(0.25f); }
    else { style.setPointColor("#557f9b"); style.setPointSize(2.5f); style.setLineWidth(0.3f); }
}
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv); applyCloudOptions();
    QMainWindow window; window.resize(1280, 820); window.setWindowTitle("CloudPmTilesLoad"); window.setWindowIcon(QIcon(":/icons/geokernel.ico"));
    auto* viewer = new GisViewer(&window); viewer->setActiveTool(GisViewerTool::Pan); window.setCentralWidget(viewer);
    const QString diagnosticDirectory = QCoreApplication::applicationDirPath();
    const QString renderLogPath = QDir(diagnosticDirectory).filePath("pmtiles-render.log");
    const QString loadLogPath = QDir(diagnosticDirectory).filePath("pmtiles-load.log");
    QFile::remove(renderLogPath); QFile::remove(loadLogPath);
    viewer->setRenderMetricsLogPath(renderLogPath);
    viewer->setRenderStepLogPath(renderLogPath);
    viewer->setVerboseMode(true);
    GisLayerPMTiles::setLoadMetricsLogPath(loadLogPath);
    GisLayerPMTiles::setLoadMetricsLogEnabled(true);
    auto* panel = new QWidget(&window); auto* layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel("<b>Cloud PMTiles streaming</b>", panel));
    layout->addWidget(new QLabel("Remote PMTiles URL", panel));
    auto* url = new QLineEdit("https://pmtiles.io/protomaps(vector)ODbL_firenze.pmtiles", panel); layout->addWidget(url);
    auto* load = new QPushButton("Probe and stream PMTiles", panel); layout->addWidget(load);
    auto* progress = new QProgressBar(panel); progress->setRange(0, 100); progress->setVisible(false); layout->addWidget(progress);
    auto* details = new QTextEdit(panel); details->setReadOnly(true); details->setPlainText("Ready."); layout->addWidget(new QLabel("Cloud diagnostics", panel)); layout->addWidget(details, 1);
    auto* dock = new QDockWidget("Cloud-native vector", &window); dock->setMinimumWidth(390); dock->setWidget(panel); window.addDockWidget(Qt::RightDockWidgetArea, dock);
    auto* toolbar = new QToolBar(&window); toolbar->setMovable(false); toolbar->setIconSize(QSize(32,32)); window.addToolBar(toolbar);
    auto* zi=toolbar->addAction(QIcon(":/icons/zoom-in.svg"),"Zoom In"); auto* zo=toolbar->addAction(QIcon(":/icons/zoom-out.svg"),"Zoom Out"); auto* fe=toolbar->addAction(QIcon(":/icons/full-extent.svg"),"Full Extent"); toolbar->addSeparator();
    QActionGroup tools(&window); tools.setExclusive(true); auto* zb=toolbar->addAction(QIcon(":/icons/zoom-box.svg"),"Zoom Rect"); zb->setCheckable(true); tools.addAction(zb); auto* pan=toolbar->addAction(QIcon(":/icons/pan.svg"),"Pan"); pan->setCheckable(true); pan->setChecked(true); tools.addAction(pan);
    QObject::connect(zi,&QAction::triggered,viewer,&GisViewer::zoomIn); QObject::connect(zo,&QAction::triggered,viewer,&GisViewer::zoomOut); QObject::connect(fe,&QAction::triggered,viewer,&GisViewer::fullExtent); QObject::connect(zb,&QAction::triggered,viewer,[viewer]{viewer->setActiveTool(GisViewerTool::ZoomBox);}); QObject::connect(pan,&QAction::triggered,viewer,[viewer]{viewer->setActiveTool(GisViewerTool::Pan);});
    QObject::connect(viewer,&GisViewer::drawingProgressChanged,&window,[progress](int value,const QString& text){progress->setVisible(true); progress->setValue(std::clamp(value,0,100)); progress->setFormat(QStringLiteral("%1% — %2").arg(std::clamp(value,0,100)).arg(text));});
    QObject::connect(viewer,&GisViewer::busyChanged,&window,[progress](bool busy){if(busy){progress->setVisible(true);progress->setFormat("%p% — Rendering map...");}else{progress->setValue(100);progress->setFormat("100% — Map ready");}});
    QObject::connect(load,&QPushButton::clicked,&window,[&window,viewer,url,load,progress,details]{
        const QUrl remote(url->text().trimmed()); if(!remote.isValid() || remote.scheme().isEmpty()){QMessageBox::warning(&window,"CloudPmTilesLoad","Enter a valid HTTP or HTTPS URL.");return;}
        load->setEnabled(false); progress->setVisible(true); progress->setValue(10); progress->setFormat("10% — Probing remote object..."); details->setPlainText("Reading the PMTiles v3 header with an HTTP range...");
        auto result=std::make_shared<Result>(); result->url=remote; QPointer<QProgressBar> safeProgress(progress); QPointer<QTextEdit> safeDetails(details);
        auto* worker=QThread::create([result,safeProgress,safeDetails]{try{PmTilesSource source; source.rangeReader().setTimeoutMilliseconds(30000); result->probe=source.probe(result->url); result->path=source.gdalVirtualPath(result->url); if(!result->probe.cloudReadable) throw std::runtime_error(result->probe.diagnostic.toStdString()); QMetaObject::invokeMethod(safeProgress,[safeProgress,safeDetails,result]{if(!safeProgress)return;safeProgress->setValue(35);safeProgress->setFormat("35% — Discovering PMTiles source layers...");if(safeDetails)safeDetails->setPlainText(diagnostics(result->probe));},Qt::QueuedConnection); const auto sourceLayers=GisLayerPMTiles::sourceLayers(result->path); if(sourceLayers.isEmpty()) throw std::runtime_error("PMTiles contains no drawable source layers."); int completed=0; for(const auto& sourceLayer:sourceLayers){auto layer=std::make_unique<GisLayerPMTiles>(result->path);layer->setSourceLayerIndex(sourceLayer.index);layer->setName(sourceLayer.name);applyBasemapStyle(*layer,sourceLayer.name);layer->open();result->layers.push_back(std::move(layer));completed++;const int mapped=35+completed*55/sourceLayers.size();QMetaObject::invokeMethod(safeProgress,[safeProgress,mapped,sourceLayer]{if(!safeProgress)return;safeProgress->setValue(mapped);safeProgress->setFormat(QStringLiteral("%1% — Opening %2...").arg(mapped).arg(sourceLayer.name));},Qt::QueuedConnection);}}catch(const std::exception& e){result->error=QString::fromUtf8(e.what());}});
        QObject::connect(worker,&QThread::finished,&window,[&window,viewer,load,progress,details,result,worker]{worker->deleteLater();load->setEnabled(true);if(!result->error.isEmpty()){progress->setValue(0);details->setPlainText("Load failed:\n"+result->error);QMessageBox::critical(&window,"CloudPmTilesLoad",result->error);return;}try{progress->setValue(92);progress->setFormat("92% — Adding PMTiles source layers to Viewer...");viewer->clearLayers();for(auto& layer:result->layers)viewer->addLayer(layer);progress->setValue(100);progress->setFormat("100% — Ready");window.statusBar()->showMessage(QStringLiteral("%1 PMTiles source layers are streaming through HTTP byte ranges.").arg(result->layers.size()));}catch(const std::exception& e){result->error=QString::fromUtf8(e.what());details->setPlainText("Load failed:\n"+result->error);QMessageBox::critical(&window,"CloudPmTilesLoad",result->error);}}); worker->start();
    });
    window.show(); QMetaObject::invokeMethod(load,&QPushButton::click,Qt::QueuedConnection); return app.exec();
}
