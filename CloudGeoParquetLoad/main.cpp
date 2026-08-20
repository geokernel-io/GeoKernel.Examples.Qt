#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDockWidget>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTextEdit>
#include <QThread>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>
#include "CloudObjects.h"
#include "Vector/Parquet/GisLayerParquet.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Cloud;
using namespace GeoKernel::Formats::Vector::Parquet;
using namespace GeoKernel::Viewer;

namespace {
struct Result { QString error; QUrl url; QString path; GeoParquetProbeResult probe; std::unique_ptr<GisLayerParquet> layer; };
void applyCloudOptions() {
    qputenv("GDAL_DISABLE_READDIR_ON_OPEN", "EMPTY_DIR"); qputenv("CPL_VSIL_CURL_ALLOWED_EXTENSIONS", ".parquet,.pmtiles");
    qputenv("GDAL_CACHEMAX", "256"); qputenv("VSI_CACHE", "TRUE"); qputenv("VSI_CACHE_SIZE", "67108864");
    qputenv("GDAL_HTTP_CONNECTTIMEOUT", "10"); qputenv("GDAL_HTTP_TIMEOUT", "30");
}
QString diagnostics(const GeoParquetProbeResult& p) {
    return QStringLiteral("Cloud GeoParquet streaming\n\nURL: %1\nContent length: %2 bytes\nContent type: %3\nAccept-Ranges: %4\nPAR1 header: %5\nPAR1 footer: %6\nGDAL source: /vsicurl/\n\n%7\n\nOnly metadata and requested byte ranges are transferred; the complete GeoParquet file is not downloaded.")
        .arg(p.url.toString()).arg(p.contentLength).arg(QString::fromUtf8(p.contentType))
        .arg(p.acceptsRanges ? "yes" : "no").arg(p.headerValid ? "valid" : "invalid")
        .arg(p.footerValid ? "valid" : "invalid").arg(p.diagnostic);
}
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv); applyCloudOptions();
    QMainWindow window; window.resize(1280, 820); window.setWindowTitle("CloudGeoParquetLoad"); window.setWindowIcon(QIcon(":/icons/geokernel.ico"));
    auto* viewer = new GisViewer(&window); viewer->setActiveTool(GisViewerTool::Pan); window.setCentralWidget(viewer);
    auto* panel = new QWidget(&window); auto* layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel("<b>Cloud GeoParquet streaming</b>", panel));
    layout->addWidget(new QLabel("Remote GeoParquet URL", panel));
    auto* url = new QLineEdit("https://raw.githubusercontent.com/opengeospatial/geoparquet/main/examples/example.parquet", panel); layout->addWidget(url);
    auto* load = new QPushButton("Probe and stream GeoParquet", panel); layout->addWidget(load);
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
        const QUrl remote(url->text().trimmed()); if(!remote.isValid() || remote.scheme().isEmpty()){QMessageBox::warning(&window,"CloudGeoParquetLoad","Enter a valid HTTP or HTTPS URL.");return;}
        load->setEnabled(false); progress->setVisible(true); progress->setValue(10); progress->setFormat("10% — Probing remote object..."); details->setPlainText("Reading the GeoParquet header and footer with HTTP ranges...");
        auto result=std::make_shared<Result>(); result->url=remote; auto* worker=QThread::create([result]{try{GeoParquetSource source; source.rangeReader().setTimeoutMilliseconds(30000); result->probe=source.probe(result->url); result->path=source.gdalVirtualPath(result->url); if(!result->probe.cloudReadable) throw std::runtime_error(result->probe.diagnostic.toStdString());}catch(const std::exception& e){result->error=QString::fromUtf8(e.what());}});
        QObject::connect(worker,&QThread::finished,&window,[&window,viewer,load,progress,details,result,worker]{worker->deleteLater();load->setEnabled(true);if(!result->error.isEmpty()){progress->setValue(0);details->setPlainText("Load failed:\n"+result->error);QMessageBox::critical(&window,"CloudGeoParquetLoad",result->error);return;}try{progress->setValue(60);progress->setFormat("60% — Opening GeoParquet layer...");details->setPlainText(diagnostics(result->probe));QApplication::processEvents();viewer->clearLayers();result->layer=std::make_unique<GisLayerParquet>(result->path);result->layer->setName("Remote GeoParquet");result->layer->open();viewer->addLayer(result->layer);viewer->fullExtent();progress->setValue(100);progress->setFormat("100% — Ready");window.statusBar()->showMessage("GeoParquet is streaming through HTTP byte ranges.");}catch(const std::exception& e){result->error=QString::fromUtf8(e.what());details->setPlainText("Load failed:\n"+result->error);QMessageBox::critical(&window,"CloudGeoParquetLoad",result->error);}}); worker->start();
    });
    window.show(); QMetaObject::invokeMethod(load,&QPushButton::click,Qt::QueuedConnection); return app.exec();
}
