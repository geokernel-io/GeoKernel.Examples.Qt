#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QSize>
#include <QStatusBar>
#include <QTextEdit>
#include <QThread>
#include <QToolBar>
#include <QVBoxLayout>

#include <memory>
#include <algorithm>
#include <vector>

#include "CloudCog.h"
#include "CoordinateSystems/CoordinateSystemFactory.h"
#include "CoordinateSystems/CoordinateTransformer.h"
#include "Raster/Tiff/GisLayerTIFF.h"
#include "Shapes/GisExtent.h"
#include "StacClient.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Cloud;
using namespace GeoKernel::Core::CoordinateSystems;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Formats::Raster::Tiff;
using namespace GeoKernel::Viewer;

namespace
{
    struct AssetResult final
    {
        QString itemId;
        QString tileId;
        QString datetime;
        QString cloudCover;
        QUrl assetUrl;
        CogProbeResult probe;
        QString rasterSummary;
        std::unique_ptr<GisLayerTIFF> layer;
    };

    struct LoadResult final
    {
        QString error;
        QString collection;
        std::vector<AssetResult> assets;
    };

    QString detailsText(const LoadResult& result)
    {
        QStringList lines={QStringLiteral("STAC + COG streaming mosaic"),QString(),QStringLiteral("Catalog: Earth Search v1"),QStringLiteral("Collection: %1").arg(result.collection),QStringLiteral("Unique MGRS tiles: %1").arg(result.assets.size()),QString()};
        for(const auto& asset:result.assets)
        {
            lines<<QStringLiteral("%1 | %2").arg(asset.tileId,asset.itemId)
                 <<QStringLiteral("Date/time: %1 | Cloud cover: %2").arg(asset.datetime,asset.cloudCover)
                 <<QStringLiteral("Content: %1 bytes | Range: %2 | IFD: %3").arg(asset.probe.contentLength).arg(asset.probe.acceptsRanges?QStringLiteral("yes"):QStringLiteral("no")).arg(asset.probe.firstIfdOffset)
                 <<asset.rasterSummary<<QString();
        }
        lines<<QStringLiteral("Only metadata and visible ranges are transferred; complete COG files are not downloaded.");return lines.join('\n');
    }

    void applyGdalCogOptions()
    {
        CogSource source;
        const auto options=source.recommendedGdalOptions();
        for(auto it=options.cbegin();it!=options.cend();++it)qputenv(it.key(),it.value());
        qputenv("GDAL_HTTP_CONNECTTIMEOUT","10");
        qputenv("GDAL_HTTP_TIMEOUT","15");
        qputenv("GDAL_HTTP_MAX_RETRY","1");
        qputenv("GDAL_HTTP_RETRY_DELAY","1");
    }

    void reportPhase(const QPointer<QMainWindow>& window,const QPointer<QTextEdit>& details,const QPointer<QProgressBar>& progress,const QString& text,int value)
    {
        if(!window)return;
        QMetaObject::invokeMethod(window,[window,details,progress,text,value]
        {
            if(!window)return;
            if(details)details->setPlainText(text);
            if(progress){progress->setVisible(true);progress->setRange(0,100);progress->setValue(value);progress->setFormat(QStringLiteral("%1% — %2").arg(value).arg(text));}
            window->statusBar()->showMessage(text);
        },Qt::QueuedConnection);
    }
}

int main(int argc,char* argv[])
{
    QApplication app(argc,argv);applyGdalCogOptions();
    QMainWindow window;window.resize(1280,820);window.setWindowTitle(QStringLiteral("StacCogLoad"));window.setWindowIcon(QIcon(QStringLiteral(":/icons/geokernel.ico")));
    auto* viewer=new GisViewer(&window);viewer->setActiveTool(GisViewerTool::Pan);window.setCentralWidget(viewer);

    auto* panel=new QWidget(&window);auto* panelLayout=new QVBoxLayout(panel);
    auto* title=new QLabel(QStringLiteral("<b>STAC COG streaming</b>"),panel);panelLayout->addWidget(title);
    auto* form=new QFormLayout();auto* catalog=new QLineEdit(QStringLiteral("https://earth-search.aws.element84.com/v1"),panel);catalog->setReadOnly(true);
    auto* collection=new QComboBox(panel);collection->addItem(QStringLiteral("Sentinel-2 L2A"),QStringLiteral("sentinel-2-l2a"));
    auto* bbox=new QLineEdit(QStringLiteral("18.00, 59.25, 18.20, 59.40"),panel);bbox->setPlaceholderText(QStringLiteral("xmin, ymin, xmax, ymax"));form->addRow(QStringLiteral("Catalog"),catalog);form->addRow(QStringLiteral("Collection"),collection);form->addRow(QStringLiteral("BBOX"),bbox);panelLayout->addLayout(form);
    auto* loadButton=new QPushButton(QStringLiteral("Search STAC and stream visual COG"),panel);panelLayout->addWidget(loadButton);
    auto* progress=new QProgressBar(panel);progress->setRange(0,100);progress->setValue(0);progress->setTextVisible(true);progress->setVisible(false);panelLayout->addWidget(progress);
    auto* itemList=new QListWidget(panel);itemList->setMaximumHeight(90);panelLayout->addWidget(new QLabel(QStringLiteral("Selected STAC item"),panel));panelLayout->addWidget(itemList);
    auto* details=new QTextEdit(panel);details->setReadOnly(true);details->setPlainText(QStringLiteral("Ready. Search the STAC catalog to select a COG asset."));panelLayout->addWidget(new QLabel(QStringLiteral("Cloud diagnostics"),panel));panelLayout->addWidget(details,1);
    auto* dock=new QDockWidget(QStringLiteral("Cloud-native raster"),&window);dock->setMinimumWidth(390);dock->setWidget(panel);window.addDockWidget(Qt::RightDockWidgetArea,dock);

    auto* toolbar=new QToolBar(&window);toolbar->setMovable(false);toolbar->setIconSize(QSize(32,32));window.addToolBar(toolbar);
    auto* zoomIn=toolbar->addAction(QIcon(QStringLiteral(":/icons/zoom-in.svg")),QStringLiteral("Zoom In"));auto* zoomOut=toolbar->addAction(QIcon(QStringLiteral(":/icons/zoom-out.svg")),QStringLiteral("Zoom Out"));auto* fullExtent=toolbar->addAction(QIcon(QStringLiteral(":/icons/full-extent.svg")),QStringLiteral("Full Extent"));toolbar->addSeparator();QActionGroup tools(&window);tools.setExclusive(true);auto* zoomBox=toolbar->addAction(QIcon(QStringLiteral(":/icons/zoom-box.svg")),QStringLiteral("Zoom Rect"));zoomBox->setCheckable(true);tools.addAction(zoomBox);auto* pan=toolbar->addAction(QIcon(QStringLiteral(":/icons/pan.svg")),QStringLiteral("Pan"));pan->setCheckable(true);pan->setChecked(true);tools.addAction(pan);
    QObject::connect(zoomIn,&QAction::triggered,viewer,&GisViewer::zoomIn);QObject::connect(zoomOut,&QAction::triggered,viewer,&GisViewer::zoomOut);QObject::connect(fullExtent,&QAction::triggered,viewer,&GisViewer::fullExtent);QObject::connect(zoomBox,&QAction::triggered,viewer,[viewer]{viewer->setActiveTool(GisViewerTool::ZoomBox);});QObject::connect(pan,&QAction::triggered,viewer,[viewer]{viewer->setActiveTool(GisViewerTool::Pan);});
    QObject::connect(viewer,&GisViewer::drawingProgressChanged,&window,[progress,loadButton](int value,const QString& text)
    {
        if(!loadButton->isEnabled())return;
        progress->setVisible(true);progress->setRange(0,100);progress->setValue(std::clamp(value,0,100));
        const QString label=text.contains(QStringLiteral("%p%"))?QStringLiteral("Rendering map..."):text;
        progress->setFormat(QStringLiteral("%1% — %2").arg(std::clamp(value,0,100)).arg(label));
    });
    QObject::connect(viewer,&GisViewer::busyChanged,&window,[progress,loadButton](bool busy)
    {
        if(!loadButton->isEnabled())return;
        if(busy){progress->setVisible(true);if(progress->value()>=100)progress->setValue(0);progress->setFormat(QStringLiteral("%p% — Rendering map..."));}
        else{progress->setValue(100);progress->setFormat(QStringLiteral("100% — Map ready"));}
    });

    QObject::connect(loadButton,&QPushButton::clicked,&window,[&window,viewer,loadButton,itemList,details,progress,collection,bbox]
    {
        const QStringList bboxParts=bbox->text().split(',',Qt::SkipEmptyParts);QVector<double> bboxValues;bool bboxValid=bboxParts.size()==4;
        for(const QString& part:bboxParts){bool ok=false;const double value=part.trimmed().toDouble(&ok);if(!ok){bboxValid=false;break;}bboxValues.append(value);}
        bboxValid=bboxValid&&bboxValues.size()==4&&bboxValues[0]<bboxValues[2]&&bboxValues[1]<bboxValues[3]&&bboxValues[0]>=-180.0&&bboxValues[2]<=180.0&&bboxValues[1]>=-90.0&&bboxValues[3]<=90.0;
        if(!bboxValid){QMessageBox::warning(&window,QStringLiteral("StacCogLoad"),QStringLiteral("Enter a valid WGS84 BBOX as:\nxmin, ymin, xmax, ymax"));bbox->setFocus();bbox->selectAll();return;}
        loadButton->setEnabled(false);progress->setVisible(true);progress->setValue(2);progress->setFormat(QStringLiteral("2% — Starting..."));itemList->clear();details->setPlainText(QStringLiteral("Searching STAC catalog..."));window.statusBar()->showMessage(QStringLiteral("Searching STAC and probing the visual COG..."));
        auto result=std::make_shared<LoadResult>();const QString collectionId=collection->currentData().toString();QPointer<QMainWindow> safeWindow(&window);QPointer<QTextEdit> safeDetails(details);QPointer<QProgressBar> safeProgress(progress);
        auto* worker=QThread::create([result,collectionId,bboxValues,safeWindow,safeDetails,safeProgress]
        {
            try
            {
                reportPhase(safeWindow,safeDetails,safeProgress,QStringLiteral("Searching the Earth Search STAC catalog..."),10);
                auto cache=std::make_shared<CloudRangeCache>();StacClient client(QUrl(QStringLiteral("https://earth-search.aws.element84.com/v1")),cache);client.setTimeoutMilliseconds(15000);StacSearchRequest request;request.collections={collectionId};request.bbox=bboxValues;request.datetime=QStringLiteral("2024-01-01T00:00:00Z/..");request.limit=100;request.query={{QStringLiteral("eo:cloud_cover"),QJsonObject{{QStringLiteral("lt"),20}}}};
                const auto search=client.search(request);reportPhase(safeWindow,safeDetails,safeProgress,QStringLiteral("Selecting one recent scene per MGRS tile..."),25);QSet<QString> selectedTiles;result->collection=collectionId;
                for(const auto& item:search.items)
                {
                    if(!item.assets.contains(QStringLiteral("visual")))continue;const auto& properties=item.properties;const QString tileId=QStringLiteral("%1%2%3").arg(properties.value(QStringLiteral("mgrs:utm_zone")).toInt()).arg(properties.value(QStringLiteral("mgrs:latitude_band")).toString(),properties.value(QStringLiteral("mgrs:grid_square")).toString());if(tileId.size()<4||selectedTiles.contains(tileId))continue;selectedTiles.insert(tileId);AssetResult asset;asset.itemId=item.id;asset.tileId=tileId;asset.datetime=properties.value(QStringLiteral("datetime")).toString();asset.cloudCover=QString::number(properties.value(QStringLiteral("eo:cloud_cover")).toDouble(),'f',1)+QStringLiteral("%");asset.assetUrl=item.assets.value(QStringLiteral("visual")).href;result->assets.push_back(std::move(asset));if(result->assets.size()>=16)break;
                }
                if(result->assets.empty())throw std::runtime_error("STAC search returned no visual COG assets");CogSource cog(cache);cog.rangeReader().setTimeoutMilliseconds(15000);
                for(std::size_t index=0;index<result->assets.size();++index){const int value=30+static_cast<int>((index+1)*35/result->assets.size());reportPhase(safeWindow,safeDetails,safeProgress,QStringLiteral("Probing COG tile %1 of %2...").arg(index+1).arg(result->assets.size()),value);auto& asset=result->assets[index];asset.probe=cog.probe(asset.assetUrl);if(!asset.probe.cloudReadable)throw std::runtime_error(QStringLiteral("%1: %2").arg(asset.tileId,asset.probe.diagnostic).toStdString());}
                reportPhase(safeWindow,safeDetails,safeProgress,QStringLiteral("%1 COG tiles verified. Preparing Viewer layers...").arg(result->assets.size()),68);
            }
            catch(const std::exception& ex){result->error=QString::fromUtf8(ex.what());}
        });
        QObject::connect(worker,&QThread::finished,&window,[&window,viewer,loadButton,itemList,details,progress,result,worker,bboxValues]
        {
            worker->deleteLater();loadButton->setEnabled(true);
            if(!result->error.isEmpty()){progress->setValue(0);progress->setFormat(QStringLiteral("Load failed"));details->setPlainText(QStringLiteral("Load failed:\n%1").arg(result->error));window.statusBar()->showMessage(QStringLiteral("STAC COG load failed."));QMessageBox::critical(&window,QStringLiteral("StacCogLoad"),result->error);return;}
            details->setPlainText(QStringLiteral("Opening COG metadata on the Viewer thread..."));window.statusBar()->showMessage(QStringLiteral("Opening COG metadata on the Viewer thread..."));QApplication::processEvents();
            try
            {
                viewer->clearLayers();CogSource cog;
                for(std::size_t index=0;index<result->assets.size();++index)
                {
                    auto& asset=result->assets[index];const int value=70+static_cast<int>((index+1)*25/result->assets.size());progress->setValue(value);progress->setFormat(QStringLiteral("%1% — Opening COG tile %2 of %3...").arg(value).arg(index+1).arg(result->assets.size()));details->setPlainText(QStringLiteral("Opening COG tile %1 of %2 on the Viewer thread...").arg(index+1).arg(result->assets.size()));QApplication::processEvents();asset.layer=std::make_unique<GisLayerTIFF>(cog.gdalVirtualPath(asset.assetUrl));asset.layer->setName(QStringLiteral("%1 | %2").arg(asset.tileId,asset.itemId));asset.layer->open();const auto& metadata=asset.layer->metadata();asset.rasterSummary=QStringLiteral("GDAL: %1 | %2 x %3 | EPSG:%4 | %5 overviews").arg(metadata.driverName).arg(metadata.width).arg(metadata.height).arg(metadata.epsgCode).arg(metadata.overviews.size());itemList->addItem(QStringLiteral("%1 | %2 | cloud %3").arg(asset.tileId,asset.datetime,asset.cloudCover));viewer->addLayer(asset.layer);
                }
                details->setPlainText(detailsText(*result));
                const auto wgs84=CoordinateSystemFactory::fromEpsg(4326);
                CoordinateTransformer toViewer(*wgs84,*viewer->coordinateSystem());
                const GisShapePoint corners[]={
                    toViewer.transform(GisShapePoint(bboxValues[0],bboxValues[1])),
                    toViewer.transform(GisShapePoint(bboxValues[0],bboxValues[3])),
                    toViewer.transform(GisShapePoint(bboxValues[2],bboxValues[1])),
                    toViewer.transform(GisShapePoint(bboxValues[2],bboxValues[3]))};
                double xMin=corners[0].x(),xMax=corners[0].x(),yMin=corners[0].y(),yMax=corners[0].y();
                for(const auto& corner:corners){xMin=std::min(xMin,corner.x());xMax=std::max(xMax,corner.x());yMin=std::min(yMin,corner.y());yMax=std::max(yMax,corner.y());}
                const double padding=0.04;viewer->setViewExtent(GisExtent(xMin,yMin,xMax,yMax).inflate((xMax-xMin)*padding,(yMax-yMin)*padding));
                progress->setValue(100);progress->setFormat(QStringLiteral("100% — Ready"));window.statusBar()->showMessage(QStringLiteral("%1 visual COG tiles are streaming through HTTP byte ranges.").arg(result->assets.size()));
            }
            catch(const std::exception& ex)
            {
                result->error=QString::fromUtf8(ex.what());progress->setValue(0);progress->setFormat(QStringLiteral("Load failed"));details->setPlainText(QStringLiteral("Load failed:\n%1").arg(result->error));window.statusBar()->showMessage(QStringLiteral("STAC COG load failed."));QMessageBox::critical(&window,QStringLiteral("StacCogLoad"),result->error);
            }
        });
        worker->start();
    });

    window.show();QMetaObject::invokeMethod(loadButton,&QPushButton::click,Qt::QueuedConnection);return app.exec();
}
