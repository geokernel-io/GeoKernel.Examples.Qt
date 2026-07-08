#include <QApplication>
#include <QCoreApplication>
#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSize>
#include <QSplitter>
#include <QSyntaxHighlighter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QTextCharFormat>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <array>
#include <memory>
#include <functional>

#include "Controls/MiniMap/GisMiniMap.h"
#include "Controls/ScaleBar/GisScaleBar.h"
#include "Layers/Defs/GisAttributeDefinition.h"
#include "Layers/Defs/GisAttributeType.h"
#include "Layers/GisLayer.h"
#include "Layers/GisLayerVector.h"
#include "Loading/LayerLoadOptions.h"
#include "FeatureSources/SpatialIndexPreparationState.h"
#include "Shapes/GisExtent.h"
#include "Shapes/GisShapePoint.h"
#include "Shapes/GisShapePolygon.h"
#include "Shapes/GisShapePolyline.h"
#include "Symbology/GisColorRampRegistry.h"
#include "Symbology/GisRuleBasedSymbolRenderer.h"
#include "Symbology/GisSymbolRendererFactory.h"
#include "Viewer/GisViewer.h"

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Viewer::Controls::Minimap;
using namespace GeoKernel::Viewer::Controls::ScaleBar;
using namespace GeoKernel::Viewer::FeatureSources;
using namespace GeoKernel::Viewer::Loading;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Layers::Defs;
using namespace GeoKernel::Core::Shapes;
using namespace GeoKernel::Core::Symbology;

enum class SampleId
{
    HelloMap,
    AddLayers,
    Project,
    Scalebar,
    Minimap,
    AddPointInteractive,
    AddPolylineInteractive,
    AddPolygonInteractive,
    AddPointProgrammatic,
    AddPolylineProgrammatic,
    AddPolygonProgrammatic,
    DeleteFeature,
    MoveFeatureTool,
    EditSession,
    LayerAddRemove,
    LayerReorder,
    LayerExtent,
    LayerEvents,
    InMemoryLayers,
    LayerRefresh,
    LayerLoadCancel,
    LayerLoadOptions,
    LayerZoomTo,
    LayerVisibility,
    CategorizedRenderer,
    ClearRenderer,
    GraduatedRenderer,
    GraduatedRendererSize,
    ClassificationMethods,
    RuleBasedRenderer,
    SimpleStyle,
    SelectionStyle,
    StylePerFeature,
    LabelCollisionOff,
    ShapefileLoad,
    ShapefileSaveAs,
    TabLoad,
    MifLoad,
    GeoPackageLoad,
    KmlLoad,
    DxfLoad,
    OpenStreetMap,
    GeoTiffLoad,
    RasterWorldTransform,
    RasterOverview,
    RasterTileCache,
    XyzPresets,
    XyzCustomUrl,
    XyzLocalCache,
    XyzTileSize,
    XyzMinMaxZoom,
    XyzAttribution,
    XyzDiagnostics,
    WktOverlay,
    WktRoundtrip
};

QString repositoryPath(const QString& relativePath)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../%1").arg(relativePath)));
}

QString readTextFile(const QString& relativePath)
{
    QFile file(repositoryPath(relativePath));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("Could not read source file:\n%1").arg(file.fileName());

    return QString::fromUtf8(file.readAll());
}

class CodeHighlighter final : public QSyntaxHighlighter
{
public:
    explicit CodeHighlighter(QTextDocument* document, const QString& language) :
        QSyntaxHighlighter(document)
    {
        QTextCharFormat keywordFormat;
        keywordFormat.setForeground(QColor(0, 86, 145));
        keywordFormat.setFontWeight(QFont::Bold);

        QTextCharFormat stringFormat;
        stringFormat.setForeground(QColor(163, 21, 21));

        QTextCharFormat commentFormat;
        commentFormat.setForeground(QColor(0, 128, 0));

        QTextCharFormat typeFormat;
        typeFormat.setForeground(QColor(43, 145, 175));

        const QStringList commonKeywords =
        {
            QStringLiteral("class"), QStringLiteral("const"), QStringLiteral("false"), QStringLiteral("if"),
            QStringLiteral("namespace"), QStringLiteral("new"), QStringLiteral("null"), QStringLiteral("return"),
            QStringLiteral("true"), QStringLiteral("using"), QStringLiteral("void")
        };

        QStringList keywords = commonKeywords;
        if (language == QStringLiteral("cpp"))
        {
            keywords << QStringLiteral("auto") << QStringLiteral("bool") << QStringLiteral("for")
                     << QStringLiteral("include") << QStringLiteral("int") << QStringLiteral("nullptr")
                     << QStringLiteral("public") << QStringLiteral("static");
        }
        else if (language == QStringLiteral("csharp"))
        {
            keywords << QStringLiteral("private") << QStringLiteral("public") << QStringLiteral("sealed")
                     << QStringLiteral("string") << QStringLiteral("var");
        }
        else
        {
            keywords << QStringLiteral("Grid") << QStringLiteral("Window") << QStringLiteral("Button")
                     << QStringLiteral("Image") << QStringLiteral("Style") << QStringLiteral("Setter");
        }

        for (const QString& keyword : keywords)
            m_rules.push_back({ QRegularExpression(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(keyword))), keywordFormat });

        m_rules.push_back({ QRegularExpression(QStringLiteral("\"[^\"\\n]*\"")), stringFormat });
        m_rules.push_back({ QRegularExpression(QStringLiteral("//[^\\n]*")), commentFormat });
        m_rules.push_back({ QRegularExpression(QStringLiteral("<!--[^>]*-->")), commentFormat });
        m_rules.push_back({ QRegularExpression(QStringLiteral("\\b[A-Z][A-Za-z0-9_]*\\b")), typeFormat });
    }

protected:
    void highlightBlock(const QString& text) override
    {
        for (const HighlightRule& rule : m_rules)
        {
            auto match = rule.pattern.globalMatch(text);
            while (match.hasNext())
            {
                const QRegularExpressionMatch result = match.next();
                setFormat(result.capturedStart(), result.capturedLength(), rule.format);
            }
        }
    }

private:
    struct HighlightRule
    {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    QVector<HighlightRule> m_rules;
};

QPlainTextEdit* createSourceView(const QString& text, const QString& language)
{
    auto* editor = new QPlainTextEdit();
    editor->setReadOnly(true);
    editor->setPlainText(text);
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    editor->setFont(QFont(QStringLiteral("Cascadia Mono"), 10));
    editor->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: #fbfbfb; border: none; padding: 8px; }"));
    new CodeHighlighter(editor->document(), language);
    return editor;
}

QString sampleTitle(SampleId sample)
{
    switch (sample)
    {
    case SampleId::AddLayers:
        return QStringLiteral("AddLayers");
    case SampleId::Project:
        return QStringLiteral("Project");
    case SampleId::Scalebar:
        return QStringLiteral("Scalebar");
    case SampleId::Minimap:
        return QStringLiteral("MiniMap");
    case SampleId::AddPointInteractive:
        return QStringLiteral("AddPointInteractive");
    case SampleId::AddPolylineInteractive:
        return QStringLiteral("AddPolylineInteractive");
    case SampleId::AddPolygonInteractive:
        return QStringLiteral("AddPolygonInteractive");
    case SampleId::AddPointProgrammatic:
        return QStringLiteral("AddPointProgrammatic");
    case SampleId::AddPolylineProgrammatic:
        return QStringLiteral("AddPolylineProgrammatic");
    case SampleId::AddPolygonProgrammatic:
        return QStringLiteral("AddPolygonProgrammatic");
    case SampleId::DeleteFeature:
        return QStringLiteral("DeleteFeature");
    case SampleId::MoveFeatureTool:
        return QStringLiteral("MoveFeatureTool");
    case SampleId::EditSession:
        return QStringLiteral("EditSession");
    case SampleId::LayerAddRemove:
        return QStringLiteral("LayerAddRemove");
    case SampleId::LayerReorder:
        return QStringLiteral("LayerReorder");
    case SampleId::LayerExtent:
        return QStringLiteral("LayerExtent");
    case SampleId::LayerEvents:
        return QStringLiteral("LayerEvents");
    case SampleId::InMemoryLayers:
        return QStringLiteral("InMemoryLayers");
    case SampleId::LayerRefresh:
        return QStringLiteral("LayerRefresh");
    case SampleId::LayerLoadCancel:
        return QStringLiteral("LayerLoadCancel");
    case SampleId::LayerLoadOptions:
        return QStringLiteral("LayerLoadOptions");
    case SampleId::LayerZoomTo:
        return QStringLiteral("LayerZoomTo");
    case SampleId::LayerVisibility:
        return QStringLiteral("LayerVisibility");
    case SampleId::CategorizedRenderer:
        return QStringLiteral("CategorizedRenderer");
    case SampleId::ClearRenderer:
        return QStringLiteral("ClearRenderer");
    case SampleId::GraduatedRenderer:
        return QStringLiteral("GraduatedRenderer");
    case SampleId::GraduatedRendererSize:
        return QStringLiteral("GraduatedRendererSize");
    case SampleId::ClassificationMethods:
        return QStringLiteral("ClassificationMethods");
    case SampleId::RuleBasedRenderer:
        return QStringLiteral("RuleBasedRenderer");
    case SampleId::SimpleStyle:
        return QStringLiteral("SimpleStyle");
    case SampleId::SelectionStyle:
        return QStringLiteral("SelectionStyle");
    case SampleId::StylePerFeature:
        return QStringLiteral("StylePerFeature");
    case SampleId::LabelCollisionOff:
        return QStringLiteral("LabelCollisionOff");
    case SampleId::ShapefileLoad:
        return QStringLiteral("ShapefileLoad");
    case SampleId::ShapefileSaveAs:
        return QStringLiteral("ShapefileSaveAs");
    case SampleId::TabLoad:
        return QStringLiteral("TabLoad");
    case SampleId::MifLoad:
        return QStringLiteral("MifLoad");
    case SampleId::GeoPackageLoad:
        return QStringLiteral("GeoPackageLoad");
    case SampleId::KmlLoad:
        return QStringLiteral("KmlLoad");
    case SampleId::DxfLoad:
        return QStringLiteral("DxfLoad");
    case SampleId::OpenStreetMap:
        return QStringLiteral("OpenStreetMap");
    case SampleId::GeoTiffLoad:
        return QStringLiteral("GeoTiffLoad");
    case SampleId::RasterWorldTransform:
        return QStringLiteral("RasterWorldTransform");
    case SampleId::RasterOverview:
        return QStringLiteral("RasterOverview");
    case SampleId::RasterTileCache:
        return QStringLiteral("RasterTileCache");
    case SampleId::XyzPresets:
        return QStringLiteral("XyzPresets");
    case SampleId::XyzCustomUrl:
        return QStringLiteral("XyzCustomUrl");
    case SampleId::XyzLocalCache:
        return QStringLiteral("XyzLocalCache");
    case SampleId::XyzTileSize:
        return QStringLiteral("XyzTileSize");
    case SampleId::XyzMinMaxZoom:
        return QStringLiteral("XyzMinMaxZoom");
    case SampleId::XyzAttribution:
        return QStringLiteral("XyzAttribution");
    case SampleId::XyzDiagnostics:
        return QStringLiteral("XyzDiagnostics");
    case SampleId::WktOverlay:
        return QStringLiteral("WktOverlay");
    case SampleId::WktRoundtrip:
        return QStringLiteral("WktRoundtrip");
    case SampleId::HelloMap:
    default:
        return QStringLiteral("HelloMap");
    }
}

void scheduleMapFitAfterLayout(GisViewer* viewer, std::function<void()> fit)
{
    if (viewer == nullptr)
        return;

    QPointer<GisViewer> guardedViewer(viewer);
    auto fitAction = std::make_shared<std::function<void()>>(std::move(fit));
    auto attempts = std::make_shared<int>(0);
    auto run = std::make_shared<std::function<void()>>();

    *run = [guardedViewer, fitAction, attempts, run]
    {
        if (guardedViewer == nullptr)
            return;

        if ((*attempts)++ < 2 || guardedViewer->width() <= 1 || guardedViewer->height() <= 1)
        {
            QTimer::singleShot(0, guardedViewer.data(), *run);
            return;
        }

        (*fitAction)();
    };

    QTimer::singleShot(0, viewer, *run);
}

QToolBar* createMapToolbar(GisViewer& viewer, QWidget* parent)
{
    auto* toolbar = new QToolBar(parent);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));

    QAction* zoomInAction = toolbar->addAction(sampleIcon(QStringLiteral("ZoomIn.svg")), QStringLiteral("Zoom In"));
    QAction* zoomOutAction = toolbar->addAction(sampleIcon(QStringLiteral("ZoomOut.svg")), QStringLiteral("Zoom Out"));
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));
    toolbar->addSeparator();

    QActionGroup* toolGroup = new QActionGroup(toolbar);
    toolGroup->setExclusive(true);

    QAction* zoomRectAction = toolbar->addAction(sampleIcon(QStringLiteral("RectangularZoom.svg")), QStringLiteral("Zoom Rect"));
    zoomRectAction->setCheckable(true);
    toolGroup->addAction(zoomRectAction);

    QAction* panAction = toolbar->addAction(sampleIcon(QStringLiteral("Pan.svg")), QStringLiteral("Pan"));
    panAction->setCheckable(true);
    toolGroup->addAction(panAction);

    QObject::connect(zoomInAction, &QAction::triggered, &viewer, [&viewer] { viewer.zoomIn(); });
    QObject::connect(zoomOutAction, &QAction::triggered, &viewer, [&viewer] { viewer.zoomOut(); });
    QObject::connect(fullExtentAction, &QAction::triggered, &viewer, [&viewer] { viewer.fullExtent(); });
    QObject::connect(zoomRectAction, &QAction::triggered, &viewer, [&viewer]
    {
        viewer.setActiveTool(GisViewerTool::ZoomBox);
    });
    QObject::connect(panAction, &QAction::triggered, &viewer, [&viewer]
    {
        viewer.setActiveTool(GisViewerTool::Pan);
    });

    panAction->setChecked(true);
    return toolbar;
}

QWidget* createHelloMapLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(createMapToolbar(*viewer, page));
    layout->addWidget(viewer, 1);

    QString errorMessage;
    const QString path = repositoryPath(QStringLiteral("assets/data/shapefile/world_4326.shp"));
    if (!viewer->addLayerFromPath(path, &errorMessage))
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("HelloMap layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? path : errorMessage));
    }

    scheduleMapFitAfterLayout(viewer, [viewer]
    {
        viewer->fullExtent();
    });

    return page;
}

QWidget* createAddPointInteractiveLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* toolbar = new QToolBar(page);
    toolbar->setIconSize(QSize(28, 28));
    auto* viewer = new GisViewer(page);
    auto* countLabel = new QLabel(QStringLiteral("Point count: 0"), page);

    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));
    toolbar->addSeparator();

    QActionGroup* toolGroup = new QActionGroup(toolbar);
    toolGroup->setExclusive(true);

    QAction* addPointAction = toolbar->addAction(QStringLiteral("Add Point"));
    addPointAction->setCheckable(true);
    toolGroup->addAction(addPointAction);

    QAction* panAction = toolbar->addAction(sampleIcon(QStringLiteral("Pan.svg")), QStringLiteral("Pan"));
    panAction->setCheckable(true);
    toolGroup->addAction(panAction);

    QAction* clearAction = toolbar->addAction(QStringLiteral("Clear Points"));
    toolbar->addSeparator();
    toolbar->addWidget(countLabel);

    viewer->setActiveTool(GisViewerTool::AddPoint);

    layout->addWidget(toolbar);
    layout->addWidget(viewer, 1);

    QString errorMessage;
    const QString worldPath = repositoryPath(QStringLiteral("assets/data/shapefile/world_4326.shp"));
    if (!viewer->addLayerFromPath(worldPath, &errorMessage))
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("World layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? worldPath : errorMessage));
        return page;
    }

    if (GisLayer* worldLayer = viewer->mapLayerAt(0))
    {
        worldLayer->setName(QStringLiteral("World"));
        worldLayer->style().setFillColor(QStringLiteral("#D8E5E1"));
        worldLayer->style().setFillOpacity(210);
        worldLayer->style().setLineColor(QStringLiteral("#6F8883"));
        worldLayer->style().setLineWidth(0.7f);
    }

    auto pointLayer = GisLayerVector::createInMemory(QStringLiteral("Clicked Points"), GisShapeType::Point);
    pointLayer->style().setPointColor(QStringLiteral("#D95D39"));
    pointLayer->style().setLineColor(QStringLiteral("#8C321D"));
    pointLayer->style().setPointSize(9.0f);
    pointLayer->style().setLineWidth(1.2f);
    pointLayer->addShape(std::make_unique<GisShapePoint>(-122.4194, 37.7749));
    auto* pointLayerPtr = pointLayer.get();
    viewer->addLayer(pointLayer);

    const auto pointLayerIndex = [viewer, pointLayerPtr]() -> int
    {
        for (int i = 0; i < viewer->layerCount(); ++i)
        {
            if (viewer->mapLayerAt(i) == pointLayerPtr)
                return i;
        }
        return -1;
    };

    const auto updateCount = [countLabel, pointLayerPtr]
    {
        countLabel->setText(QStringLiteral("Point count: %1").arg(pointLayerPtr != nullptr ? pointLayerPtr->count() : 0));
    };

    const auto beginPointEditing = [viewer, pointLayerIndex]() -> bool
    {
        const int index = pointLayerIndex();
        if (index < 0)
            return false;

        if (!viewer->isLayerEditing(index) && !viewer->beginEditLayer(index))
            return false;

        return viewer->setActiveEditLayerIndex(index);
    };

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));
    });

    QObject::connect(addPointAction, &QAction::triggered, viewer, [viewer, beginPointEditing]
    {
        if (beginPointEditing())
            viewer->setActiveTool(GisViewerTool::AddPoint);
    });

    QObject::connect(panAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setActiveTool(GisViewerTool::Pan);
    });

    QObject::connect(clearAction, &QAction::triggered, viewer, [viewer, pointLayerIndex, beginPointEditing, updateCount]
    {
        viewer->rollbackEditLayer(pointLayerIndex());
        beginPointEditing();
        viewer->invalidateRenderCache(false, true);
        viewer->refreshLayers();
        updateCount();
    });

    QObject::connect(viewer, &GisViewer::layerEditStateChanged, viewer, [viewer, pointLayerPtr, updateCount](GisLayer* layer)
    {
        if (layer != pointLayerPtr)
            return;

        viewer->invalidateRenderCache(false, true);
        viewer->refreshLayers();
        updateCount();
    });

    beginPointEditing();
    addPointAction->setChecked(true);
    updateCount();

    scheduleMapFitAfterLayout(viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));
    });

    return page;
}

GisShapePoint galleryGeneratedPoint(int index);

GisLayerStyle galleryWorldStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#D8E5E1"));
    style.setFillOpacity(210);
    style.setLineColor(QStringLiteral("#6F8883"));
    style.setLineWidth(0.7f);
    return style;
}

GisLayerStyle galleryPointStyle()
{
    GisLayerStyle style;
    style.setPointColor(QStringLiteral("#D95D39"));
    style.setLineColor(QStringLiteral("#8C321D"));
    style.setPointSize(9.0f);
    style.setLineWidth(1.2f);
    style.setSelectedLineColor(QStringLiteral("#F59E0B"));
    style.setSelectedLineWidth(4.0f);
    return style;
}

GisLayerStyle galleryPolylineStyle()
{
    GisLayerStyle style;
    style.setLineColor(QStringLiteral("#266D8F"));
    style.setLineWidth(2.4f);
    style.setSelectedLineColor(QStringLiteral("#F59E0B"));
    style.setSelectedLineWidth(4.0f);
    return style;
}

GisLayerStyle galleryPolygonStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#F1D58A"));
    style.setFillOpacity(160);
    style.setLineColor(QStringLiteral("#266D8F"));
    style.setLineWidth(1.8f);
    style.setSelectedLineColor(QStringLiteral("#F59E0B"));
    style.setSelectedLineWidth(4.0f);
    return style;
}

bool loadGalleryWorldLayer(QWidget* parent, GisViewer& viewer)
{
    QString errorMessage;
    const QString worldPath = repositoryPath(QStringLiteral("assets/data/shapefile/world_4326.shp"));
    if (!viewer.addLayerFromPath(worldPath, &errorMessage))
    {
        QMessageBox::critical(
            parent,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("World layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? worldPath : errorMessage));
        return false;
    }

    if (GisLayer* worldLayer = viewer.mapLayerAt(0))
    {
        worldLayer->setName(QStringLiteral("World"));
        worldLayer->style() = galleryWorldStyle();
    }

    return true;
}

int galleryLayerIndexOf(const GisViewer& viewer, const GisLayer* layer)
{
    if (layer == nullptr)
        return -1;

    for (int i = 0; i < viewer.layerCount(); ++i)
    {
        if (viewer.mapLayerAt(i) == layer)
            return i;
    }

    return -1;
}

GisLayerVector* galleryVectorLayerByName(const GisViewer& viewer, const QString& name)
{
    return dynamic_cast<GisLayerVector*>(viewer.mapLayerByName(name));
}

int galleryEditLayerIndexByName(const GisViewer& viewer, const QString& name)
{
    for (int i = 0; i < viewer.layerCount(); ++i)
    {
        const GisLayer* layer = viewer.mapLayerAt(i);
        if (layer != nullptr && layer->name() == name)
            return i;
    }

    return -1;
}

bool activateGalleryEditLayerByName(GisViewer& viewer, const QString& layerName)
{
    const int index = galleryEditLayerIndexByName(viewer, layerName);
    return index >= 0
        && (viewer.isLayerEditing(index) || viewer.beginEditLayer(index))
        && viewer.setActiveEditLayerIndex(index);
}

QString galleryCountText(const GisViewer& viewer, const QString& label, const QString& layerName)
{
    const GisLayerVector* layer = galleryVectorLayerByName(viewer, layerName);
    return QStringLiteral("%1 count: %2").arg(label).arg(layer != nullptr ? layer->count() : 0);
}

QWidget* createDigitizingLivePage(
    const QString& layerName,
    const QString& countName,
    GisShapeType shapeType,
    GisViewerTool addTool,
    const GisLayerStyle& style)
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* toolbar = new QToolBar(page);
    toolbar->setIconSize(QSize(28, 28));
    auto* viewer = new GisViewer(page);
    auto* countLabel = new QLabel(page);

    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));
    toolbar->addSeparator();

    QActionGroup* toolGroup = new QActionGroup(toolbar);
    toolGroup->setExclusive(true);

    QAction* addAction = toolbar->addAction(QStringLiteral("Add %1").arg(countName));
    addAction->setCheckable(true);
    toolGroup->addAction(addAction);

    QAction* panAction = toolbar->addAction(sampleIcon(QStringLiteral("Pan.svg")), QStringLiteral("Pan"));
    panAction->setCheckable(true);
    toolGroup->addAction(panAction);

    QAction* clearAction = toolbar->addAction(QStringLiteral("Clear"));
    toolbar->addSeparator();
    toolbar->addWidget(countLabel);
    viewer->setActiveTool(addTool);

    layout->addWidget(toolbar);
    layout->addWidget(viewer, 1);

    if (!loadGalleryWorldLayer(page, *viewer))
        return page;

    auto editLayer = GisLayerVector::createInMemory(
        layerName,
        shapeType,
        GisExtent(-180.0, -90.0, 180.0, 90.0));
    editLayer->style() = style;
    viewer->addLayer(editLayer);

    const auto refresh = [viewer]
    {
        viewer->invalidateRenderCache(false, true);
        viewer->refreshLayers();
    };

    const auto updateCount = [viewer, countLabel, countName, layerName]
    {
        countLabel->setText(galleryCountText(*viewer, countName, layerName));
    };

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));
    });

    QObject::connect(addAction, &QAction::triggered, viewer, [viewer, layerName, addTool]
    {
        if (activateGalleryEditLayerByName(*viewer, layerName))
            viewer->setActiveTool(addTool);
    });

    QObject::connect(panAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setActiveTool(GisViewerTool::Pan);
    });

    QObject::connect(clearAction, &QAction::triggered, viewer, [viewer, layerName, refresh, updateCount]
    {
        viewer->rollbackEditLayer(galleryEditLayerIndexByName(*viewer, layerName));
        activateGalleryEditLayerByName(*viewer, layerName);
        refresh();
        updateCount();
    });

    QObject::connect(viewer, &GisViewer::layerEditStateChanged, viewer, [layerName, refresh, updateCount](GisLayer* layer)
    {
        if (layer == nullptr || layer->name() != layerName)
            return;

        refresh();
        updateCount();
    });

    activateGalleryEditLayerByName(*viewer, layerName);
    addAction->setChecked(true);
    viewer->setActiveTool(addTool);
    updateCount();

    scheduleMapFitAfterLayout(viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));
    });

    return page;
}

QVector<GisShapePoint> galleryProgrammaticPolyline(int index)
{
    const double offset = (index % 5) * 3.2;
    const double row = (index / 5) * 4.2;
    return
    {
        GisShapePoint(-124.0 + offset, 30.0 + row),
        GisShapePoint(-120.0 + offset, 35.0 + row),
        GisShapePoint(-116.0 + offset, 32.0 + row),
        GisShapePoint(-111.0 + offset, 38.0 + row),
        GisShapePoint(-106.0 + offset, 34.5 + row)
    };
}

QVector<GisShapePoint> galleryProgrammaticPolygon(int index)
{
    const double offset = (index % 4) * 8.0;
    const double row = (index / 4) * 7.0;
    return
    {
        GisShapePoint(-124.0 + offset, 27.0 + row),
        GisShapePoint(-116.0 + offset, 27.0 + row),
        GisShapePoint(-114.0 + offset, 33.0 + row),
        GisShapePoint(-120.0 + offset, 37.0 + row),
        GisShapePoint(-126.0 + offset, 33.0 + row),
        GisShapePoint(-124.0 + offset, 27.0 + row)
    };
}

QWidget* createProgrammaticAddLivePage(
    const QString& layerName,
    const QString& countName,
    GisShapeType shapeType,
    const GisLayerStyle& style)
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* toolbar = new QToolBar(page);
    toolbar->setIconSize(QSize(28, 28));
    auto* viewer = new GisViewer(page);
    auto* countLabel = new QLabel(page);
    auto* statusLabel = new QLabel(page);

    QAction* addAction = toolbar->addAction(QStringLiteral("Add %1").arg(countName));
    QAction* clearAction = toolbar->addAction(QStringLiteral("Clear"));
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(countLabel);
    toolbar->addSeparator();
    toolbar->addWidget(statusLabel);

    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(toolbar);
    layout->addWidget(viewer, 1);

    if (!loadGalleryWorldLayer(page, *viewer))
        return page;

    auto editLayer = GisLayerVector::createInMemory(
        layerName,
        shapeType,
        GisExtent(-180.0, -90.0, 180.0, 90.0));
    editLayer->style() = style;
    viewer->addLayer(editLayer);

    auto cursor = std::make_shared<int>(0);

    const auto refresh = [viewer]
    {
        viewer->invalidateRenderCache(false, true);
        viewer->refreshLayers();
    };

    const auto updateCount = [viewer, countLabel, countName, layerName]
    {
        countLabel->setText(galleryCountText(*viewer, countName, layerName));
    };

    QObject::connect(addAction, &QAction::triggered, viewer, [viewer, layerName, countName, shapeType, cursor, refresh, updateCount, statusLabel]
    {
        if (!activateGalleryEditLayerByName(*viewer, layerName))
        {
            statusLabel->setText(QStringLiteral("%1 layer could not enter edit mode.").arg(layerName));
            return;
        }

        const int index = galleryEditLayerIndexByName(*viewer, layerName);

        bool added = false;
        if (shapeType == GisShapeType::Point)
            added = viewer->addPointToEditLayer(index, galleryGeneratedPoint((*cursor)++));
        else if (shapeType == GisShapeType::Polyline)
            added = viewer->addPolylineToEditLayer(index, galleryProgrammaticPolyline((*cursor)++));
        else if (shapeType == GisShapeType::Polygon)
            added = viewer->addPolygonToEditLayer(index, galleryProgrammaticPolygon((*cursor)++));

        if (!added)
        {
            statusLabel->setText(QStringLiteral("Add failed for %1.").arg(layerName));
            return;
        }

        refresh();
        updateCount();
        statusLabel->setText(QStringLiteral("Added %1.").arg(countName));
    });

    QObject::connect(clearAction, &QAction::triggered, viewer, [viewer, layerName, cursor, refresh, updateCount, statusLabel]
    {
        viewer->rollbackEditLayer(galleryEditLayerIndexByName(*viewer, layerName));
        activateGalleryEditLayerByName(*viewer, layerName);
        *cursor = 0;
        refresh();
        updateCount();
        statusLabel->setText(QStringLiteral("Cleared %1.").arg(layerName));
    });

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));
    });

    activateGalleryEditLayerByName(*viewer, layerName);
    updateCount();
    statusLabel->setText(QStringLiteral("Ready."));

    scheduleMapFitAfterLayout(viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));
    });

    return page;
}

QWidget* createAddPolylineInteractiveLivePage()
{
    return createDigitizingLivePage(
        QStringLiteral("Clicked Polylines"),
        QStringLiteral("Polyline"),
        GisShapeType::Polyline,
        GisViewerTool::AddPolyline,
        galleryPolylineStyle());
}

QWidget* createAddPolygonInteractiveLivePage()
{
    return createDigitizingLivePage(
        QStringLiteral("Clicked Polygons"),
        QStringLiteral("Polygon"),
        GisShapeType::Polygon,
        GisViewerTool::AddPolygon,
        galleryPolygonStyle());
}

QWidget* createAddPointProgrammaticLivePage()
{
    return createProgrammaticAddLivePage(
        QStringLiteral("Programmatic Points"),
        QStringLiteral("Point"),
        GisShapeType::Point,
        galleryPointStyle());
}

QWidget* createAddPolylineProgrammaticLivePage()
{
    return createProgrammaticAddLivePage(
        QStringLiteral("Programmatic Polylines"),
        QStringLiteral("Polyline"),
        GisShapeType::Polyline,
        galleryPolylineStyle());
}

QWidget* createAddPolygonProgrammaticLivePage()
{
    return createProgrammaticAddLivePage(
        QStringLiteral("Programmatic Polygons"),
        QStringLiteral("Polygon"),
        GisShapeType::Polygon,
        galleryPolygonStyle());
}

void rebuildDeleteFeatureTable(QTableWidget& table, const GisLayerVector& layer)
{
    table.setRowCount(0);
    for (int rowIndex = 0; rowIndex < layer.count(); ++rowIndex)
    {
        const QHash<QString, QVariant> attributes = layer.attributesForRow(rowIndex);
        const int row = table.rowCount();
        table.insertRow(row);
        table.setItem(row, 0, new QTableWidgetItem(QString::number(rowIndex + 1)));
        table.setItem(row, 1, new QTableWidgetItem(attributes.value(QStringLiteral("Name")).toString()));
        table.setItem(row, 2, new QTableWidgetItem(attributes.value(QStringLiteral("Group")).toString()));
        table.setItem(row, 3, new QTableWidgetItem(attributes.value(QStringLiteral("Value")).toString()));
    }
}

void selectDeleteFeatureTableRow(QTableWidget& table, int featureId)
{
    table.clearSelection();
    for (int row = 0; row < table.rowCount(); ++row)
    {
        const QTableWidgetItem* idItem = table.item(row, 0);
        if (idItem != nullptr && idItem->text().toInt() == featureId)
        {
            table.selectRow(row);
            table.scrollToItem(idItem, QAbstractItemView::PositionAtCenter);
            return;
        }
    }
}

GisShapePoint galleryDeleteFeaturePoint(int index)
{
    static constexpr double xMin = -122.0;
    static constexpr double yMin = 30.0;
    static constexpr double xStep = 7.0;
    static constexpr double yStep = 5.0;
    static constexpr int columns = 8;

    return GisShapePoint(
        xMin + (index % columns) * xStep,
        yMin + (index / columns) * yStep);
}

QWidget* createDeleteFeatureLivePage()
{
    const QString pointLayerName = QStringLiteral("Editable Points");

    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* toolbar = new QToolBar(page);
    toolbar->setIconSize(QSize(28, 28));
    auto* viewer = new GisViewer(page);
    auto* countLabel = new QLabel(page);

    QAction* selectAction = toolbar->addAction(sampleIcon(QStringLiteral("Select.svg")), QStringLiteral("Select"));
    selectAction->setCheckable(true);
    QAction* deleteOneAction = toolbar->addAction(sampleIcon(QStringLiteral("Delete.svg")), QStringLiteral("Delete Feature"));
    QAction* deleteSelectedAction = toolbar->addAction(sampleIcon(QStringLiteral("Delete.svg")), QStringLiteral("Delete Selected"));
    QAction* resetAction = toolbar->addAction(sampleIcon(QStringLiteral("Refresh.svg")), QStringLiteral("Reset Points"));
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(countLabel);

    auto* splitter = new QSplitter(page);
    auto* table = new QTableWidget(splitter);
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({
        QStringLiteral("ID"),
        QStringLiteral("Name"),
        QStringLiteral("Group"),
        QStringLiteral("Value")
    });
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    splitter->addWidget(table);
    splitter->addWidget(viewer);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({ 260, 900 });

    auto* status = new QLabel(QStringLiteral("Select a point, then delete one feature or all selected features."), page);
    status->setStyleSheet(QStringLiteral("background: #f4f4f4; border-top: 1px solid #d0d0d0; padding-left: 4px;"));

    viewer->setActiveTool(GisViewerTool::Info);

    layout->addWidget(toolbar);
    layout->addWidget(splitter, 1);
    layout->addWidget(status);

    if (!loadGalleryWorldLayer(page, *viewer))
        return page;

    auto pointLayer = GisLayerVector::createInMemory(
        pointLayerName,
        GisShapeType::Point,
        GisExtent(-180.0, -90.0, 180.0, 90.0));
    pointLayer->addAttributeDefinition({ QStringLiteral("Name"), GisAttributeType::String, 64, 0 });
    pointLayer->addAttributeDefinition({ QStringLiteral("Group"), GisAttributeType::String, 32, 0 });
    pointLayer->addAttributeDefinition({ QStringLiteral("Value"), GisAttributeType::Integer, 8, 0 });
    pointLayer->style() = galleryPointStyle();
    pointLayer->style().setShowLabels(true);
    pointLayer->style().setLabelField(QStringLiteral("Name"));
    pointLayer->style().setLabelFontSize(10.0f);
    pointLayer->style().setLabelColor(QStringLiteral("#263238"));
    pointLayer->style().setLabelHaloEnabled(true);
    pointLayer->style().setLabelHaloColor(QStringLiteral("#FFFFFF"));
    pointLayer->style().setLabelHaloWidth(2.0f);
    pointLayer->style().setLabelOffsetY(-12.0f);
    pointLayer->style().setLabelAllowOverlap(true);
    viewer->addLayer(pointLayer);

    const auto refresh = [viewer]
    {
        viewer->invalidateRenderCache(false, true);
        viewer->refreshLayers();
    };

    const auto rebuildTableFromLayer = [viewer, table, pointLayerName]
    {
        if (const GisLayerVector* layer = galleryVectorLayerByName(*viewer, pointLayerName))
            rebuildDeleteFeatureTable(*table, *layer);
        else
            table->setRowCount(0);
    };

    const auto updateCount = [viewer, countLabel, pointLayerName]
    {
        const GisLayerVector* layer = galleryVectorLayerByName(*viewer, pointLayerName);
        countLabel->setText(QStringLiteral("Feature count: %1 | Selected: %2")
            .arg(layer != nullptr ? layer->count() : 0)
            .arg(viewer->selectedFeatureCount()));
    };

    const auto populate = [viewer, pointLayerName, rebuildTableFromLayer, refresh, updateCount, status]
    {
        viewer->rollbackEditLayer(galleryEditLayerIndexByName(*viewer, pointLayerName));
        activateGalleryEditLayerByName(*viewer, pointLayerName);
        viewer->clearSelectedFeatures();

        for (int i = 0; i < 16; ++i)
        {
            QHash<QString, QVariant> attributes;
            attributes.insert(QStringLiteral("Name"), QStringLiteral("Point %1").arg(i + 1));
            attributes.insert(QStringLiteral("Group"), i % 2 == 0 ? QStringLiteral("A") : QStringLiteral("B"));
            attributes.insert(QStringLiteral("Value"), (i + 1) * 5);
            viewer->addPointToEditLayer(
                galleryEditLayerIndexByName(*viewer, pointLayerName),
                galleryDeleteFeaturePoint(i),
                attributes);
        }

        rebuildTableFromLayer();
        status->setText(QStringLiteral("Reset 16 editable point features."));
        refresh();
        updateCount();
    };

    QObject::connect(selectAction, &QAction::triggered, viewer, [viewer](bool checked)
    {
        viewer->setActiveTool(checked ? GisViewerTool::Info : GisViewerTool::Pan);
    });

    QObject::connect(viewer, &GisViewer::mapClicked, viewer, [viewer, pointLayerName, table, status, updateCount](
        GisViewerTool tool,
        const QPointF& screenPoint,
        const GisShapePoint&,
        Qt::KeyboardModifiers modifiers)
    {
        if (tool != GisViewerTool::Info)
            return;

        const FeatureHitTestResult result = viewer->hitTestTopFeatureAt(screenPoint, 8);
        if (!result.isValid() || result.layer == nullptr || result.layer->name() != pointLayerName)
        {
            viewer->clearSelectedFeatures();
            table->clearSelection();
            status->setText(QStringLiteral("No editable point feature found."));
            updateCount();
            return;
        }

        if (modifiers.testFlag(Qt::ControlModifier))
            viewer->toggleSelectedFeature(result);
        else
            viewer->setSelectedFeature(result);

        selectDeleteFeatureTableRow(*table, result.featureId);
        status->setText(QStringLiteral("Selected feature %1.").arg(result.featureId));
        updateCount();
    });

    QObject::connect(deleteOneAction, &QAction::triggered, viewer, [viewer, pointLayerName, table, status, rebuildTableFromLayer, refresh, updateCount]
    {
        const QVector<FeatureHitTestResult> selected = viewer->selectedFeatures();
        if (selected.isEmpty())
        {
            status->setText(QStringLiteral("Select a feature first."));
            return;
        }

        const int featureId = selected.first().featureId;
        activateGalleryEditLayerByName(*viewer, pointLayerName);
        if (!viewer->deleteShapeFromEditLayer(galleryEditLayerIndexByName(*viewer, pointLayerName), featureId))
        {
            status->setText(QStringLiteral("deleteShapeFromEditLayer failed."));
            return;
        }

        viewer->clearSelectedFeatures();
        rebuildTableFromLayer();
        table->clearSelection();
        status->setText(QStringLiteral("Deleted feature %1.").arg(featureId));
        refresh();
        updateCount();
    });

    QObject::connect(deleteSelectedAction, &QAction::triggered, viewer, [viewer, pointLayerName, table, status, rebuildTableFromLayer, refresh, updateCount]
    {
        const int selectedCount = viewer->selectedFeatureCount();
        if (selectedCount <= 0)
        {
            status->setText(QStringLiteral("Select one or more features first."));
            return;
        }

        activateGalleryEditLayerByName(*viewer, pointLayerName);
        if (!viewer->deleteSelectedFeaturesFromEditLayer())
        {
            status->setText(QStringLiteral("deleteSelectedFeaturesFromEditLayer failed."));
            return;
        }

        rebuildTableFromLayer();
        table->clearSelection();
        status->setText(QStringLiteral("Deleted %1 selected feature(s).").arg(selectedCount));
        refresh();
        updateCount();
    });

    QObject::connect(resetAction, &QAction::triggered, viewer, populate);
    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));
    });
    QObject::connect(viewer, &GisViewer::selectionChanged, viewer, [updateCount](int) { updateCount(); });

    activateGalleryEditLayerByName(*viewer, pointLayerName);
    populate();
    selectAction->setChecked(true);

    scheduleMapFitAfterLayout(viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));
    });

    return page;
}

QWidget* createMoveFeatureToolLivePage()
{
    const QString pointLayerName = QStringLiteral("Movable Points");

    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* toolbar = new QToolBar(page);
    toolbar->setIconSize(QSize(28, 28));
    auto* viewer = new GisViewer(page);
    auto* countLabel = new QLabel(page);
    auto* status = new QLabel(QStringLiteral("Select a point, switch to Move Feature, then drag it."), page);
    status->setStyleSheet(QStringLiteral("background: #f4f4f4; border-top: 1px solid #d0d0d0; padding-left: 4px;"));

    QAction* selectAction = toolbar->addAction(sampleIcon(QStringLiteral("Select.svg")), QStringLiteral("Select"));
    selectAction->setCheckable(true);
    QAction* moveAction = toolbar->addAction(sampleIcon(QStringLiteral("Move.svg")), QStringLiteral("Move Feature"));
    moveAction->setCheckable(true);
    QAction* resetAction = toolbar->addAction(sampleIcon(QStringLiteral("Refresh.svg")), QStringLiteral("Reset Points"));
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(countLabel);

    auto* toolGroup = new QActionGroup(toolbar);
    toolGroup->setExclusive(true);
    toolGroup->addAction(selectAction);
    toolGroup->addAction(moveAction);

    viewer->setActiveTool(GisViewerTool::Info);

    layout->addWidget(toolbar);
    layout->addWidget(viewer, 1);
    layout->addWidget(status);

    if (!loadGalleryWorldLayer(page, *viewer))
        return page;

    auto pointLayer = GisLayerVector::createInMemory(
        pointLayerName,
        GisShapeType::Point,
        GisExtent(-180.0, -90.0, 180.0, 90.0));
    pointLayer->addAttributeDefinition({ QStringLiteral("Name"), GisAttributeType::String, 64, 0 });
    pointLayer->addAttributeDefinition({ QStringLiteral("Group"), GisAttributeType::String, 32, 0 });
    pointLayer->style() = galleryPointStyle();
    pointLayer->style().setPointSize(12.0f);
    pointLayer->style().setShowLabels(true);
    pointLayer->style().setLabelField(QStringLiteral("Name"));
    pointLayer->style().setLabelFontSize(10.0f);
    pointLayer->style().setLabelColor(QStringLiteral("#263238"));
    pointLayer->style().setLabelHaloEnabled(true);
    pointLayer->style().setLabelHaloColor(QStringLiteral("#FFFFFF"));
    pointLayer->style().setLabelHaloWidth(2.0f);
    pointLayer->style().setLabelOffsetY(-13.0f);
    pointLayer->style().setLabelAllowOverlap(true);
    viewer->addLayer(pointLayer);

    const auto refresh = [viewer]
    {
        viewer->invalidateRenderCache(false, true);
        viewer->refreshLayers();
    };

    const auto updateCount = [viewer, countLabel, pointLayerName]
    {
        const GisLayerVector* layer = galleryVectorLayerByName(*viewer, pointLayerName);
        countLabel->setText(QStringLiteral("Feature count: %1 | Selected: %2")
            .arg(layer != nullptr ? layer->count() : 0)
            .arg(viewer->selectedFeatureCount()));
    };

    const auto populate = [viewer, pointLayerName, refresh, updateCount, status]
    {
        viewer->rollbackEditLayer(galleryEditLayerIndexByName(*viewer, pointLayerName));
        activateGalleryEditLayerByName(*viewer, pointLayerName);
        viewer->clearSelectedFeatures();

        for (int i = 0; i < 14; ++i)
        {
            QHash<QString, QVariant> attributes;
            attributes.insert(QStringLiteral("Name"), QStringLiteral("Point %1").arg(i + 1));
            attributes.insert(QStringLiteral("Group"), i % 2 == 0 ? QStringLiteral("North") : QStringLiteral("South"));
            viewer->addPointToEditLayer(
                galleryEditLayerIndexByName(*viewer, pointLayerName),
                galleryDeleteFeaturePoint(i),
                attributes);
        }

        status->setText(QStringLiteral("Select a point, switch to Move Feature, then drag it."));
        refresh();
        updateCount();
    };

    QObject::connect(selectAction, &QAction::triggered, viewer, [viewer, status]
    {
        viewer->setActiveTool(GisViewerTool::Info);
        status->setText(QStringLiteral("Select mode. Click a movable point."));
    });

    QObject::connect(moveAction, &QAction::triggered, viewer, [viewer, pointLayerName, status]
    {
        activateGalleryEditLayerByName(*viewer, pointLayerName);
        viewer->setActiveTool(GisViewerTool::MoveFeature);
        status->setText(QStringLiteral("Move Feature mode. Drag a selected point."));
    });

    QObject::connect(viewer, &GisViewer::mapClicked, viewer, [viewer, pointLayerName, status, updateCount](
        GisViewerTool tool,
        const QPointF& screenPoint,
        const GisShapePoint&,
        Qt::KeyboardModifiers modifiers)
    {
        if (tool != GisViewerTool::Info)
            return;

        const FeatureHitTestResult result = viewer->hitTestTopFeatureAt(screenPoint, 8);
        if (!result.isValid() || result.layer == nullptr || result.layer->name() != pointLayerName)
        {
            viewer->clearSelectedFeatures();
            status->setText(QStringLiteral("No movable point feature found."));
            updateCount();
            return;
        }

        if (modifiers.testFlag(Qt::ControlModifier))
            viewer->toggleSelectedFeature(result);
        else
            viewer->setSelectedFeature(result);

        status->setText(QStringLiteral("Selected feature %1. Switch to Move Feature and drag it.").arg(result.featureId));
        updateCount();
    });

    QObject::connect(viewer, &GisViewer::layerEditStateChanged, viewer, [pointLayerName, refresh, updateCount, status](GisLayer* layer)
    {
        if (layer == nullptr || layer->name() != pointLayerName)
            return;

        refresh();
        updateCount();
        status->setText(QStringLiteral("Feature geometry changed by Move Feature tool."));
    });

    QObject::connect(resetAction, &QAction::triggered, viewer, populate);
    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));
    });
    QObject::connect(viewer, &GisViewer::selectionChanged, viewer, [updateCount](int) { updateCount(); });

    activateGalleryEditLayerByName(*viewer, pointLayerName);
    populate();
    selectAction->setChecked(true);

    scheduleMapFitAfterLayout(viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));
    });

    return page;
}

GisShapePoint galleryEditSessionPoint(int index)
{
    const int column = index % 11;
    const int row = (index / 11) % 6;
    const int cycle = index / 66;

    return GisShapePoint(
        -124.0 + (column * 5.6) + (cycle * 0.35),
        25.0 + (row * 4.2) + (cycle * 0.35));
}

QWidget* createEditSessionLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* toolbar = new QToolBar(page);
    toolbar->setIconSize(QSize(28, 28));
    auto* viewer = new GisViewer(page);
    auto* stateLabel = new QLabel(page);

    QAction* beginEditAction = toolbar->addAction(QStringLiteral("Begin Edit"));
    QAction* addFeatureAction = toolbar->addAction(QStringLiteral("Add Feature"));
    toolbar->addSeparator();
    QAction* commitEditAction = toolbar->addAction(QStringLiteral("Commit Edit"));
    QAction* rollbackEditAction = toolbar->addAction(QStringLiteral("Rollback Edit"));
    toolbar->addSeparator();
    QAction* fullExtentAction = toolbar->addAction(sampleIcon(QStringLiteral("FullExtent.svg")), QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    toolbar->addWidget(stateLabel);
    
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(toolbar);
    layout->addWidget(viewer, 1);

    QString errorMessage;
    const QString worldPath = repositoryPath(QStringLiteral("assets/data/shapefile/world_4326.shp"));
    if (!viewer->addLayerFromPath(worldPath, &errorMessage))
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("World layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? worldPath : errorMessage));
        return page;
    }

    if (GisLayer* worldLayer = viewer->mapLayerAt(0))
    {
        worldLayer->setName(QStringLiteral("World"));
        worldLayer->style().setFillColor(QStringLiteral("#D8E5E1"));
        worldLayer->style().setFillOpacity(210);
        worldLayer->style().setLineColor(QStringLiteral("#6F8883"));
        worldLayer->style().setLineWidth(0.7f);
    }

    auto editLayer = GisLayerVector::createInMemory(QStringLiteral("Editable Cities"), GisShapeType::Point);
    editLayer->style().setPointColor(QStringLiteral("#D85B35"));
    editLayer->style().setLineColor(QStringLiteral("#8C321D"));
    editLayer->style().setPointSize(9.0f);
    editLayer->style().setLineWidth(1.2f);
    editLayer->addPoint(GisShapePoint(-122.4194, 37.7749));
    editLayer->addPoint(GisShapePoint(-118.2437, 34.0522));
    editLayer->addPoint(GisShapePoint(-112.0740, 33.4484));
    auto* editLayerPtr = editLayer.get();
    viewer->addLayer(editLayer);

    auto editPointCursor = std::make_shared<int>(0);

    const auto editLayerIndex = [viewer, editLayerPtr]() -> int
    {
        for (int i = 0; i < viewer->layerCount(); ++i)
        {
            if (viewer->mapLayerAt(i) == editLayerPtr)
                return i;
        }
        return -1;
    };

    const auto updateUi = [beginEditAction, addFeatureAction, commitEditAction, rollbackEditAction, stateLabel, editLayerPtr]
    {
        const bool editing = editLayerPtr != nullptr && editLayerPtr->isEditing();
        beginEditAction->setEnabled(!editing);
        addFeatureAction->setEnabled(editing);
        commitEditAction->setEnabled(editing);
        rollbackEditAction->setEnabled(editing);
        stateLabel->setText(QStringLiteral("Editing: %1 | Feature count: %2")
            .arg(editing ? QStringLiteral("ON") : QStringLiteral("OFF"))
            .arg(editLayerPtr != nullptr ? editLayerPtr->count() : 0));
    };

    QObject::connect(beginEditAction, &QAction::triggered, viewer, [viewer, editLayerIndex, updateUi]
    {
        viewer->beginEditLayer(editLayerIndex());
        updateUi();
    });

    QObject::connect(addFeatureAction, &QAction::triggered, viewer, [viewer, editLayerPtr, editPointCursor, updateUi]
    {
        if (editLayerPtr == nullptr || !editLayerPtr->isEditing())
            return;

        if (editLayerPtr->addShapeEdit(std::make_unique<GisShapePoint>(galleryEditSessionPoint((*editPointCursor)++))))
        {
            viewer->invalidateRenderCache(true, true);
            viewer->refreshLayers();
            updateUi();
        }
    });

    QObject::connect(commitEditAction, &QAction::triggered, viewer, [viewer, editLayerIndex, updateUi]
    {
        viewer->commitEditLayer(editLayerIndex());
        viewer->invalidateRenderCache(true, true);
        viewer->refreshLayers();
        updateUi();
    });

    QObject::connect(rollbackEditAction, &QAction::triggered, viewer, [viewer, editLayerIndex, updateUi]
    {
        viewer->rollbackEditLayer(editLayerIndex());
        viewer->invalidateRenderCache(true, true);
        viewer->refreshLayers();
        updateUi();
    });

    QObject::connect(fullExtentAction, &QAction::triggered, viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));
    });

    updateUi();

    scheduleMapFitAfterLayout(viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));
    });

    return page;
}

bool loadStyledFileLayer(GisViewer& viewer, const QString& relativePath)
{
    const QString path = repositoryPath(relativePath);
    QString errorMessage;

    if (!viewer.addLayerFromPath(path, &errorMessage))
    {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("Layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? path : errorMessage));
        return false;
    }

    if (GisLayer* layer = viewer.mapLayerAt(0))
    {
        if (path.endsWith(QStringLiteral(".kml"), Qt::CaseInsensitive))
        {
            layer->style().setPointColor(QStringLiteral("#D95D39"));
            layer->style().setPointSize(8.0f);
            layer->style().setLineColor(QStringLiteral("#D95D39"));
            layer->style().setLineWidth(1.5f);
        }
        else if (path.endsWith(QStringLiteral(".shp"), Qt::CaseInsensitive))
        {
            layer->style().setFillColor(QStringLiteral("#D8E5E1"));
            layer->style().setFillOpacity(140);
            layer->style().setLineColor(QStringLiteral("#5F7874"));
            layer->style().setLineWidth(1.0f);
        }
    }

    return true;
}

QWidget* createAddLayersLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(createMapToolbar(*viewer, page));
    layout->addWidget(viewer, 1);

    viewer->addOpenStreetMapLayer(true);
    const bool loaded =
        loadStyledFileLayer(*viewer, QStringLiteral("assets/data/tiff/usa_3857.tif")) &&
        loadStyledFileLayer(*viewer, QStringLiteral("assets/data/shapefile/usa_states_3857.shp")) &&
        loadStyledFileLayer(*viewer, QStringLiteral("assets/data/kml/usa_cities_4326.kml"));

    if (loaded)
    {
        scheduleMapFitAfterLayout(viewer, [viewer]
        {
            viewer->zoomToLayer(2);
        });
    }

    return page;
}

QWidget* createProjectLivePage(std::function<void()> ready = {})
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    auto* progressWidget = new QWidget(page);
    auto* progressLayout = new QHBoxLayout(progressWidget);
    progressLayout->setContentsMargins(8, 4, 8, 4);
    progressLayout->setSpacing(8);

    auto* progressLabel = new QLabel(QStringLiteral("Ready"), progressWidget);
    auto* progressBar = new QProgressBar(progressWidget);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setTextVisible(true);
    progressBar->setFormat(QStringLiteral("%p%"));
    progressBar->setMinimumWidth(260);

    progressLayout->addWidget(progressLabel, 1);
    progressLayout->addWidget(progressBar);

    layout->addWidget(createMapToolbar(*viewer, page));
    layout->addWidget(viewer, 1);
    layout->addWidget(progressWidget);

    QPointer<QWidget> guardedPage(page);
    QPointer<GisViewer> guardedViewer(viewer);
    QPointer<QProgressBar> guardedProgressBar(progressBar);
    QPointer<QLabel> guardedProgressLabel(progressLabel);

    QMetaObject::invokeMethod(viewer, [guardedPage, guardedViewer, guardedProgressBar, guardedProgressLabel, ready]
    {
        if (guardedPage == nullptr || guardedViewer == nullptr || guardedProgressBar == nullptr || guardedProgressLabel == nullptr)
        {
            if (ready)
                ready();
            return;
        }

        const QString projectPath = repositoryPath(QStringLiteral("assets/data/project/andalucia.geokernel"));
        guardedProgressBar->setValue(0);
        guardedProgressLabel->setText(QStringLiteral("Loading andalucia.geokernel..."));

        auto progress = [guardedProgressBar, guardedProgressLabel](int value, const QString& text)
        {
            if (guardedProgressBar == nullptr || guardedProgressLabel == nullptr)
                return;

            guardedProgressBar->setValue(std::clamp(value, 0, 100));
            guardedProgressLabel->setText(text);
            QApplication::processEvents();
        };

        if (!guardedViewer->openProject(projectPath, progress))
        {
            if (guardedProgressBar != nullptr)
                guardedProgressBar->setValue(0);
            if (guardedProgressLabel != nullptr)
                guardedProgressLabel->setText(QStringLiteral("Project could not be loaded."));
            QMessageBox::critical(
                guardedPage,
                QStringLiteral("SamplesGallery"),
                QStringLiteral("Project could not be loaded:\n%1").arg(projectPath));
            if (ready)
                ready();
            return;
        }

        guardedProgressBar->setValue(100);
        guardedProgressLabel->setText(QStringLiteral("Project loaded."));
        scheduleMapFitAfterLayout(guardedViewer, [guardedViewer]
        {
            if (guardedViewer != nullptr)
                guardedViewer->fullExtent();
        });
        if (ready)
            ready();
    }, Qt::QueuedConnection);

    return page;
}

QWidget* createScalebarLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    auto* scaleBar = new GisScaleBar(viewer);
    scaleBar->setViewer(viewer);
    scaleBar->resize(180, 52);
    scaleBar->setAnchor(GisScaleBarAnchor::BottomRight);
    scaleBar->raise();
    scaleBar->show();

    layout->addWidget(viewer, 1);

    if (loadStyledFileLayer(*viewer, QStringLiteral("assets/data/shapefile/world_4326.shp")))
    {
        scheduleMapFitAfterLayout(viewer, [viewer] { viewer->fullExtent(); });
    }

    return page;
}

QWidget* createMinimapLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    auto* miniMap = new GisMiniMap(viewer);
    miniMap->setViewer(viewer);
    miniMap->resize(220, 150);
    miniMap->setAnchor(GisMiniMapAnchor::TopRight);
    miniMap->raise();
    miniMap->show();

    layout->addWidget(viewer, 1);

    if (loadStyledFileLayer(*viewer, QStringLiteral("assets/data/shapefile/world_4326.shp")))
    {
        scheduleMapFitAfterLayout(viewer, [viewer] { viewer->fullExtent(); });
    }

    return page;
}

void removeLayerByName(GisViewer& viewer, const QString& name)
{
    for (int i = viewer.layerCount() - 1; i >= 0; --i)
    {
        const GisLayer* layer = viewer.mapLayerAt(i);
        if (layer != nullptr && layer->name() == name)
            viewer.removeLayer(i);
    }
}

bool hasLayerNamed(const GisViewer& viewer, const QString& name)
{
    for (int i = 0; i < viewer.layerCount(); ++i)
    {
        const GisLayer* layer = viewer.mapLayerAt(i);
        if (layer != nullptr && layer->name() == name)
            return true;
    }

    return false;
}

void addNamedLayer(GisViewer& viewer, const QString& name, const QString& relativePath)
{
    if (hasLayerNamed(viewer, name))
        return;

    if (!loadStyledFileLayer(viewer, relativePath))
        return;

    if (GisLayer* layer = viewer.mapLayerAt(0))
        layer->setName(name);
}

QWidget* createLayerAddRemoveLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* toolbar = new QWidget(page);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(4, 4, 4, 4);
    toolbarLayout->setSpacing(4);

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    const auto addButton = [toolbarLayout, toolbar](const QString& text)
    {
        auto* button = new QPushButton(text, toolbar);
        toolbarLayout->addWidget(button);
        return button;
    };

    QPushButton* addWorld = addButton(QStringLiteral("Add World"));
    QPushButton* addStates = addButton(QStringLiteral("Add States"));
    QPushButton* addCities = addButton(QStringLiteral("Add Cities"));
    QPushButton* removeWorld = addButton(QStringLiteral("Remove World"));
    QPushButton* removeStates = addButton(QStringLiteral("Remove States"));
    QPushButton* removeCities = addButton(QStringLiteral("Remove Cities"));
    QPushButton* clearLayers = addButton(QStringLiteral("Clear Layers"));
    toolbarLayout->addStretch(1);

    QObject::connect(addWorld, &QPushButton::clicked, viewer, [viewer]
    {
        addNamedLayer(*viewer, QStringLiteral("World"), QStringLiteral("assets/data/shapefile/world_4326.shp"));
    });
    QObject::connect(addStates, &QPushButton::clicked, viewer, [viewer]
    {
        addNamedLayer(*viewer, QStringLiteral("States"), QStringLiteral("assets/data/us_state_boundaries.shp"));
    });
    QObject::connect(addCities, &QPushButton::clicked, viewer, [viewer]
    {
        addNamedLayer(*viewer, QStringLiteral("Cities"), QStringLiteral("assets/data/kml/usa_cities_4326.kml"));
    });
    QObject::connect(removeWorld, &QPushButton::clicked, viewer, [viewer]
    {
        removeLayerByName(*viewer, QStringLiteral("World"));
    });
    QObject::connect(removeStates, &QPushButton::clicked, viewer, [viewer]
    {
        removeLayerByName(*viewer, QStringLiteral("States"));
    });
    QObject::connect(removeCities, &QPushButton::clicked, viewer, [viewer]
    {
        removeLayerByName(*viewer, QStringLiteral("Cities"));
    });
    QObject::connect(clearLayers, &QPushButton::clicked, viewer, [viewer]
    {
        viewer->clearLayers();
    });

    layout->addWidget(toolbar);
    layout->addWidget(viewer, 1);

    addNamedLayer(*viewer, QStringLiteral("World"), QStringLiteral("assets/data/shapefile/world_4326.shp"));
    addNamedLayer(*viewer, QStringLiteral("States"), QStringLiteral("assets/data/us_state_boundaries.shp"));
    addNamedLayer(*viewer, QStringLiteral("Cities"), QStringLiteral("assets/data/kml/usa_cities_4326.kml"));
    scheduleMapFitAfterLayout(viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-151.2, 16.4, -41.6, 55.6));
    });

    return page;
}

void refreshReorderList(const GisViewer& viewer, QListWidget& list, int selectedIndex = -1)
{
    list.clear();
    for (int i = 0; i < viewer.layerCount(); ++i)
        list.addItem(viewer.layerDisplayText(i));

    if (selectedIndex >= 0 && selectedIndex < list.count())
        list.setCurrentRow(selectedIndex);
    else if (list.count() > 0)
        list.setCurrentRow(0);
}

QWidget* createLayerReorderLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* sidePanel = new QWidget(page);
    sidePanel->setFixedWidth(220);
    auto* sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(8, 8, 8, 8);
    sideLayout->setSpacing(8);

    auto* layerList = new QListWidget(sidePanel);
    auto* moveUp = new QPushButton(QStringLiteral("Move Up"), sidePanel);
    auto* moveDown = new QPushButton(QStringLiteral("Move Down"), sidePanel);
    sideLayout->addWidget(layerList, 1);
    sideLayout->addWidget(moveUp);
    sideLayout->addWidget(moveDown);

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(sidePanel);
    layout->addWidget(viewer, 1);

    addNamedLayer(*viewer, QStringLiteral("World"), QStringLiteral("assets/data/shapefile/world_4326.shp"));
    addNamedLayer(*viewer, QStringLiteral("States"), QStringLiteral("assets/data/us_state_boundaries.shp"));
    addNamedLayer(*viewer, QStringLiteral("Cities"), QStringLiteral("assets/data/kml/usa_cities_4326.kml"));

    refreshReorderList(*viewer, *layerList);

    QObject::connect(moveUp, &QPushButton::clicked, viewer, [viewer, layerList]
    {
        const int row = layerList->currentRow();
        if (row <= 0)
            return;

        viewer->moveLayer(row, row - 1);
        refreshReorderList(*viewer, *layerList, row - 1);
    });

    QObject::connect(moveDown, &QPushButton::clicked, viewer, [viewer, layerList]
    {
        const int row = layerList->currentRow();
        if (row < 0 || row >= viewer->layerCount() - 1)
            return;

        viewer->moveLayer(row, row + 1);
        refreshReorderList(*viewer, *layerList, row + 1);
    });

    QObject::connect(viewer, &GisViewer::layersChanged, layerList, [viewer, layerList]
    {
        refreshReorderList(*viewer, *layerList, layerList->currentRow());
    });

    scheduleMapFitAfterLayout(viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-151.2, 16.4, -41.6, 55.6));
    });

    return page;
}

struct CityZoomLayer
{
    QString name;
    QString relativePath;
    QString fillColor;
};

QString displayNameFromFileName(const QString& fileName)
{
    QString name = QFileInfo(fileName).completeBaseName().replace(QLatin1Char('_'), QLatin1Char(' '));
    const QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    QStringList titleWords;
    titleWords.reserve(words.size());
    for (QString word : words)
    {
        if (!word.isEmpty())
            word[0] = word[0].toUpper();
        titleWords.append(word);
    }

    return titleWords.join(QLatin1Char(' '));
}

QVector<CityZoomLayer> loadCityZoomLayerList()
{
    const QStringList palette
    {
        QStringLiteral("#BFD6E5"),
        QStringLiteral("#C9D5C9"),
        QStringLiteral("#D8CDA7"),
        QStringLiteral("#D7B79B"),
        QStringLiteral("#D6C6E3"),
        QStringLiteral("#B9D8C5")
    };

    const QDir cityDir(repositoryPath(QStringLiteral("assets/data/shapefile/california")));
    const QFileInfoList files = cityDir.entryInfoList(
        QStringList { QStringLiteral("*.shp") },
        QDir::Files,
        QDir::Name);

    QVector<CityZoomLayer> layers;
    layers.reserve(files.size());

    for (int i = 0; i < files.size(); ++i)
    {
        const QFileInfo& file = files[i];
        layers.append(CityZoomLayer {
            displayNameFromFileName(file.fileName()),
            QStringLiteral("assets/data/shapefile/california/%1").arg(file.fileName()),
            palette[i % palette.size()]
        });
    }

    return layers;
}

bool addCityZoomLayer(GisViewer& viewer, const CityZoomLayer& city)
{
    if (!loadStyledFileLayer(viewer, city.relativePath))
        return false;

    if (GisLayer* layer = viewer.mapLayerAt(0))
    {
        layer->setName(city.name);
        layer->style().setFillColor(city.fillColor);
        layer->style().setFillOpacity(150);
        layer->style().setLineColor(QStringLiteral("#5F7772"));
        layer->style().setLineWidth(0.8f);
        layer->style().setShowLabels(true);
        layer->style().setLabelField(QStringLiteral("NAME"));
        layer->style().setLabelColor(QStringLiteral("#000000"));
        layer->style().setLabelHaloEnabled(true);
        layer->style().setLabelHaloColor(QStringLiteral("#FFFF00"));
        layer->style().setLabelHaloWidth(2.0f);
    }

    return true;
}

int layerIndexByName(const GisViewer& viewer, const QString& name)
{
    for (int i = 0; i < viewer.layerCount(); ++i)
    {
        const GisLayer* layer = viewer.mapLayerAt(i);
        if (layer != nullptr && layer->name() == name)
            return i;
    }

    return -1;
}

QWidget* createLayerZoomToLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* topPanel = new QWidget(page);
    auto* topLayout = new QHBoxLayout(topPanel);
    topLayout->setContentsMargins(6, 4, 6, 4);
    topLayout->setSpacing(6);

    auto* comboBox = new QComboBox(topPanel);
    comboBox->setMinimumWidth(220);
    comboBox->addItem(QStringLiteral("-"));
    topLayout->addWidget(comboBox);
    topLayout->addStretch(1);

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(topPanel);
    layout->addWidget(viewer, 1);

    const QVector<CityZoomLayer> cities = loadCityZoomLayerList();
    for (const CityZoomLayer& city : cities)
    {
        comboBox->addItem(city.name);
        if (!addCityZoomLayer(*viewer, city))
            return page;
    }

    viewer->refreshLayers();

    QObject::connect(comboBox, &QComboBox::currentTextChanged, viewer, [viewer](const QString& text)
    {
        if (text == QStringLiteral("-"))
        {
            viewer->fullExtent();
            return;
        }

        const int index = layerIndexByName(*viewer, text);
        if (index >= 0)
            viewer->zoomToLayer(index);
    });

    scheduleMapFitAfterLayout(viewer, [viewer]
    {
        viewer->fullExtent();
    });

    return page;
}

QWidget* createLayerExtentLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    auto* status = new QLabel(QStringLiteral("Ready"), page);
    status->setStyleSheet(QStringLiteral("background: #f4f4f4; border-top: 1px solid #d0d0d0; padding-left: 4px;"));

    layout->addWidget(viewer, 1);
    layout->addWidget(status);

    const QString path = repositoryPath(QStringLiteral("assets/data/california/california.shp"));
    QString errorMessage;
    if (!viewer->addLayerFromPath(path, &errorMessage))
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("LayerExtent layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? path : errorMessage));
        return page;
    }

    GisLayer* california = viewer->mapLayerAt(0);
    if (california == nullptr)
        return page;

    california->setName(QStringLiteral("California"));
    california->style().setFillColor(QStringLiteral("#D8E5E1"));
    california->style().setFillOpacity(210);
    california->style().setLineColor(QStringLiteral("#6F8883"));
    california->style().setLineWidth(0.9f);

    const GisExtent extent = california->extent();
    QVector<GisShapePoint> rectangle
    {
        GisShapePoint(extent.xMin(), extent.yMin()),
        GisShapePoint(extent.xMax(), extent.yMin()),
        GisShapePoint(extent.xMax(), extent.yMax()),
        GisShapePoint(extent.xMin(), extent.yMax()),
        GisShapePoint(extent.xMin(), extent.yMin())
    };

    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#FFFFFF"));
    style.setFillOpacity(0);
    style.setLineColor(QStringLiteral("#E2453D"));
    style.setLineWidth(2.2f);

    viewer->addPolygonLayer(
        QStringLiteral("Layer Extent"),
        QVector<QVector<GisShapePoint>> { rectangle },
        style);

    status->setText(QStringLiteral("California extent: %1, %2 - %3, %4")
        .arg(extent.xMin(), 0, 'f', 3)
        .arg(extent.yMin(), 0, 'f', 3)
        .arg(extent.xMax(), 0, 'f', 3)
        .arg(extent.yMax(), 0, 'f', 3));

    scheduleMapFitAfterLayout(viewer, [viewer]
    {
        viewer->fullExtent();
    });

    return page;
}

void removeLayerByName(GisViewer& viewer, const QString& name);

QVector<GisShapePoint> galleryRouteShape(double offset)
{
    return
    {
        GisShapePoint(-122.4194 + offset, 37.7749),
        GisShapePoint(-118.2437 + offset, 34.0522),
        GisShapePoint(-112.0740 + offset, 33.4484),
        GisShapePoint(-104.9903 + offset, 39.7392)
    };
}

QVector<GisShapePoint> galleryRegionShape(double offset)
{
    return
    {
        GisShapePoint(-101.0 + offset, 30.0),
        GisShapePoint(-91.0 + offset, 30.0),
        GisShapePoint(-89.0 + offset, 37.0),
        GisShapePoint(-96.0 + offset, 42.0),
        GisShapePoint(-103.0 + offset, 38.0),
        GisShapePoint(-101.0 + offset, 30.0)
    };
}

GisShapePoint galleryGeneratedPoint(int index)
{
    constexpr int columns = 12;
    constexpr double startX = -124.0;
    constexpr double startY = 25.0;
    constexpr double stepX = 4.8;
    constexpr double stepY = 3.2;

    const int column = index % columns;
    const int row = index / columns;
    const double jitterX = (row % 3) * 0.35;
    const double jitterY = (column % 4) * 0.25;
    return GisShapePoint(startX + column * stepX + jitterX, startY + row * stepY + jitterY);
}

int galleryLayerIndexByName(const GisViewer& viewer, const QString& name)
{
    for (int i = 0; i < viewer.layerCount(); ++i)
    {
        const GisLayer* layer = viewer.mapLayerAt(i);
        if (layer != nullptr && layer->name() == name)
            return i;
    }

    return -1;
}

int galleryVectorFeatureCount(const GisViewer& viewer, const QString& name)
{
    const auto* layer = dynamic_cast<const GisLayerVector*>(viewer.mapLayerByName(name));
    return layer != nullptr ? layer->count() : 0;
}

void updateInMemoryStatus(const GisViewer& viewer, QLabel& status, const QString& message)
{
    status.setText(QStringLiteral("%1 Memory features - points: %2 | lines: %3 | polygons: %4")
        .arg(message)
        .arg(galleryVectorFeatureCount(viewer, QStringLiteral("Memory Cities")))
        .arg(galleryVectorFeatureCount(viewer, QStringLiteral("Memory Routes")))
        .arg(galleryVectorFeatureCount(viewer, QStringLiteral("Memory Regions"))));
}

void refreshInMemoryLayers(GisViewer& viewer)
{
    viewer.invalidateRenderCache(false, true);
    viewer.refreshLayers();
}

bool addGalleryPointToEditLayer(GisViewer& viewer, const QString& layerName, const GisShapePoint& point)
{
    const int index = galleryLayerIndexByName(viewer, layerName);
    return index >= 0
        && viewer.beginEditLayer(index)
        && viewer.addPointToEditLayer(index, point)
        && viewer.commitEditLayer(index);
}

bool addGalleryPolylineToEditLayer(GisViewer& viewer, const QString& layerName, const QVector<GisShapePoint>& points)
{
    const int index = galleryLayerIndexByName(viewer, layerName);
    return index >= 0
        && viewer.beginEditLayer(index)
        && viewer.addPolylineToEditLayer(index, points)
        && viewer.commitEditLayer(index);
}

bool addGalleryPolygonToEditLayer(GisViewer& viewer, const QString& layerName, const QVector<GisShapePoint>& points)
{
    const int index = galleryLayerIndexByName(viewer, layerName);
    return index >= 0
        && viewer.beginEditLayer(index)
        && viewer.addPolygonToEditLayer(index, points)
        && viewer.commitEditLayer(index);
}

void createInMemoryGalleryLayers(GisViewer& viewer)
{
    GisLayerStyle polygonStyle;
    polygonStyle.setFillColor(QStringLiteral("#F1D58A"));
    polygonStyle.setFillOpacity(150);
    polygonStyle.setLineColor(QStringLiteral("#9A7A1F"));
    polygonStyle.setLineWidth(1.5f);

    GisLayerStyle lineStyle;
    lineStyle.setLineColor(QStringLiteral("#266D8F"));
    lineStyle.setLineWidth(2.2f);

    GisLayerStyle pointStyle;
    pointStyle.setPointColor(QStringLiteral("#D95F35"));
    pointStyle.setPointSize(7.0f);

    viewer.addPolygonLayer(
        QStringLiteral("Memory Regions"),
        QVector<QVector<GisShapePoint>> { galleryRegionShape(0.0) },
        polygonStyle);
    viewer.addPolylineLayer(
        QStringLiteral("Memory Routes"),
        QVector<QVector<GisShapePoint>> { galleryRouteShape(0.0) },
        lineStyle);
    viewer.addPointLayer(
        QStringLiteral("Memory Cities"),
        QVector<GisShapePoint>
        {
            GisShapePoint(-122.4194, 37.7749),
            GisShapePoint(-118.2437, 34.0522)
        },
        pointStyle);
}

QWidget* createInMemoryLayersLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* toolbar = new QWidget(page);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(4, 4, 4, 4);
    toolbarLayout->setSpacing(4);

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    auto* status = new QLabel(QStringLiteral("Ready"), page);
    status->setStyleSheet(QStringLiteral("background: #f4f4f4; border-top: 1px solid #d0d0d0; padding-left: 4px;"));

    const auto addButton = [toolbarLayout, toolbar](const QString& text)
    {
        auto* button = new QPushButton(text, toolbar);
        toolbarLayout->addWidget(button);
        return button;
    };

    QPushButton* addPoint = addButton(QStringLiteral("Add Point"));
    QPushButton* addLine = addButton(QStringLiteral("Add Line"));
    QPushButton* addPolygon = addButton(QStringLiteral("Add Polygon"));
    QPushButton* clearMemory = addButton(QStringLiteral("Clear Memory Layers"));
    QPushButton* fullExtent = addButton(QStringLiteral("Full Extent"));
    toolbarLayout->addStretch(1);

    layout->addWidget(toolbar);
    layout->addWidget(viewer, 1);
    layout->addWidget(status);

    QString errorMessage;
    const QString worldPath = repositoryPath(QStringLiteral("assets/data/shapefile/world_4326.shp"));
    if (!viewer->addLayerFromPath(worldPath, &errorMessage))
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("InMemoryLayers world layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? worldPath : errorMessage));
        return page;
    }

    if (GisLayer* world = viewer->mapLayerAt(0))
    {
        world->setName(QStringLiteral("World"));
        world->style().setFillColor(QStringLiteral("#D8E5E1"));
        world->style().setFillOpacity(210);
        world->style().setLineColor(QStringLiteral("#6F8883"));
        world->style().setLineWidth(0.7f);
    }

    createInMemoryGalleryLayers(*viewer);

    auto pointCursor = std::make_shared<int>(0);
    auto lineCursor = std::make_shared<int>(1);
    auto polygonCursor = std::make_shared<int>(1);

    QObject::connect(addPoint, &QPushButton::clicked, viewer, [viewer, status, pointCursor]
    {
        if (addGalleryPointToEditLayer(*viewer, QStringLiteral("Memory Cities"), galleryGeneratedPoint(*pointCursor)))
        {
            ++(*pointCursor);
            refreshInMemoryLayers(*viewer);
            updateInMemoryStatus(*viewer, *status, QStringLiteral("Point added."));
        }
        else
        {
            updateInMemoryStatus(*viewer, *status, QStringLiteral("Point could not be added."));
        }
    });

    QObject::connect(addLine, &QPushButton::clicked, viewer, [viewer, status, lineCursor]
    {
        if (addGalleryPolylineToEditLayer(*viewer, QStringLiteral("Memory Routes"), galleryRouteShape(*lineCursor * 2.0)))
        {
            ++(*lineCursor);
            refreshInMemoryLayers(*viewer);
            updateInMemoryStatus(*viewer, *status, QStringLiteral("Line added."));
        }
        else
        {
            updateInMemoryStatus(*viewer, *status, QStringLiteral("Line could not be added."));
        }
    });

    QObject::connect(addPolygon, &QPushButton::clicked, viewer, [viewer, status, polygonCursor]
    {
        if (addGalleryPolygonToEditLayer(*viewer, QStringLiteral("Memory Regions"), galleryRegionShape(*polygonCursor * 5.0)))
        {
            ++(*polygonCursor);
            refreshInMemoryLayers(*viewer);
            updateInMemoryStatus(*viewer, *status, QStringLiteral("Polygon added."));
        }
        else
        {
            updateInMemoryStatus(*viewer, *status, QStringLiteral("Polygon could not be added."));
        }
    });

    QObject::connect(clearMemory, &QPushButton::clicked, viewer, [viewer, status, pointCursor, lineCursor, polygonCursor]
    {
        removeLayerByName(*viewer, QStringLiteral("Memory Cities"));
        removeLayerByName(*viewer, QStringLiteral("Memory Routes"));
        removeLayerByName(*viewer, QStringLiteral("Memory Regions"));
        createInMemoryGalleryLayers(*viewer);
        *pointCursor = 0;
        *lineCursor = 1;
        *polygonCursor = 1;
        refreshInMemoryLayers(*viewer);
        updateInMemoryStatus(*viewer, *status, QStringLiteral("Memory layers reset."));
    });

    QObject::connect(fullExtent, &QPushButton::clicked, viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));
    });

    scheduleMapFitAfterLayout(viewer, [viewer, status]
    {
        viewer->setViewExtent(GisExtent(-130.0, 20.0, -65.0, 52.0));
        updateInMemoryStatus(*viewer, *status, QStringLiteral("Memory layers created."));
    });

    return page;
}

void applyGalleryLayerRefreshBaseStyle(GisLayer& layer)
{
    layer.setName(QStringLiteral("California"));
    layer.style().setFillColor(QStringLiteral("#D8E5E1"));
    layer.style().setFillOpacity(210);
    layer.style().setLineColor(QStringLiteral("#6F8883"));
    layer.style().setLineWidth(0.9f);
}

QWidget* createLayerRefreshLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* toolbar = new QWidget(page);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(4, 4, 4, 4);
    toolbarLayout->setSpacing(4);

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    auto* status = new QLabel(QStringLiteral("Ready"), page);
    status->setStyleSheet(QStringLiteral("background: #f4f4f4; border-top: 1px solid #d0d0d0; padding-left: 4px;"));

    const auto addButton = [toolbarLayout, toolbar](const QString& text)
    {
        auto* button = new QPushButton(text, toolbar);
        toolbarLayout->addWidget(button);
        return button;
    };

    QPushButton* fillButton = addButton(QStringLiteral("Change Fill"));
    QPushButton* outlineButton = addButton(QStringLiteral("Change Outline"));
    QPushButton* opacityButton = addButton(QStringLiteral("Change Opacity"));
    QPushButton* refreshButton = addButton(QStringLiteral("Refresh Layer"));
    QPushButton* fullExtentButton = addButton(QStringLiteral("Full Extent"));
    toolbarLayout->addStretch(1);

    layout->addWidget(toolbar);
    layout->addWidget(viewer, 1);
    layout->addWidget(status);

    QString errorMessage;
    const QString path = repositoryPath(QStringLiteral("assets/data/california/california.shp"));
    if (!viewer->addLayerFromPath(path, &errorMessage))
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("LayerRefresh layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? path : errorMessage));
        return page;
    }

    GisLayer* california = viewer->mapLayerAt(0);
    if (california == nullptr)
        return page;

    applyGalleryLayerRefreshBaseStyle(*california);

    const auto fillColors = std::make_shared<std::array<QString, 4>>(std::array<QString, 4>
    {
        QStringLiteral("#D8E5E1"),
        QStringLiteral("#D9C7A5"),
        QStringLiteral("#C7D7EA"),
        QStringLiteral("#D7C5DE")
    });
    const auto outlineColors = std::make_shared<std::array<QString, 4>>(std::array<QString, 4>
    {
        QStringLiteral("#6F8883"),
        QStringLiteral("#A24A3D"),
        QStringLiteral("#356780"),
        QStringLiteral("#6F4D8C")
    });
    const auto opacities = std::make_shared<std::array<int, 4>>(std::array<int, 4> { 210, 160, 110, 235 });

    auto fillIndex = std::make_shared<int>(0);
    auto outlineIndex = std::make_shared<int>(0);
    auto opacityIndex = std::make_shared<int>(0);

    QObject::connect(fillButton, &QPushButton::clicked, viewer, [california, status, fillColors, fillIndex]
    {
        *fillIndex = (*fillIndex + 1) % static_cast<int>(fillColors->size());
        california->style().setFillColor((*fillColors)[*fillIndex]);
        status->setText(QStringLiteral("Fill changed. Press Refresh Layer to redraw."));
    });

    QObject::connect(outlineButton, &QPushButton::clicked, viewer, [california, status, outlineColors, outlineIndex]
    {
        *outlineIndex = (*outlineIndex + 1) % static_cast<int>(outlineColors->size());
        california->style().setLineColor((*outlineColors)[*outlineIndex]);
        california->style().setLineWidth(*outlineIndex == 0 ? 0.9f : 1.6f);
        status->setText(QStringLiteral("Outline changed. Press Refresh Layer to redraw."));
    });

    QObject::connect(opacityButton, &QPushButton::clicked, viewer, [california, status, opacities, opacityIndex]
    {
        *opacityIndex = (*opacityIndex + 1) % static_cast<int>(opacities->size());
        california->style().setFillOpacity((*opacities)[*opacityIndex]);
        status->setText(QStringLiteral("Opacity changed. Press Refresh Layer to redraw."));
    });

    QObject::connect(refreshButton, &QPushButton::clicked, viewer, [viewer, status]
    {
        viewer->refreshLayers();
        status->setText(QStringLiteral("Layer refreshed."));
    });

    QObject::connect(fullExtentButton, &QPushButton::clicked, viewer, &GisViewer::fullExtent);

    scheduleMapFitAfterLayout(viewer, [viewer, status]
    {
        viewer->refreshLayers();
        viewer->fullExtent();
        status->setText(QStringLiteral("Layer loaded. Change style, then press Refresh Layer."));
    });

    return page;
}

QString layerVisibilityText(const GisViewer& viewer, int index)
{
    const GisLayer* layer = viewer.mapLayerAt(index);
    if (layer == nullptr)
        return QString();

    return QStringLiteral("%1 %2")
        .arg(layer->visible() ? QStringLiteral("[x]") : QStringLiteral("[ ]"))
        .arg(viewer.layerDisplayText(index));
}

void refreshLayerVisibilityList(const GisViewer& viewer, QListWidget& list, int selectedIndex = -1)
{
    list.clear();
    for (int i = 0; i < viewer.layerCount(); ++i)
        list.addItem(layerVisibilityText(viewer, i));

    if (selectedIndex >= 0 && selectedIndex < list.count())
        list.setCurrentRow(selectedIndex);
    else if (list.count() > 0)
        list.setCurrentRow(0);
}

void updateLayerVisibilityButton(const GisViewer& viewer, const QListWidget& list, QPushButton& button)
{
    const int index = list.currentRow();
    const GisLayer* layer = viewer.mapLayerAt(index);
    if (layer == nullptr)
    {
        button.setEnabled(false);
        button.setText(QStringLiteral("Change Visibility"));
        return;
    }

    button.setEnabled(true);
    button.setText(layer->visible()
        ? QStringLiteral("Change Visibility: Hide")
        : QStringLiteral("Change Visibility: Show"));
}

void toggleLayerVisibility(GisViewer& viewer, QListWidget& list, QPushButton& button)
{
    const int index = list.currentRow();
    GisLayer* layer = viewer.mapLayerAt(index);
    if (layer == nullptr)
        return;

    viewer.setLayerVisible(index, !layer->visible());
    refreshLayerVisibilityList(viewer, list, index);
    updateLayerVisibilityButton(viewer, list, button);
}

QWidget* createLayerVisibilityLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* content = new QWidget(page);
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    auto* sidePanel = new QWidget(content);
    sidePanel->setFixedWidth(220);
    auto* sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(8, 8, 8, 8);
    sideLayout->setSpacing(8);

    auto* layerList = new QListWidget(sidePanel);
    auto* visibilityButton = new QPushButton(QStringLiteral("Change Visibility"), sidePanel);
    sideLayout->addWidget(layerList, 1);
    sideLayout->addWidget(visibilityButton);

    auto* viewer = new GisViewer(content);
    viewer->setActiveTool(GisViewerTool::Pan);

    contentLayout->addWidget(sidePanel);
    contentLayout->addWidget(viewer, 1);

    layout->addWidget(createMapToolbar(*viewer, page));
    layout->addWidget(content, 1);

    if (!loadStyledFileLayer(*viewer, QStringLiteral("assets/data/shapefile/world_4326.shp")) ||
        !loadStyledFileLayer(*viewer, QStringLiteral("assets/data/us_state_boundaries.shp")) ||
        !loadStyledFileLayer(*viewer, QStringLiteral("assets/data/kml/usa_cities_4326.kml")))
    {
        return page;
    }

    if (GisLayer* layer = viewer->mapLayerAt(0))
        layer->setName(QStringLiteral("Cities"));
    if (GisLayer* layer = viewer->mapLayerAt(1))
        layer->setName(QStringLiteral("States"));
    if (GisLayer* layer = viewer->mapLayerAt(2))
        layer->setName(QStringLiteral("World"));

    refreshLayerVisibilityList(*viewer, *layerList);
    updateLayerVisibilityButton(*viewer, *layerList, *visibilityButton);

    QObject::connect(layerList, &QListWidget::currentRowChanged, visibilityButton, [viewer, layerList, visibilityButton]
    {
        updateLayerVisibilityButton(*viewer, *layerList, *visibilityButton);
    });

    QObject::connect(visibilityButton, &QPushButton::clicked, viewer, [viewer, layerList, visibilityButton]
    {
        toggleLayerVisibility(*viewer, *layerList, *visibilityButton);
    });

    QObject::connect(viewer, &GisViewer::layersChanged, layerList, [viewer, layerList, visibilityButton]
    {
        const int selectedIndex = layerList->currentRow();
        refreshLayerVisibilityList(*viewer, *layerList, selectedIndex);
        updateLayerVisibilityButton(*viewer, *layerList, *visibilityButton);
    });

    scheduleMapFitAfterLayout(viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-151.2, 16.4, -41.6, 55.6));
    });

    return page;
}

QString galleryLayerEventName(const GisLayer* layer)
{
    return layer != nullptr && !layer->name().isEmpty()
        ? layer->name()
        : QStringLiteral("<none>");
}

void appendLayerEventLog(QPlainTextEdit& log, const QString& text)
{
    log.appendPlainText(QStringLiteral("%1  %2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")))
        .arg(text));
}

void configureGalleryLayerEventsLayer(GisLayer& layer, const QString& name)
{
    if (name == QStringLiteral("Cities"))
    {
        layer.style().setPointColor(QStringLiteral("#D95D39"));
        layer.style().setPointSize(7.0f);
        layer.style().setLineColor(QStringLiteral("#D95D39"));
        layer.style().setLineWidth(1.5f);
        return;
    }

    if (name == QStringLiteral("States"))
    {
        layer.style().setFillColor(QStringLiteral("#A9C8DB"));
        layer.style().setFillOpacity(115);
        layer.style().setLineColor(QStringLiteral("#356780"));
        layer.style().setLineWidth(1.2f);
        return;
    }

    layer.style().setFillColor(QStringLiteral("#D8E5E1"));
    layer.style().setFillOpacity(220);
    layer.style().setLineColor(QStringLiteral("#7B918D"));
    layer.style().setLineWidth(0.8f);
}

bool addGalleryLayerEventsLayer(
    GisViewer& viewer,
    QPlainTextEdit& log,
    const QString& name,
    const QString& relativePath)
{
    if (galleryLayerIndexByName(viewer, name) >= 0)
    {
        appendLayerEventLog(log, QStringLiteral("Action skipped: %1 already exists").arg(name));
        return true;
    }

    appendLayerEventLog(log, QStringLiteral("Action: addLayerFromPath(%1)").arg(relativePath));
    if (!loadStyledFileLayer(viewer, relativePath))
        return false;

    if (GisLayer* layer = viewer.mapLayerAt(0))
    {
        layer->setName(name);
        configureGalleryLayerEventsLayer(*layer, name);
    }

    viewer.refreshLayers();
    return true;
}

void refreshGalleryLayerEventsList(const GisViewer& viewer, QListWidget& list, int selectedIndex = -1)
{
    list.clear();
    for (int i = 0; i < viewer.layerCount(); ++i)
    {
        const GisLayer* layer = viewer.mapLayerAt(i);
        const QString visibleState = layer != nullptr && layer->visible() ? QStringLiteral("[x]") : QStringLiteral("[ ]");
        list.addItem(QStringLiteral("%1 %2").arg(visibleState, viewer.layerDisplayText(i)));
    }

    if (selectedIndex >= 0 && selectedIndex < list.count())
        list.setCurrentRow(selectedIndex);
    else if (list.count() > 0)
        list.setCurrentRow(0);
}

void connectGalleryLayerEventsSignals(GisViewer& viewer, QPlainTextEdit& log, QListWidget& layerList)
{
    QObject::connect(&viewer, &GisViewer::layerAdding, &log, [&log](const QString& name)
    {
        appendLayerEventLog(log, QStringLiteral("Signal: layerAdding(%1)").arg(name));
    });

    QObject::connect(&viewer, &GisViewer::layerAdded, &log, [&log](GisLayer* layer)
    {
        appendLayerEventLog(log, QStringLiteral("Signal: layerAdded(%1)").arg(galleryLayerEventName(layer)));
    });

    QObject::connect(&viewer, &GisViewer::layerRemoving, &log, [&log](GisLayer* layer)
    {
        appendLayerEventLog(log, QStringLiteral("Signal: layerRemoving(%1)").arg(galleryLayerEventName(layer)));
    });

    QObject::connect(&viewer, &GisViewer::layerRemoved, &log, [&log](const QString& name)
    {
        appendLayerEventLog(log, QStringLiteral("Signal: layerRemoved(%1)").arg(name));
    });

    QObject::connect(&viewer, &GisViewer::layerVisibilityChanged, &log, [&log](GisLayer* layer, bool visible)
    {
        appendLayerEventLog(log, QStringLiteral("Signal: layerVisibilityChanged(%1, %2)")
            .arg(galleryLayerEventName(layer), visible ? QStringLiteral("true") : QStringLiteral("false")));
    });

    QObject::connect(&viewer, &GisViewer::layerEditStateChanged, &log, [&log](GisLayer* layer)
    {
        appendLayerEventLog(log, QStringLiteral("Signal: layerEditStateChanged(%1)").arg(galleryLayerEventName(layer)));
    });

    QObject::connect(&viewer, &GisViewer::layerEditSessionStarted, &log, [&log](GisLayer* layer)
    {
        appendLayerEventLog(log, QStringLiteral("Signal: layerEditSessionStarted(%1)").arg(galleryLayerEventName(layer)));
    });

    QObject::connect(&viewer, &GisViewer::layerEditSessionCommitted, &log, [&log](GisLayer* layer)
    {
        appendLayerEventLog(log, QStringLiteral("Signal: layerEditSessionCommitted(%1)").arg(galleryLayerEventName(layer)));
    });

    QObject::connect(&viewer, &GisViewer::layerEditSessionRolledBack, &log, [&log](GisLayer* layer)
    {
        appendLayerEventLog(log, QStringLiteral("Signal: layerEditSessionRolledBack(%1)").arg(galleryLayerEventName(layer)));
    });

    QObject::connect(&viewer, &GisViewer::layerOrderChanged, &log, [&log]
    {
        appendLayerEventLog(log, QStringLiteral("Signal: layerOrderChanged()"));
    });

    QObject::connect(&viewer, &GisViewer::layersChanged, &layerList, [&viewer, &log, &layerList]
    {
        const int selectedIndex = layerList.currentRow();
        refreshGalleryLayerEventsList(viewer, layerList, selectedIndex);
        appendLayerEventLog(log, QStringLiteral("Signal: layersChanged(count=%1)").arg(viewer.layerCount()));
    });
}

QWidget* createLayerEventsLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* content = new QWidget(page);
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    auto* sidePanel = new QWidget(content);
    sidePanel->setFixedWidth(300);
    auto* sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(8, 8, 8, 8);
    sideLayout->setSpacing(6);

    auto* layerList = new QListWidget(sidePanel);
    auto* log = new QPlainTextEdit(sidePanel);
    log->setReadOnly(true);
    log->setFont(QFont(QStringLiteral("Cascadia Mono"), 9));
    log->setLineWrapMode(QPlainTextEdit::NoWrap);

    auto* viewer = new GisViewer(content);
    viewer->setActiveTool(GisViewerTool::Pan);

    const auto addButton = [sideLayout, sidePanel](const QString& text)
    {
        auto* button = new QPushButton(text, sidePanel);
        sideLayout->addWidget(button);
        return button;
    };

    QPushButton* addWorld = addButton(QStringLiteral("Add World"));
    QPushButton* addStates = addButton(QStringLiteral("Add States"));
    QPushButton* addCities = addButton(QStringLiteral("Add Cities"));
    QPushButton* removeSelected = addButton(QStringLiteral("Remove Selected"));
    QPushButton* clearLayers = addButton(QStringLiteral("Clear Layers"));
    QPushButton* toggleVisibility = addButton(QStringLiteral("Toggle Visibility"));
    QPushButton* moveUp = addButton(QStringLiteral("Move Up"));
    QPushButton* moveDown = addButton(QStringLiteral("Move Down"));
    QPushButton* refresh = addButton(QStringLiteral("Refresh"));
    QPushButton* clearLog = addButton(QStringLiteral("Clear Log"));

    sideLayout->addWidget(layerList, 1);
    sideLayout->addWidget(log, 2);

    contentLayout->addWidget(sidePanel);
    contentLayout->addWidget(viewer, 1);

    layout->addWidget(createMapToolbar(*viewer, page));
    layout->addWidget(content, 1);

    connectGalleryLayerEventsSignals(*viewer, *log, *layerList);

    QObject::connect(addWorld, &QPushButton::clicked, viewer, [viewer, log]
    {
        addGalleryLayerEventsLayer(*viewer, *log, QStringLiteral("World"), QStringLiteral("assets/data/shapefile/world_4326.shp"));
    });

    QObject::connect(addStates, &QPushButton::clicked, viewer, [viewer, log]
    {
        addGalleryLayerEventsLayer(*viewer, *log, QStringLiteral("States"), QStringLiteral("assets/data/us_state_boundaries.shp"));
    });

    QObject::connect(addCities, &QPushButton::clicked, viewer, [viewer, log]
    {
        addGalleryLayerEventsLayer(*viewer, *log, QStringLiteral("Cities"), QStringLiteral("assets/data/kml/usa_cities_4326.kml"));
    });

    QObject::connect(removeSelected, &QPushButton::clicked, viewer, [viewer, log, layerList]
    {
        const int index = layerList->currentRow();
        const GisLayer* layer = viewer->mapLayerAt(index);
        if (layer == nullptr)
            return;

        appendLayerEventLog(*log, QStringLiteral("Action: removeLayer(%1)").arg(galleryLayerEventName(layer)));
        viewer->removeLayer(index);
    });

    QObject::connect(clearLayers, &QPushButton::clicked, viewer, [viewer, log]
    {
        appendLayerEventLog(*log, QStringLiteral("Action: clearLayers()"));
        viewer->clearLayers();
    });

    QObject::connect(toggleVisibility, &QPushButton::clicked, viewer, [viewer, log, layerList]
    {
        const int index = layerList->currentRow();
        GisLayer* layer = viewer->mapLayerAt(index);
        if (layer == nullptr)
            return;

        appendLayerEventLog(*log, QStringLiteral("Action: setLayerVisible(%1, %2)")
            .arg(galleryLayerEventName(layer), layer->visible() ? QStringLiteral("false") : QStringLiteral("true")));
        viewer->setLayerVisible(index, !layer->visible());
    });

    QObject::connect(moveUp, &QPushButton::clicked, viewer, [viewer, log, layerList]
    {
        const int fromIndex = layerList->currentRow();
        const int toIndex = fromIndex - 1;
        if (fromIndex < 0 || toIndex < 0)
            return;

        appendLayerEventLog(*log, QStringLiteral("Action: moveLayer(%1 -> %2)").arg(fromIndex).arg(toIndex));
        if (viewer->moveLayer(fromIndex, toIndex))
            refreshGalleryLayerEventsList(*viewer, *layerList, toIndex);
    });

    QObject::connect(moveDown, &QPushButton::clicked, viewer, [viewer, log, layerList]
    {
        const int fromIndex = layerList->currentRow();
        const int toIndex = fromIndex + 1;
        if (fromIndex < 0 || toIndex >= viewer->layerCount())
            return;

        appendLayerEventLog(*log, QStringLiteral("Action: moveLayer(%1 -> %2)").arg(fromIndex).arg(toIndex));
        if (viewer->moveLayer(fromIndex, toIndex))
            refreshGalleryLayerEventsList(*viewer, *layerList, toIndex);
    });

    QObject::connect(refresh, &QPushButton::clicked, viewer, [viewer, log]
    {
        appendLayerEventLog(*log, QStringLiteral("Action: refreshLayers()"));
        viewer->refreshLayers();
    });

    QObject::connect(clearLog, &QPushButton::clicked, log, [log]
    {
        log->clear();
    });

    addGalleryLayerEventsLayer(*viewer, *log, QStringLiteral("World"), QStringLiteral("assets/data/shapefile/world_4326.shp"));
    addGalleryLayerEventsLayer(*viewer, *log, QStringLiteral("States"), QStringLiteral("assets/data/us_state_boundaries.shp"));
    addGalleryLayerEventsLayer(*viewer, *log, QStringLiteral("Cities"), QStringLiteral("assets/data/kml/usa_cities_4326.kml"));
    refreshGalleryLayerEventsList(*viewer, *layerList);

    scheduleMapFitAfterLayout(viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-151.2, 16.4, -41.6, 55.6));
    });

    return page;
}

struct LayerLoadQueryResult
{
    int queryCount = 0;
    int hitCount = 0;
    qint64 elapsedMs = 0;
};

enum class LayerLoadIndexMode
{
    None,
    NoIndex,
    RTree
};

struct LayerLoadCancelState
{
    bool cancelRequested = false;
    bool isLoading = false;
    bool isPumpingMessages = false;
};

QString spatialIndexStateText(SpatialIndexPreparationState state)
{
    switch (state)
    {
    case SpatialIndexPreparationState::Loading:
        return QStringLiteral("Spatial index is loading...");
    case SpatialIndexPreparationState::BuildingLocator:
        return QStringLiteral("Spatial locator is preparing...");
    case SpatialIndexPreparationState::BuildingIndex:
        return QStringLiteral("Spatial index is building...");
    case SpatialIndexPreparationState::Ready:
        return QStringLiteral("Spatial index is ready.");
    case SpatialIndexPreparationState::Cancelled:
        return QStringLiteral("Spatial index cancelled.");
    case SpatialIndexPreparationState::Failed:
        return QStringLiteral("Spatial index failed.");
    case SpatialIndexPreparationState::None:
    default:
        return QStringLiteral("Spatial index idle.");
    }
}

LayerLoadOptions createLayerLoadGalleryOptions(bool useSpatialIndex, QProgressBar& progressBar, QLabel& statusLabel)
{
    LayerLoadOptions options;
    options.useSpatialIndex = useSpatialIndex;
    options.spatialIndexType = SpatialIndexType::RTree;
    options.buildFeatureSource = true;
    options.applyDefaultStyle = true;
    options.defaultStyle.setFillColor(QStringLiteral("#D8E5E1"));
    options.defaultStyle.setFillOpacity(210);
    options.defaultStyle.setLineColor(QStringLiteral("#607D78"));
    options.defaultStyle.setLineWidth(0.9f);

    options.progress = [&progressBar](int value)
    {
        progressBar.setValue(std::clamp(value, 0, 100));
        QApplication::processEvents();
    };
    options.status = [&statusLabel](const QString& text)
    {
        statusLabel.setText(text);
        QApplication::processEvents();
    };
    options.indexStateChanged = [&statusLabel](SpatialIndexPreparationState state)
    {
        statusLabel.setText(spatialIndexStateText(state));
        QApplication::processEvents();
    };

    return options;
}

LayerLoadOptions createLayerLoadCancelGalleryOptions(
    LayerLoadCancelState& state,
    QProgressBar& progressBar,
    QLabel& statusLabel)
{
    LayerLoadOptions options;
    options.useSpatialIndex = true;
    options.spatialIndexType = SpatialIndexType::RTree;
    options.buildFeatureSource = true;
    options.applyDefaultStyle = true;
    options.defaultStyle.setFillColor(QStringLiteral("#D8E5E1"));
    options.defaultStyle.setFillOpacity(210);
    options.defaultStyle.setLineColor(QStringLiteral("#607D78"));
    options.defaultStyle.setLineWidth(0.9f);
    options.defaultStyle.setPointColor(QStringLiteral("#2D82B7"));
    options.defaultStyle.setPointSize(3.5f);

    options.progress = [&progressBar](int value)
    {
        progressBar.setValue(std::clamp(value, 0, 100));
    };
    options.status = [&statusLabel](const QString& text)
    {
        statusLabel.setText(text);
    };
    options.isCancellationRequested = [&state]()
    {
        if (!state.isPumpingMessages)
        {
            state.isPumpingMessages = true;
            QApplication::processEvents();
            state.isPumpingMessages = false;
        }

        return state.cancelRequested;
    };
    options.indexStateChanged = [&statusLabel](SpatialIndexPreparationState state)
    {
        statusLabel.setText(spatialIndexStateText(state));
    };

    return options;
}

bool loadLayerLoadCancelLayer(
    GisViewer& viewer,
    LayerLoadCancelState& state,
    QPushButton& loadButton,
    QPushButton& cancelButton,
    QPushButton& clearButton,
    QProgressBar& progressBar,
    QLabel& statusLabel)
{
    state.cancelRequested = false;
    state.isLoading = true;
    loadButton.setEnabled(false);
    cancelButton.setEnabled(true);
    clearButton.setEnabled(false);
    progressBar.setValue(0);
    statusLabel.setText(QStringLiteral("Layer load started..."));

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QElapsedTimer timer;
    timer.start();

    viewer.clearLayers();
    QString errorMessage;
    const QString path = repositoryPath(QStringLiteral("assets/data/output_1m_points.shp"));
    const bool loaded = viewer.addLayerFromPath(
        path,
        createLayerLoadCancelGalleryOptions(state, progressBar, statusLabel),
        &errorMessage);

    QApplication::restoreOverrideCursor();
    state.isLoading = false;
    loadButton.setEnabled(true);
    cancelButton.setEnabled(false);
    clearButton.setEnabled(true);

    if (state.cancelRequested)
    {
        progressBar.setValue(0);
        statusLabel.setText(QStringLiteral("Layer load cancelled after %1 ms.").arg(timer.elapsed()));
        return false;
    }

    if (!loaded)
    {
        progressBar.setValue(0);
        statusLabel.setText(QStringLiteral("Layer load failed."));
        QMessageBox::critical(
            &viewer,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("LayerLoadCancel layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? path : errorMessage));
        return false;
    }

    if (GisLayer* layer = viewer.mapLayerAt(0))
        layer->setName(QStringLiteral("One Million Points"));

    viewer.fullExtent();
    progressBar.setValue(100);
    statusLabel.setText(QStringLiteral("Layer loaded in %1 ms.").arg(timer.elapsed()));
    return true;
}

QWidget* createLayerLoadCancelLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* toolbar = new QWidget(page);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(6, 4, 6, 4);
    toolbarLayout->setSpacing(8);

    auto* loadButton = new QPushButton(QStringLiteral("Load Large Layer"), toolbar);
    auto* cancelButton = new QPushButton(QStringLiteral("Cancel"), toolbar);
    auto* clearButton = new QPushButton(QStringLiteral("Clear"), toolbar);
    cancelButton->setEnabled(false);
    toolbarLayout->addWidget(loadButton);
    toolbarLayout->addWidget(cancelButton);
    toolbarLayout->addWidget(clearButton);
    toolbarLayout->addStretch(1);

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    auto* statusBar = new QWidget(page);
    auto* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(6, 3, 6, 3);
    auto* statusLabel = new QLabel(QStringLiteral("Press Load Large Layer, then Cancel while loading."), statusBar);
    auto* progressBar = new QProgressBar(statusBar);
    progressBar->setRange(0, 100);
    progressBar->setFixedWidth(220);
    statusLayout->addWidget(statusLabel, 1);
    statusLayout->addWidget(progressBar);

    layout->addWidget(toolbar);
    layout->addWidget(viewer, 1);
    layout->addWidget(statusBar);

    auto state = std::make_shared<LayerLoadCancelState>();

    QObject::connect(loadButton, &QPushButton::clicked, viewer, [viewer, state, loadButton, cancelButton, clearButton, progressBar, statusLabel]
    {
        loadLayerLoadCancelLayer(*viewer, *state, *loadButton, *cancelButton, *clearButton, *progressBar, *statusLabel);
    });
    QObject::connect(cancelButton, &QPushButton::clicked, viewer, [state, cancelButton, statusLabel]
    {
        if (!state->isLoading)
            return;

        state->cancelRequested = true;
        cancelButton->setEnabled(false);
        statusLabel->setText(QStringLiteral("Cancel requested..."));
    });
    QObject::connect(clearButton, &QPushButton::clicked, viewer, [viewer, state, loadButton, cancelButton, progressBar, statusLabel]
    {
        if (state->isLoading)
            return;

        state->cancelRequested = false;
        viewer->clearLayers();
        progressBar->setValue(0);
        loadButton->setEnabled(true);
        cancelButton->setEnabled(false);
        statusLabel->setText(QStringLiteral("Layers cleared."));
    });

    return page;
}

bool loadLayerLoadOptionsLayer(
    GisViewer& viewer,
    bool useSpatialIndex,
    QProgressBar& progressBar,
    QLabel& statusLabel)
{
    progressBar.setValue(0);
    statusLabel.setText(useSpatialIndex
        ? QStringLiteral("Loading USA states with RTree...")
        : QStringLiteral("Loading USA states without spatial index..."));

    QElapsedTimer timer;
    timer.start();

    viewer.clearLayers();
    QString errorMessage;
    const QString path = repositoryPath(QStringLiteral("assets/data/shapefile/usa_states_3857.shp"));
    if (!viewer.addLayerFromPath(path, createLayerLoadGalleryOptions(useSpatialIndex, progressBar, statusLabel), &errorMessage))
    {
        QMessageBox::critical(
            &viewer,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("LayerLoadOptions layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? path : errorMessage));
        statusLabel.setText(QStringLiteral("Load failed."));
        return false;
    }

    if (GisLayer* layer = viewer.mapLayerAt(0))
        layer->setName(useSpatialIndex ? QStringLiteral("USA States - RTree") : QStringLiteral("USA States - No Index"));

    viewer.setViewExtent(GisExtent(-16831516, 1856556, -4631023, 7472472));
    progressBar.setValue(100);
    statusLabel.setText(useSpatialIndex
        ? QStringLiteral("RTree layer loaded. Load time: %1 ms.").arg(timer.elapsed())
        : QStringLiteral("No-index layer loaded. Load time: %1 ms.").arg(timer.elapsed()));
    return true;
}

LayerLoadQueryResult runLayerLoadOptionsQueryBenchmark(
    GisViewer& viewer,
    LayerLoadIndexMode mode,
    QProgressBar& progressBar,
    QLabel& statusLabel,
    QLabel& noIndexResult,
    QLabel& rtreeResult)
{
    LayerLoadQueryResult result;

    const GisLayer* layer = viewer.mapLayerAt(0);
    if (layer == nullptr)
    {
        statusLabel.setText(QStringLiteral("Load a vector layer first."));
        return result;
    }

    const GisExtent extent = layer->extent();
    if (extent.isEmpty())
    {
        statusLabel.setText(QStringLiteral("Layer extent is empty."));
        return result;
    }

    constexpr int rows = 5;
    constexpr int columns = 8;
    constexpr int passes = 6;
    const double stepX = extent.width() / columns;
    const double stepY = extent.height() / rows;
    const double queryWidth = stepX * 0.65;
    const double queryHeight = stepY * 0.65;
    const int totalQueries = rows * columns * passes;
    int completed = 0;
    int totalHits = 0;

    progressBar.setValue(0);
    statusLabel.setText(QStringLiteral("Running query benchmark..."));
    QApplication::processEvents();

    QElapsedTimer timer;
    timer.start();
    for (int pass = 0; pass < passes; ++pass)
    {
        for (int row = 0; row < rows; ++row)
        {
            for (int column = 0; column < columns; ++column)
            {
                const double xMin = extent.xMin() + column * stepX;
                const double yMin = extent.yMin() + row * stepY;
                totalHits += viewer.hitTestFeaturesInExtent(GisExtent(
                    xMin,
                    yMin,
                    xMin + queryWidth,
                    yMin + queryHeight)).size();

                ++completed;
                if (completed % 16 == 0)
                {
                    progressBar.setValue((completed * 100) / totalQueries);
                    QApplication::processEvents();
                }
            }
        }
    }

    result.queryCount = totalQueries;
    result.hitCount = totalHits;
    result.elapsedMs = timer.elapsed();
    progressBar.setValue(100);

    const QString resultText = QStringLiteral("%1: query %2 ms, %3 queries, %4 hits")
        .arg(mode == LayerLoadIndexMode::RTree ? QStringLiteral("RTree") : QStringLiteral("No Index"))
        .arg(result.elapsedMs)
        .arg(result.queryCount)
        .arg(result.hitCount);

    if (mode == LayerLoadIndexMode::RTree)
        rtreeResult.setText(resultText);
    else if (mode == LayerLoadIndexMode::NoIndex)
        noIndexResult.setText(resultText);

    statusLabel.setText(QStringLiteral("Query test finished: %1 queries, %2 hits, %3 ms.")
        .arg(totalQueries)
        .arg(totalHits)
        .arg(result.elapsedMs));
    return result;
}

QWidget* createLayerLoadOptionsLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* optionsBar = new QWidget(page);
    auto* optionsLayout = new QHBoxLayout(optionsBar);
    optionsLayout->setContentsMargins(6, 4, 6, 4);
    optionsLayout->setSpacing(8);

    auto* loadNoIndex = new QPushButton(QStringLiteral("Load No Index"), optionsBar);
    auto* loadRTree = new QPushButton(QStringLiteral("Load RTree"), optionsBar);
    auto* runQueryTest = new QPushButton(QStringLiteral("Run Query Test"), optionsBar);
    auto* clearLayers = new QPushButton(QStringLiteral("Clear"), optionsBar);
    optionsLayout->addWidget(loadNoIndex);
    optionsLayout->addWidget(loadRTree);
    optionsLayout->addWidget(runQueryTest);
    optionsLayout->addWidget(clearLayers);
    optionsLayout->addStretch(1);

    auto* resultBar = new QWidget(page);
    auto* resultLayout = new QVBoxLayout(resultBar);
    resultLayout->setContentsMargins(6, 2, 6, 4);
    resultLayout->setSpacing(2);
    auto* helpLabel = new QLabel(QStringLiteral("Load one index mode, then run the query test. Load time is shown separately and is not part of the test result."), resultBar);
    auto* noIndexResult = new QLabel(QStringLiteral("No Index: -"), resultBar);
    auto* rtreeResult = new QLabel(QStringLiteral("RTree: -"), resultBar);
    resultLayout->addWidget(helpLabel);
    resultLayout->addWidget(noIndexResult);
    resultLayout->addWidget(rtreeResult);

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    auto* statusBar = new QWidget(page);
    auto* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(6, 3, 6, 3);
    auto* statusLabel = new QLabel(QStringLiteral("Ready."), statusBar);
    auto* progressBar = new QProgressBar(statusBar);
    progressBar->setRange(0, 100);
    progressBar->setFixedWidth(220);
    statusLayout->addWidget(statusLabel, 1);
    statusLayout->addWidget(progressBar);

    layout->addWidget(optionsBar);
    layout->addWidget(resultBar);
    layout->addWidget(viewer, 1);
    layout->addWidget(statusBar);

    auto* currentMode = new LayerLoadIndexMode(LayerLoadIndexMode::None);
    QObject::connect(page, &QObject::destroyed, page, [currentMode]
    {
        delete currentMode;
    });

    QObject::connect(loadNoIndex, &QPushButton::clicked, viewer, [viewer, progressBar, statusLabel, currentMode]
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        if (loadLayerLoadOptionsLayer(*viewer, false, *progressBar, *statusLabel))
            *currentMode = LayerLoadIndexMode::NoIndex;
        QApplication::restoreOverrideCursor();
    });
    QObject::connect(loadRTree, &QPushButton::clicked, viewer, [viewer, progressBar, statusLabel, currentMode]
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        if (loadLayerLoadOptionsLayer(*viewer, true, *progressBar, *statusLabel))
            *currentMode = LayerLoadIndexMode::RTree;
        QApplication::restoreOverrideCursor();
    });
    QObject::connect(runQueryTest, &QPushButton::clicked, viewer, [viewer, progressBar, statusLabel, noIndexResult, rtreeResult, currentMode]
    {
        if (*currentMode == LayerLoadIndexMode::None)
        {
            statusLabel->setText(QStringLiteral("Load the shapefile first."));
            return;
        }

        QApplication::setOverrideCursor(Qt::WaitCursor);
        runLayerLoadOptionsQueryBenchmark(*viewer, *currentMode, *progressBar, *statusLabel, *noIndexResult, *rtreeResult);
        QApplication::restoreOverrideCursor();
    });
    QObject::connect(clearLayers, &QPushButton::clicked, viewer, [viewer, progressBar, statusLabel, noIndexResult, rtreeResult, currentMode]
    {
        viewer->clearLayers();
        *currentMode = LayerLoadIndexMode::None;
        progressBar->setValue(0);
        noIndexResult->setText(QStringLiteral("No Index: -"));
        rtreeResult->setText(QStringLiteral("RTree: -"));
        statusLabel->setText(QStringLiteral("Layers cleared."));
    });

    if (loadLayerLoadOptionsLayer(*viewer, true, *progressBar, *statusLabel))
        *currentMode = LayerLoadIndexMode::RTree;

    return page;
}

struct GallerySimpleStyleValues
{
    QColor fillColor = QColor(QStringLiteral("#F1D58A"));
    QColor lineColor = QColor(QStringLiteral("#266D8F"));
    double lineWidth = 2.0;
    double pointSize = 10.0;
};

struct GallerySelectionStyleValues
{
    QColor selectedLineColor = QColor(QStringLiteral("#F59E0B"));
    double selectedLineWidth = 4.0;
};

GisShapePolygon gallerySimpleStylePolygon()
{
    GisShapePolygon polygon;
    polygon.parts().append({
        GisShapePoint(-8.0, -3.0),
        GisShapePoint(1.0, -3.0),
        GisShapePoint(3.0, 4.0),
        GisShapePoint(-6.0, 6.0),
        GisShapePoint(-10.0, 2.0),
        GisShapePoint(-8.0, -3.0)
    });
    return polygon;
}

GisShapePolyline gallerySimpleStylePolyline()
{
    GisShapePolyline polyline;
    polyline.parts().append({
        GisShapePoint(-12.0, -7.0),
        GisShapePoint(-5.0, -1.0),
        GisShapePoint(1.0, -5.0),
        GisShapePoint(8.0, 2.0),
        GisShapePoint(13.0, -2.0)
    });
    return polyline;
}

GisShapePolygon gallerySelectionPolygonA()
{
    GisShapePolygon polygon;
    polygon.parts().append({
        GisShapePoint(-11.0, -4.0),
        GisShapePoint(-4.0, -4.0),
        GisShapePoint(-3.0, 2.0),
        GisShapePoint(-8.0, 5.0),
        GisShapePoint(-12.0, 1.0),
        GisShapePoint(-11.0, -4.0)
    });
    return polygon;
}

GisShapePolygon gallerySelectionPolygonB()
{
    GisShapePolygon polygon;
    polygon.parts().append({
        GisShapePoint(2.0, -4.0),
        GisShapePoint(10.0, -4.0),
        GisShapePoint(12.0, 2.0),
        GisShapePoint(6.0, 5.0),
        GisShapePoint(1.0, 1.0),
        GisShapePoint(2.0, -4.0)
    });
    return polygon;
}

GisShapePolyline gallerySelectionLine()
{
    GisShapePolyline line;
    line.parts().append({
        GisShapePoint(-12.0, -7.0),
        GisShapePoint(-6.0, -1.0),
        GisShapePoint(0.0, -5.5),
        GisShapePoint(6.0, -0.5),
        GisShapePoint(13.0, -5.0)
    });
    return line;
}

GisExtent gallerySimpleStyleExtent()
{
    return GisExtent(-19.5, -14.2, 20.5, 18.9);
}

void setGalleryColorSwatch(QPushButton& button, const QColor& color)
{
    button.setStyleSheet(QStringLiteral("QPushButton { background:%1; border:1px solid #7f8c8d; min-height:24px; }")
        .arg(color.name()));
}

QDoubleSpinBox* createGalleryStyleSpinBox(QWidget* parent, double minValue, double maxValue, double value)
{
    auto* spinBox = new QDoubleSpinBox(parent);
    spinBox->setRange(minValue, maxValue);
    spinBox->setDecimals(1);
    spinBox->setSingleStep(0.5);
    spinBox->setValue(value);
    return spinBox;
}

GisLayerVector* galleryVectorLayerByName(GisViewer& viewer, const QString& name)
{
    return dynamic_cast<GisLayerVector*>(viewer.mapLayerByName(name));
}

void applyGallerySimpleStyle(GisViewer& viewer, const GallerySimpleStyleValues& values)
{
    if (auto* layer = galleryVectorLayerByName(viewer, QStringLiteral("Styled Polygon")))
    {
        layer->style().setFillColor(values.fillColor.name());
        layer->style().setFillOpacity(185);
        layer->style().setLineColor(values.lineColor.name());
        layer->style().setLineWidth(static_cast<float>(values.lineWidth));
    }

    if (auto* layer = galleryVectorLayerByName(viewer, QStringLiteral("Styled Polyline")))
    {
        layer->style().setLineColor(values.lineColor.name());
        layer->style().setLineWidth(static_cast<float>(values.lineWidth));
    }

    if (auto* layer = galleryVectorLayerByName(viewer, QStringLiteral("Styled Points")))
    {
        layer->style().setPointColor(QStringLiteral("#D95F35"));
        layer->style().setPointSize(static_cast<float>(values.pointSize));
    }

    viewer.refreshLayers();
}

void applyGallerySelectionStyle(GisViewer& viewer, const GallerySelectionStyleValues& values)
{
    const QString selectedColor = values.selectedLineColor.name();
    const auto selectedWidth = static_cast<float>(values.selectedLineWidth);

    for (const QString& name :
        {
            QStringLiteral("Selectable Polygons"),
            QStringLiteral("Selectable Polyline"),
            QStringLiteral("Selectable Points")
        })
    {
        if (auto* layer = galleryVectorLayerByName(viewer, name))
        {
            layer->style().setSelectedLineColor(selectedColor);
            layer->style().setSelectedLineWidth(selectedWidth);
        }
    }

    viewer.refreshLayers();
}

GisLayerStyle galleryBaseStateStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#D8E5E1"));
    style.setFillOpacity(220);
    style.setLineColor(QStringLiteral("#536B68"));
    style.setLineWidth(0.9f);
    return style;
}

GisLayerStyle galleryBaseCountyStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#DCE8E4"));
    style.setFillOpacity(225);
    style.setLineColor(QStringLiteral("#536B68"));
    style.setLineWidth(0.8f);
    return style;
}

QIcon galleryLegendIcon(const GisLayerStyle& style)
{
    QPixmap pixmap(38, 22);
    pixmap.fill(Qt::transparent);

    QColor fill(style.fillColor());
    fill.setAlpha(style.fillOpacity());

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(style.lineColor()), 1.5));
    painter.setBrush(fill);
    painter.drawRect(QRectF(5, 4, 28, 14));
    return QIcon(pixmap);
}

QIcon galleryPointLegendIcon(const GisLayerStyle& style)
{
    QPixmap pixmap(48, 26);
    pixmap.fill(Qt::transparent);

    QColor fill(style.pointColor());
    if (!fill.isValid())
        fill = QColor(QStringLiteral("#D95F35"));

    QColor outline(style.lineColor());
    if (!outline.isValid())
        outline = QColor(QStringLiteral("#8A3A24"));

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(outline, 1.0));
    painter.setBrush(fill);

    const double radius = std::clamp(static_cast<double>(style.pointSize()), 4.0, 18.0) / 2.0;
    painter.drawEllipse(QPointF(24.0, 13.0), radius, radius);
    return QIcon(pixmap);
}

void updateGalleryRendererLegend(QListWidget& legend, const GisLayerVector& layer)
{
    legend.clear();
    if (layer.symbolRenderer() == nullptr)
        return;

    for (const GisSymbolLegendItem& item : layer.symbolRenderer()->legendItems())
    {
        if (!item.enabled)
            continue;

        auto* row = new QListWidgetItem(
            galleryLegendIcon(item.style),
            item.label.isEmpty() ? QStringLiteral("(empty)") : item.label);
        legend.addItem(row);
    }
}

void updateGalleryPointRendererLegend(QListWidget& legend, const GisLayerVector& layer)
{
    legend.clear();
    if (layer.symbolRenderer() == nullptr)
        return;

    for (const GisSymbolLegendItem& item : layer.symbolRenderer()->legendItems())
    {
        if (!item.enabled)
            continue;

        auto* row = new QListWidgetItem(
            galleryPointLegendIcon(item.style),
            item.label.isEmpty() ? QStringLiteral("(empty)") : item.label);
        legend.addItem(row);
    }
}

QWidget* createCategorizedRendererLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* legendList = new QListWidget(page);
    legendList->setFixedWidth(220);
    legendList->setFrameShape(QFrame::NoFrame);
    legendList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    legendList->addItem(QStringLiteral("Loading STATE categories..."));

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(legendList);
    layout->addWidget(viewer, 1);

    viewer->addOpenStreetMapLayer(true);

    QString errorMessage;
    const QString path = repositoryPath(QStringLiteral("assets/data/shapefile/usa_states_3857.shp"));
    if (!viewer->addLayerFromPath(path, &errorMessage))
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("CategorizedRenderer layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? path : errorMessage));
        return page;
    }

    auto* statesLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
    if (statesLayer == nullptr)
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("Loaded layer is not a vector layer."));
        return page;
    }

    statesLayer->setName(QStringLiteral("USA States - categorized by STATE"));
    statesLayer->style() = galleryBaseStateStyle();

    auto renderer = GisSymbolRendererFactory::createCategorizedRenderer(
        *statesLayer,
        QStringLiteral("STATE"),
        GisColorRampRegistry::ramp(QStringLiteral("Unique")),
        galleryBaseStateStyle(),
        64);

    if (!renderer)
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("Could not create categorized renderer from STATE field."));
        return page;
    }

    statesLayer->setSymbolRenderer(std::move(renderer));
    updateGalleryRendererLegend(*legendList, *statesLayer);
    viewer->setViewExtent(GisExtent(-16831516.0, 1856556.0, -4631023.0, 7472472.0));
    return page;
}

bool applyGalleryCategorizedStateRenderer(GisLayerVector& layer)
{
    auto renderer = GisSymbolRendererFactory::createCategorizedRenderer(
        layer,
        QStringLiteral("STATE"),
        GisColorRampRegistry::ramp(QStringLiteral("Unique")),
        galleryBaseStateStyle(),
        64);

    if (!renderer)
        return false;

    layer.setSymbolRenderer(std::move(renderer));
    return true;
}

QWidget* createClearRendererLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* toolbar = new QWidget(page);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(6, 4, 6, 4);
    toolbarLayout->setSpacing(6);

    auto* applyButton = new QPushButton(QStringLiteral("Apply Categorized Renderer"), toolbar);
    auto* clearButton = new QPushButton(QStringLiteral("Clear Renderer"), toolbar);
    auto* rendererLabel = new QLabel(QStringLiteral("Renderer: categorized by STATE"), toolbar);
    toolbarLayout->addWidget(applyButton);
    toolbarLayout->addWidget(clearButton);
    toolbarLayout->addWidget(rendererLabel);
    toolbarLayout->addStretch(1);

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(toolbar);
    layout->addWidget(viewer, 1);

    QString errorMessage;
    const QString path = repositoryPath(QStringLiteral("assets/data/shapefile/usa_states_3857.shp"));
    if (!viewer->addLayerFromPath(path, &errorMessage))
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("ClearRenderer layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? path : errorMessage));
        return page;
    }

    auto* statesLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
    if (statesLayer == nullptr)
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("Loaded layer is not a vector layer."));
        return page;
    }

    statesLayer->setName(QStringLiteral("USA States"));
    statesLayer->style() = galleryBaseStateStyle();
    if (!applyGalleryCategorizedStateRenderer(*statesLayer))
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("Could not create categorized renderer from STATE field."));
        return page;
    }

    QObject::connect(applyButton, &QPushButton::clicked, viewer, [viewer, statesLayer, rendererLabel]
    {
        statesLayer->style() = galleryBaseStateStyle();
        if (!applyGalleryCategorizedStateRenderer(*statesLayer))
            return;

        rendererLabel->setText(QStringLiteral("Renderer: categorized by STATE"));
        viewer->invalidateRenderCache(true, true);
        viewer->update();
    });

    QObject::connect(clearButton, &QPushButton::clicked, viewer, [viewer, statesLayer, rendererLabel]
    {
        statesLayer->clearSymbolRenderer();
        statesLayer->style() = galleryBaseStateStyle();
        rendererLabel->setText(QStringLiteral("Renderer: none, default layer style"));
        viewer->invalidateRenderCache(true, true);
        viewer->update();
    });

    viewer->setViewExtent(GisExtent(-16831516.0, 1856556.0, -4631023.0, 7472472.0));
    return page;
}

constexpr int GalleryStylePerFeatureShapeIdRole = Qt::UserRole + 1;

GisLayerStyle galleryParcelStyle(const QString& fillColor, const QString& lineColor)
{
    GisLayerStyle style;
    QColor fill(fillColor);
    fill.setAlpha(170);
    QColor line(lineColor);
    line.setAlpha(235);
    style.setFillColor(fill.name(QColor::HexArgb));
    style.setLineColor(line.name(QColor::HexArgb));
    style.setLineWidth(2.0f);
    return style;
}

GisSymbolRule galleryZoneRule(const QString& zone, const QString& fillColor, const QString& lineColor)
{
    GisSymbolRule rule;
    rule.fieldName = QStringLiteral("zone");
    rule.op = GisSymbolRuleOperator::Equals;
    rule.value = zone;
    rule.label = zone;
    rule.style = galleryParcelStyle(fillColor, lineColor);
    return rule;
}

std::unique_ptr<GisRuleBasedSymbolRenderer> createGalleryZoneRenderer()
{
    auto renderer = std::make_unique<GisRuleBasedSymbolRenderer>();
    renderer->setDefaultStyle(galleryParcelStyle(QStringLiteral("#E5E7EB"), QStringLiteral("#6B7280")));
    renderer->addRule(galleryZoneRule(QStringLiteral("Residential"), QStringLiteral("#F5DFA1"), QStringLiteral("#A16207")));
    renderer->addRule(galleryZoneRule(QStringLiteral("Commercial"), QStringLiteral("#9DD7F5"), QStringLiteral("#0369A1")));
    renderer->addRule(galleryZoneRule(QStringLiteral("Industrial"), QStringLiteral("#C4B5FD"), QStringLiteral("#6D28D9")));
    renderer->addRule(galleryZoneRule(QStringLiteral("Park"), QStringLiteral("#9AD9A8"), QStringLiteral("#15803D")));
    renderer->addRule(galleryZoneRule(QStringLiteral("Mixed"), QStringLiteral("#FDBA9A"), QStringLiteral("#C2410C")));
    return renderer;
}

std::unique_ptr<GisShapePolygon> makeGalleryParcel(
    const QString& name,
    const QString& zone,
    double xMin,
    double yMin,
    double xMax,
    double yMax)
{
    auto polygon = std::make_unique<GisShapePolygon>();
    polygon->parts().append({
        GisShapePoint(xMin, yMin),
        GisShapePoint(xMax, yMin),
        GisShapePoint(xMax, yMax),
        GisShapePoint(xMin, yMax),
        GisShapePoint(xMin, yMin)
    });
    polygon->attributes().insert(QStringLiteral("name"), name);
    polygon->attributes().insert(QStringLiteral("zone"), zone);
    return polygon;
}

GisLayerStyle galleryStyleForZone(const QString& zone)
{
    const auto renderer = createGalleryZoneRenderer();
    for (const GisSymbolLegendItem& item : renderer->legendItems())
    {
        if (item.label == zone)
            return item.style;
    }

    return renderer->defaultStyle();
}

void refreshGalleryFeatureStyleList(QListWidget& list, const GisLayerVector& layer)
{
    const int currentShapeId = list.currentItem() != nullptr
        ? list.currentItem()->data(GalleryStylePerFeatureShapeIdRole).toInt()
        : -1;

    list.clear();
    int rowToSelect = -1;
    for (GisShape* shape : layer.items())
    {
        if (shape == nullptr)
            continue;

        const QString name = shape->attributes().value(QStringLiteral("name")).toString();
        const QString zone = shape->attributes().value(QStringLiteral("zone")).toString();
        auto* item = new QListWidgetItem(
            galleryLegendIcon(galleryStyleForZone(zone)),
            QStringLiteral("%1 - %2").arg(name, zone));
        item->setData(GalleryStylePerFeatureShapeIdRole, shape->id());
        list.addItem(item);

        if (shape->id() == currentShapeId)
            rowToSelect = list.count() - 1;
    }

    if (rowToSelect >= 0)
        list.setCurrentRow(rowToSelect);
    else if (list.count() > 0)
        list.setCurrentRow(0);
}

void syncGalleryZoneCombo(QComboBox& combo, const QListWidget& list, const GisLayerVector& layer)
{
    if (list.currentItem() == nullptr)
        return;

    const int shapeId = list.currentItem()->data(GalleryStylePerFeatureShapeIdRole).toInt();
    const GisShape* shape = layer.getShape(shapeId);
    if (shape == nullptr)
        return;

    const int index = combo.findText(shape->attributes().value(QStringLiteral("zone")).toString());
    if (index >= 0)
        combo.setCurrentIndex(index);
}

QWidget* createStylePerFeatureLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* panel = new QWidget(page);
    panel->setFixedWidth(250);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(8, 8, 8, 8);

    auto* featureList = new QListWidget(panel);
    featureList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* zoneCombo = new QComboBox(panel);
    zoneCombo->addItems({
        QStringLiteral("Residential"),
        QStringLiteral("Commercial"),
        QStringLiteral("Industrial"),
        QStringLiteral("Park"),
        QStringLiteral("Mixed")
    });

    auto* applyButton = new QPushButton(QStringLiteral("Apply Feature Style"), panel);
    panelLayout->addWidget(new QLabel(QStringLiteral("Feature attributes"), panel));
    panelLayout->addWidget(featureList, 1);
    panelLayout->addWidget(new QLabel(QStringLiteral("Zone attribute"), panel));
    panelLayout->addWidget(zoneCombo);
    panelLayout->addWidget(applyButton);

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(panel);
    layout->addWidget(viewer, 1);

    auto parcels = std::make_unique<GisLayerVector>();
    parcels->setName(QStringLiteral("Parcels - style from zone attribute"));
    parcels->style() = galleryParcelStyle(QStringLiteral("#E5E7EB"), QStringLiteral("#6B7280"));
    parcels->addShape(makeGalleryParcel(QStringLiteral("Parcel A"), QStringLiteral("Residential"), 0.0, 3.0, 3.0, 5.7));
    parcels->addShape(makeGalleryParcel(QStringLiteral("Parcel B"), QStringLiteral("Commercial"), 3.4, 3.3, 6.8, 5.4));
    parcels->addShape(makeGalleryParcel(QStringLiteral("Parcel C"), QStringLiteral("Industrial"), 7.1, 3.1, 10.4, 5.7));
    parcels->addShape(makeGalleryParcel(QStringLiteral("Parcel D"), QStringLiteral("Park"), 1.0, 0.5, 4.8, 2.7));
    parcels->addShape(makeGalleryParcel(QStringLiteral("Parcel E"), QStringLiteral("Mixed"), 5.2, 0.7, 9.8, 2.8));
    parcels->open();
    parcels->setSymbolRenderer(createGalleryZoneRenderer());

    auto* parcelsLayer = parcels.get();
    viewer->addLayer(parcels);
    refreshGalleryFeatureStyleList(*featureList, *parcelsLayer);
    syncGalleryZoneCombo(*zoneCombo, *featureList, *parcelsLayer);

    QObject::connect(featureList, &QListWidget::currentItemChanged, viewer, [zoneCombo, featureList, parcelsLayer]
    {
        syncGalleryZoneCombo(*zoneCombo, *featureList, *parcelsLayer);
    });

    QObject::connect(applyButton, &QPushButton::clicked, viewer, [viewer, featureList, zoneCombo, parcelsLayer]
    {
        if (featureList->currentItem() == nullptr)
            return;

        const int shapeId = featureList->currentItem()->data(GalleryStylePerFeatureShapeIdRole).toInt();
        GisShape* shape = parcelsLayer->getShape(shapeId);
        if (shape == nullptr)
            return;

        shape->attributes().insert(QStringLiteral("zone"), zoneCombo->currentText());
        refreshGalleryFeatureStyleList(*featureList, *parcelsLayer);
        viewer->invalidateRenderCache(true, true);
        viewer->update();
    });

    viewer->setViewExtent(GisExtent(-0.8, -0.2, 11.2, 6.4));
    return page;
}

GisLayerStyle galleryBaseCityPointStyle()
{
    GisLayerStyle style;
    QColor pointFill(QStringLiteral("#D95F35"));
    pointFill.setAlpha(70);
    QColor pointOutline(QStringLiteral("#8A3A24"));
    pointOutline.setAlpha(150);
    style.setPointColor(pointFill.name(QColor::HexArgb));
    style.setLineColor(pointOutline.name(QColor::HexArgb));
    style.setPointSize(5.0f);
    style.setLineWidth(1.0f);
    return style;
}

void makeGalleryPointCategoriesReadable(GisCategorizedSymbolRenderer& renderer)
{
    QVector<GisCategorizedSymbolClass> classes = renderer.classes();
    for (GisCategorizedSymbolClass& category : classes)
    {
        QColor pointFill(category.style.pointColor());
        if (!pointFill.isValid())
            pointFill = QColor(QStringLiteral("#D95F35"));
        pointFill.setAlpha(70);

        QColor pointOutline(category.style.lineColor());
        if (!pointOutline.isValid())
            pointOutline = QColor(QStringLiteral("#8A3A24"));
        pointOutline.setAlpha(150);

        category.style.setPointColor(pointFill.name(QColor::HexArgb));
        category.style.setLineColor(pointOutline.name(QColor::HexArgb));
        category.style.setLineWidth(1.0f);
    }

    renderer.setDefaultStyle(galleryBaseCityPointStyle());
    renderer.setClasses(std::move(classes));
}

GisLayerStyle galleryRuleBasedCityStyle(const QString& fillColor, const QString& outlineColor, float pointSize)
{
    GisLayerStyle style;
    QColor fill(fillColor);
    fill.setAlpha(165);
    QColor outline(outlineColor);
    outline.setAlpha(220);

    style.setPointColor(fill.name(QColor::HexArgb));
    style.setLineColor(outline.name(QColor::HexArgb));
    style.setPointSize(pointSize);
    style.setLineWidth(1.0f);
    return style;
}

GisSymbolRule galleryPopClassRule(
    const QString& label,
    const QString& popClass,
    const QString& fillColor,
    const QString& outlineColor,
    float pointSize)
{
    GisSymbolRule rule;
    rule.fieldName = QStringLiteral("POP_CLASS");
    rule.op = GisSymbolRuleOperator::Equals;
    rule.value = popClass;
    rule.label = label;
    rule.style = galleryRuleBasedCityStyle(fillColor, outlineColor, pointSize);
    return rule;
}

std::unique_ptr<GisRuleBasedSymbolRenderer> createGalleryPopClassRuleRenderer()
{
    auto renderer = std::make_unique<GisRuleBasedSymbolRenderer>();
    renderer->setDefaultStyle(galleryRuleBasedCityStyle(QStringLiteral("#CBD5E1"), QStringLiteral("#64748B"), 4.0f));

    renderer->addRule(galleryPopClassRule(
        QStringLiteral("Less than 50,000"),
        QStringLiteral("Less than 50,000"),
        QStringLiteral("#A8ADB7"),
        QStringLiteral("#626975"),
        4.0f));
    renderer->addRule(galleryPopClassRule(
        QStringLiteral("50,000 to 100,000"),
        QStringLiteral("50,000 to 100,000"),
        QStringLiteral("#5DADE2"),
        QStringLiteral("#21618C"),
        5.5f));
    renderer->addRule(galleryPopClassRule(
        QStringLiteral("100,000 to 250,000"),
        QStringLiteral("100,000 to 250,000"),
        QStringLiteral("#58D68D"),
        QStringLiteral("#1E8449"),
        7.0f));
    renderer->addRule(galleryPopClassRule(
        QStringLiteral("250,000 to 500,000"),
        QStringLiteral("250,000 to 500,000"),
        QStringLiteral("#F5B041"),
        QStringLiteral("#935116"),
        9.0f));
    renderer->addRule(galleryPopClassRule(
        QStringLiteral("500,000 to 1,000,000"),
        QStringLiteral("500,000 to 1,000,000"),
        QStringLiteral("#EC7063"),
        QStringLiteral("#943126"),
        11.5f));
    renderer->addRule(galleryPopClassRule(
        QStringLiteral("1,000,000 to 5,000,000"),
        QStringLiteral("1,000,000 to 5,000,000"),
        QStringLiteral("#8E2C1B"),
        QStringLiteral("#4A160E"),
        15.0f));

    return renderer;
}

QWidget* createGraduatedRendererSizeLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* legendList = new QListWidget(page);
    legendList->setFixedWidth(245);
    legendList->setFrameShape(QFrame::NoFrame);
    legendList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    legendList->addItem(QStringLiteral("Loading POP_CLASS size classes..."));

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(legendList);
    layout->addWidget(viewer, 1);

    QString errorMessage;
    const QString path = repositoryPath(QStringLiteral("assets/data/shapefile/cities_4326.shp"));
    if (!viewer->addLayerFromPath(path, &errorMessage))
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("GraduatedRendererSize layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? path : errorMessage));
        return page;
    }

    auto* citiesLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
    if (citiesLayer == nullptr)
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("Loaded layer is not a vector layer."));
        return page;
    }

    citiesLayer->setName(QStringLiteral("Cities - graduated size by POP_CLASS"));
    citiesLayer->style() = galleryBaseCityPointStyle();

    auto renderer = GisSymbolRendererFactory::createCategorizedRenderer(
        *citiesLayer,
        QStringLiteral("POP_CLASS"),
        GisColorRampRegistry::ramp(QStringLiteral("Plasma")),
        galleryBaseCityPointStyle(),
        6,
        false,
        GisSymbolRendererFactory::DefaultProviderBackedSampleRowLimit,
        GisSymbolStyleTarget::SizeOrWidth,
        5.0,
        20.0);

    if (!renderer)
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("Could not create graduated size renderer from POP_CLASS field."));
        return page;
    }

    makeGalleryPointCategoriesReadable(*renderer);
    citiesLayer->setSymbolRenderer(std::move(renderer));
    updateGalleryPointRendererLegend(*legendList, *citiesLayer);

    scheduleMapFitAfterLayout(viewer, [viewer]
    {
        viewer->fullExtent();
    });

    return page;
}

QWidget* createRuleBasedRendererLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* legendList = new QListWidget(page);
    legendList->setFixedWidth(245);
    legendList->setFrameShape(QFrame::NoFrame);
    legendList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    legendList->addItem(QStringLiteral("Loading POP_CLASS rules..."));

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(legendList);
    layout->addWidget(viewer, 1);

    QString errorMessage;
    const QString path = repositoryPath(QStringLiteral("assets/data/shapefile/cities_4326.shp"));
    if (!viewer->addLayerFromPath(path, &errorMessage))
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("RuleBasedRenderer layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? path : errorMessage));
        return page;
    }

    auto* citiesLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
    if (citiesLayer == nullptr)
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("Loaded layer is not a vector layer."));
        return page;
    }

    citiesLayer->setName(QStringLiteral("Cities - rule based by POP_CLASS"));
    citiesLayer->style() = galleryRuleBasedCityStyle(QStringLiteral("#CBD5E1"), QStringLiteral("#64748B"), 4.0f);
    citiesLayer->setSymbolRenderer(createGalleryPopClassRuleRenderer());
    updateGalleryPointRendererLegend(*legendList, *citiesLayer);

    scheduleMapFitAfterLayout(viewer, [viewer]
    {
        viewer->setViewExtent(GisExtent(-180.0, -58.0, 180.0, 82.0));
    });

    return page;
}

QWidget* createGraduatedRendererLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* sidePanel = new QWidget(page);
    sidePanel->setFixedWidth(245);
    auto* sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(8, 8, 8, 8);
    sideLayout->setSpacing(8);

    auto* rampCombo = new QComboBox(sidePanel);
    rampCombo->addItems(GisColorRampRegistry::names());
    rampCombo->setCurrentIndex(rampCombo->findText(QStringLiteral("GreenBlue"), Qt::MatchFixedString));

    auto* legendList = new QListWidget(sidePanel);
    legendList->setFrameShape(QFrame::NoFrame);
    legendList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    legendList->addItem(QStringLiteral("Loading POPULATION classes..."));

    sideLayout->addWidget(new QLabel(QStringLiteral("Color ramp"), sidePanel));
    sideLayout->addWidget(rampCombo);
    sideLayout->addWidget(legendList, 1);

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(sidePanel);
    layout->addWidget(viewer, 1);

    viewer->addOpenStreetMapLayer(true);

    QString errorMessage;
    const QString path = repositoryPath(QStringLiteral("assets/data/california/california.shp"));
    if (!viewer->addLayerFromPath(path, &errorMessage))
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("GraduatedRenderer layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? path : errorMessage));
        return page;
    }

    auto* countiesLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
    if (countiesLayer == nullptr)
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("Loaded layer is not a vector layer."));
        return page;
    }

    countiesLayer->setName(QStringLiteral("California counties - graduated by POPULATION"));
    countiesLayer->style() = galleryBaseCountyStyle();

    auto applyRenderer = [viewer, rampCombo, legendList, countiesLayer]() -> bool
    {
        auto renderer = GisSymbolRendererFactory::createGraduatedRenderer(
            *countiesLayer,
            QStringLiteral("POPULATION"),
            GisClassificationMethod::NaturalBreaks,
            5,
            GisColorRampRegistry::ramp(rampCombo->currentText()),
            galleryBaseCountyStyle());

        if (!renderer)
            return false;

        countiesLayer->setSymbolRenderer(std::move(renderer));
        updateGalleryRendererLegend(*legendList, *countiesLayer);
        viewer->invalidateRenderCache(true, true);
        viewer->update();
        return true;
    };

    if (!applyRenderer())
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("Could not create graduated renderer from POPULATION field."));
        return page;
    }

    QObject::connect(rampCombo, &QComboBox::currentTextChanged, viewer, [page, applyRenderer]
    {
        if (!applyRenderer())
        {
            QMessageBox::critical(
                page,
                QStringLiteral("SamplesGallery"),
                QStringLiteral("Could not create graduated renderer from POPULATION field."));
        }
    });

    scheduleMapFitAfterLayout(viewer, [viewer]
    {
        viewer->zoomToLayer(0);
    });

    return page;
}

GisClassificationMethod galleryClassificationMethodFromText(const QString& text)
{
    if (text == QStringLiteral("Quantile"))
        return GisClassificationMethod::Quantile;
    if (text == QStringLiteral("Standard Deviation"))
        return GisClassificationMethod::StandardDeviation;

    return GisClassificationMethod::EqualInterval;
}

QWidget* createClassificationMethodsLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* sidePanel = new QWidget(page);
    sidePanel->setFixedWidth(245);
    auto* sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(8, 8, 8, 8);
    sideLayout->setSpacing(8);

    auto* methodCombo = new QComboBox(sidePanel);
    methodCombo->addItems({
        QStringLiteral("Equal Interval"),
        QStringLiteral("Quantile"),
        QStringLiteral("Standard Deviation")
    });

    auto* legendList = new QListWidget(sidePanel);
    legendList->setFrameShape(QFrame::NoFrame);
    legendList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    legendList->addItem(QStringLiteral("Loading POPULATION classes..."));

    sideLayout->addWidget(new QLabel(QStringLiteral("Classification method"), sidePanel));
    sideLayout->addWidget(methodCombo);
    sideLayout->addWidget(legendList, 1);

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(sidePanel);
    layout->addWidget(viewer, 1);

    QString errorMessage;
    const QString path = repositoryPath(QStringLiteral("assets/data/california/california.shp"));
    if (!viewer->addLayerFromPath(path, &errorMessage))
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("ClassificationMethods layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? path : errorMessage));
        return page;
    }

    auto* countiesLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
    if (countiesLayer == nullptr)
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("Loaded layer is not a vector layer."));
        return page;
    }

    countiesLayer->setName(QStringLiteral("California counties - classification methods"));
    countiesLayer->style() = galleryBaseCountyStyle();

    auto applyRenderer = [viewer, methodCombo, legendList, countiesLayer]() -> bool
    {
        auto renderer = GisSymbolRendererFactory::createGraduatedRenderer(
            *countiesLayer,
            QStringLiteral("POPULATION"),
            galleryClassificationMethodFromText(methodCombo->currentText()),
            5,
            GisColorRampRegistry::ramp(QStringLiteral("GreenBlue")),
            galleryBaseCountyStyle(),
            1.0);

        if (!renderer)
            return false;

        countiesLayer->setSymbolRenderer(std::move(renderer));
        updateGalleryRendererLegend(*legendList, *countiesLayer);
        viewer->invalidateRenderCache(true, true);
        viewer->update();
        return true;
    };

    if (!applyRenderer())
    {
        QMessageBox::critical(
            page,
            QStringLiteral("SamplesGallery"),
            QStringLiteral("Could not create graduated renderer from POPULATION field."));
        return page;
    }

    QObject::connect(methodCombo, &QComboBox::currentTextChanged, viewer, [page, applyRenderer]
    {
        if (!applyRenderer())
        {
            QMessageBox::critical(
                page,
                QStringLiteral("SamplesGallery"),
                QStringLiteral("Could not create graduated renderer from POPULATION field."));
        }
    });

    scheduleMapFitAfterLayout(viewer, [viewer]
    {
        viewer->zoomToLayer(0);
    });

    return page;
}

QWidget* createSimpleStyleLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* sidePanel = new QWidget(page);
    sidePanel->setFixedWidth(230);
    auto* sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(10, 10, 10, 10);
    sideLayout->setSpacing(8);

    auto* fillColorButton = new QPushButton(sidePanel);
    auto* lineColorButton = new QPushButton(sidePanel);
    auto* lineWidthSpin = createGalleryStyleSpinBox(sidePanel, 0.5, 12.0, 2.0);
    auto* pointSizeSpin = createGalleryStyleSpinBox(sidePanel, 2.0, 32.0, 10.0);
    auto* resetButton = new QPushButton(QStringLiteral("Reset Style"), sidePanel);

    sideLayout->addWidget(new QLabel(QStringLiteral("Fill Color"), sidePanel));
    sideLayout->addWidget(fillColorButton);
    sideLayout->addWidget(new QLabel(QStringLiteral("Line Color"), sidePanel));
    sideLayout->addWidget(lineColorButton);
    sideLayout->addWidget(new QLabel(QStringLiteral("Line Width"), sidePanel));
    sideLayout->addWidget(lineWidthSpin);
    sideLayout->addWidget(new QLabel(QStringLiteral("Point Size"), sidePanel));
    sideLayout->addWidget(pointSizeSpin);
    sideLayout->addWidget(resetButton);
    sideLayout->addStretch(1);

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(sidePanel);
    layout->addWidget(viewer, 1);

    viewer->addPolygonLayer(QStringLiteral("Styled Polygon"), { gallerySimpleStylePolygon().parts().first() });
    viewer->addPolylineLayer(QStringLiteral("Styled Polyline"), { gallerySimpleStylePolyline().parts().first() });
    viewer->addPointLayer(
        QStringLiteral("Styled Points"),
        {
            GisShapePoint(-6.0, 9.0),
            GisShapePoint(0.0, 8.0),
            GisShapePoint(7.0, 7.0)
        });

    auto* values = new GallerySimpleStyleValues();
    setGalleryColorSwatch(*fillColorButton, values->fillColor);
    setGalleryColorSwatch(*lineColorButton, values->lineColor);
    applyGallerySimpleStyle(*viewer, *values);

    QObject::connect(fillColorButton, &QPushButton::clicked, viewer, [page, viewer, fillColorButton, values]
    {
        const QColor color = QColorDialog::getColor(values->fillColor, page, QStringLiteral("Fill Color"));
        if (!color.isValid())
            return;

        values->fillColor = color;
        setGalleryColorSwatch(*fillColorButton, values->fillColor);
        applyGallerySimpleStyle(*viewer, *values);
    });

    QObject::connect(lineColorButton, &QPushButton::clicked, viewer, [page, viewer, lineColorButton, values]
    {
        const QColor color = QColorDialog::getColor(values->lineColor, page, QStringLiteral("Line Color"));
        if (!color.isValid())
            return;

        values->lineColor = color;
        setGalleryColorSwatch(*lineColorButton, values->lineColor);
        applyGallerySimpleStyle(*viewer, *values);
    });

    QObject::connect(lineWidthSpin, &QDoubleSpinBox::valueChanged, viewer, [viewer, values](double value)
    {
        values->lineWidth = value;
        applyGallerySimpleStyle(*viewer, *values);
    });

    QObject::connect(pointSizeSpin, &QDoubleSpinBox::valueChanged, viewer, [viewer, values](double value)
    {
        values->pointSize = value;
        applyGallerySimpleStyle(*viewer, *values);
    });

    QObject::connect(resetButton, &QPushButton::clicked, viewer, [viewer, fillColorButton, lineColorButton, lineWidthSpin, pointSizeSpin, values]
    {
        *values = GallerySimpleStyleValues {};
        lineWidthSpin->setValue(values->lineWidth);
        pointSizeSpin->setValue(values->pointSize);
        setGalleryColorSwatch(*fillColorButton, values->fillColor);
        setGalleryColorSwatch(*lineColorButton, values->lineColor);
        applyGallerySimpleStyle(*viewer, *values);
    });

    QObject::connect(page, &QObject::destroyed, page, [values] { delete values; });
    viewer->setViewExtent(gallerySimpleStyleExtent());
    return page;
}

QWidget* createSelectionStyleLivePage()
{
    auto* page = new QWidget();
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* sidePanel = new QWidget(page);
    sidePanel->setFixedWidth(230);
    auto* sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(10, 10, 10, 10);
    sideLayout->setSpacing(8);

    auto* selectedColorButton = new QPushButton(sidePanel);
    auto* selectedWidthSpin = createGalleryStyleSpinBox(sidePanel, 1.0, 16.0, 4.0);
    auto* clearSelectionButton = new QPushButton(QStringLiteral("Clear Selection"), sidePanel);
    auto* resetButton = new QPushButton(QStringLiteral("Reset Style"), sidePanel);
    auto* status = new QLabel(QStringLiteral("Tool: Select | Selected: 0"), sidePanel);
    status->setWordWrap(true);

    sideLayout->addWidget(new QLabel(QStringLiteral("Selected Line Color"), sidePanel));
    sideLayout->addWidget(selectedColorButton);
    sideLayout->addWidget(new QLabel(QStringLiteral("Selected Line Width"), sidePanel));
    sideLayout->addWidget(selectedWidthSpin);
    sideLayout->addWidget(clearSelectionButton);
    sideLayout->addWidget(resetButton);
    sideLayout->addWidget(status);
    sideLayout->addStretch(1);

    auto* viewer = new GisViewer(page);    
    viewer->setActiveTool(GisViewerTool::Select);

    layout->addWidget(sidePanel);
    layout->addWidget(viewer, 1);

    GisLayerStyle polygonStyle;
    polygonStyle.setFillColor(QStringLiteral("#F1D58A"));
    polygonStyle.setFillOpacity(180);
    polygonStyle.setLineColor(QStringLiteral("#266D8F"));
    polygonStyle.setLineWidth(1.8f);
    viewer->addPolygonLayer(
        QStringLiteral("Selectable Polygons"),
        { gallerySelectionPolygonA().parts().first(), gallerySelectionPolygonB().parts().first() },
        polygonStyle);

    GisLayerStyle lineStyle;
    lineStyle.setLineColor(QStringLiteral("#266D8F"));
    lineStyle.setLineWidth(2.2f);
    viewer->addPolylineLayer(QStringLiteral("Selectable Polyline"), { gallerySelectionLine().parts().first() }, lineStyle);

    GisLayerStyle pointStyle;
    pointStyle.setPointColor(QStringLiteral("#D95F35"));
    pointStyle.setPointSize(10.0f);
    viewer->addPointLayer(
        QStringLiteral("Selectable Points"),
        {
            GisShapePoint(-8.0, 8.0),
            GisShapePoint(0.0, 7.0),
            GisShapePoint(8.0, 8.0)
        },
        pointStyle);

    auto* values = new GallerySelectionStyleValues();
    setGalleryColorSwatch(*selectedColorButton, values->selectedLineColor);
    applyGallerySelectionStyle(*viewer, *values);

    QObject::connect(viewer, &GisViewer::selectionChanged, status, [status](int count)
    {
        status->setText(QStringLiteral("Tool: Select | Selected: %1").arg(count));
    });

    QObject::connect(selectedColorButton, &QPushButton::clicked, viewer, [page, viewer, selectedColorButton, values]
    {
        const QColor color = QColorDialog::getColor(values->selectedLineColor, page, QStringLiteral("Selected Line Color"));
        if (!color.isValid())
            return;

        values->selectedLineColor = color;
        setGalleryColorSwatch(*selectedColorButton, values->selectedLineColor);
        applyGallerySelectionStyle(*viewer, *values);
    });

    QObject::connect(selectedWidthSpin, &QDoubleSpinBox::valueChanged, viewer, [viewer, values](double value)
    {
        values->selectedLineWidth = value;
        applyGallerySelectionStyle(*viewer, *values);
    });

    QObject::connect(clearSelectionButton, &QPushButton::clicked, viewer, &GisViewer::clearSelectedFeatures);
    QObject::connect(resetButton, &QPushButton::clicked, viewer, [viewer, selectedColorButton, selectedWidthSpin, values]
    {
        *values = GallerySelectionStyleValues {};
        selectedWidthSpin->setValue(values->selectedLineWidth);
        setGalleryColorSwatch(*selectedColorButton, values->selectedLineColor);
        applyGallerySelectionStyle(*viewer, *values);
    });

    QObject::connect(page, &QObject::destroyed, page, [values] { delete values; });
    viewer->setViewExtent(GisExtent(-15.0, -9.0, 15.0, 11.0));
    return page;
}

bool sampleKeepsGalleryBusyUntilReady(SampleId sample)
{
    return sample == SampleId::Project;
}

GisShapePolygon gallerySimpleStylePolygon();
GisShapePolyline gallerySimpleStylePolyline();
GisExtent gallerySimpleStyleExtent();

QString standardTripleSourceFolder(SampleId sample)
{
    switch (sample)
    {
    case SampleId::Minimap:
        return QStringLiteral("Minimap");
    case SampleId::AddPolylineInteractive:
        return QStringLiteral("AddPolylineInteractive");
    case SampleId::AddPolygonInteractive:
        return QStringLiteral("AddPolygonInteractive");
    case SampleId::AddPointProgrammatic:
        return QStringLiteral("AddPointProgrammatic");
    case SampleId::AddPolylineProgrammatic:
        return QStringLiteral("AddPolylineProgrammatic");
    case SampleId::AddPolygonProgrammatic:
        return QStringLiteral("AddPolygonProgrammatic");
    case SampleId::DeleteFeature:
        return QStringLiteral("DeleteFeature");
    case SampleId::MoveFeatureTool:
        return QStringLiteral("MoveFeatureTool");
    default:
        return sampleTitle(sample);
    }
}

bool isFileBackedGallerySample(SampleId sample)
{
    switch (sample)
    {
    case SampleId::LabelCollisionOff:
    case SampleId::ShapefileLoad:
    case SampleId::ShapefileSaveAs:
    case SampleId::TabLoad:
    case SampleId::MifLoad:
    case SampleId::GeoPackageLoad:
    case SampleId::KmlLoad:
    case SampleId::DxfLoad:
    case SampleId::GeoTiffLoad:
    case SampleId::RasterWorldTransform:
    case SampleId::RasterOverview:
    case SampleId::RasterTileCache:
        return true;
    default:
        return false;
    }
}

bool isXyzGallerySample(SampleId sample)
{
    switch (sample)
    {
    case SampleId::OpenStreetMap:
    case SampleId::XyzPresets:
    case SampleId::XyzCustomUrl:
    case SampleId::XyzLocalCache:
    case SampleId::XyzTileSize:
    case SampleId::XyzMinMaxZoom:
    case SampleId::XyzAttribution:
    case SampleId::XyzDiagnostics:
        return true;
    default:
        return false;
    }
}

bool isSerializationGallerySample(SampleId sample)
{
    return sample == SampleId::WktOverlay || sample == SampleId::WktRoundtrip;
}

QString gallerySampleDataPath(SampleId sample)
{
    switch (sample)
    {
    case SampleId::TabLoad:
        return QStringLiteral("assets/data/paris.tab");
    case SampleId::MifLoad:
        return QStringLiteral("assets/data/albania.mif");
    case SampleId::GeoPackageLoad:
        return QStringLiteral("assets/data/europe_detailed.gpkg");
    case SampleId::KmlLoad:
        return QStringLiteral("assets/data/travel.kmz");
    case SampleId::DxfLoad:
        return QStringLiteral("assets/data/geog_25000.dxf");
    case SampleId::GeoTiffLoad:
    case SampleId::RasterWorldTransform:
        return QStringLiteral("assets/data/tiff/usa_3857.tif");
    case SampleId::RasterOverview:
    case SampleId::RasterTileCache:
        return QStringLiteral("assets/data/world_8km.tif");
    case SampleId::LabelCollisionOff:
        return QStringLiteral("assets/data/kml/usa_cities_4326.kml");
    case SampleId::ShapefileLoad:
    case SampleId::ShapefileSaveAs:
    default:
        return QStringLiteral("assets/data/shapefile/world_4326.shp");
    }
}

QWidget* createGenericLayerLoadLivePage(SampleId sample)
{
    auto* page = new QWidget();
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* viewerContainer = new QWidget(page);
    auto* viewerLayout = new QVBoxLayout(viewerContainer);
    viewerLayout->setContentsMargins(0, 0, 0, 0);
    viewerLayout->setSpacing(0);

    auto* viewer = new GisViewer(viewerContainer);
    viewer->setActiveTool(GisViewerTool::Pan);
    viewerLayout->addWidget(createMapToolbar(*viewer, viewerContainer));
    viewerLayout->addWidget(viewer, 1);

    auto* info = new QPlainTextEdit(page);
    info->setReadOnly(true);
    info->setFixedWidth(360);
    info->setLineWrapMode(QPlainTextEdit::WidgetWidth);

    layout->addWidget(viewerContainer, 1);
    layout->addWidget(info);

    const QString relativePath = gallerySampleDataPath(sample);
    const QString path = repositoryPath(relativePath);
    QString errorMessage;
    const bool loaded = viewer->addLayerFromPath(path, &errorMessage);
    QStringList lines;
    lines << sampleTitle(sample);
    lines << QString();
    lines << QStringLiteral("Data:");
    lines << path;
    lines << QString();

    if (loaded)
    {
        lines << QStringLiteral("Layer loaded.");
        lines << QStringLiteral("Layer count: %1").arg(viewer->layerCount());
        scheduleMapFitAfterLayout(viewer, [viewer] { viewer->fullExtent(); });
    }
    else
    {
        lines << QStringLiteral("Layer could not be loaded.");
        lines << (errorMessage.isEmpty() ? path : errorMessage);
    }

    if (sample == SampleId::LabelCollisionOff && loaded)
    {
        lines << QString();
        lines << QStringLiteral("This gallery preview loads the dense label source. Open the standalone sample for the split collision comparison.");
    }
    else if ((sample == SampleId::RasterOverview || sample == SampleId::RasterTileCache) && loaded)
    {
        lines << QString();
        lines << QStringLiteral("This preview loads the raster. The standalone sample contains the benchmark controls.");
    }

    info->setPlainText(lines.join(QStringLiteral("\n")));
    return page;
}

QWidget* createGenericXyzLivePage(SampleId sample)
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    auto* status = new QLabel(page);
    status->setStyleSheet(QStringLiteral("padding: 4px 8px;"));

    layout->addWidget(createMapToolbar(*viewer, page));
    layout->addWidget(viewer, 1);
    layout->addWidget(status);

    if (viewer->addOpenStreetMapLayer(true) >= 0)
    {
        status->setText(QStringLiteral("%1 tile preview loaded. Open the standalone sample for its specific XYZ controls.").arg(sampleTitle(sample)));
        scheduleMapFitAfterLayout(viewer, [viewer]
        {
            viewer->setViewExtent(GisExtent(-1400000.0, 4100000.0, 4200000.0, 7800000.0));
        });
    }
    else
    {
        status->setText(QStringLiteral("Tile layer could not be added."));
    }

    return page;
}

QWidget* createGenericSerializationLivePage(SampleId sample)
{
    auto* page = new QWidget();
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* viewer = new GisViewer(page);
    viewer->setActiveTool(GisViewerTool::Pan);

    auto* info = new QPlainTextEdit(page);
    info->setReadOnly(true);
    info->setFixedWidth(360);

    auto polygonLayer = std::make_unique<GisLayerVector>();
    polygonLayer->setName(QStringLiteral("Serialization Polygon"));
    polygonLayer->style().setFillColor(QStringLiteral("#F1D58A"));
    polygonLayer->style().setFillOpacity(170);
    polygonLayer->style().setLineColor(QStringLiteral("#266D8F"));
    polygonLayer->style().setLineWidth(2.0f);
    polygonLayer->addPolygon(gallerySimpleStylePolygon());
    polygonLayer->open();
    viewer->addLayer(polygonLayer);

    auto lineLayer = std::make_unique<GisLayerVector>();
    lineLayer->setName(QStringLiteral("Serialization Line"));
    lineLayer->style().setLineColor(QStringLiteral("#D95F35"));
    lineLayer->style().setLineWidth(2.0f);
    lineLayer->addPolyline(gallerySimpleStylePolyline());
    lineLayer->open();
    viewer->addLayer(lineLayer);

    info->setPlainText(QStringLiteral(
        "%1\n\nThis gallery preview renders the geometry used by the serialization samples.\n"
        "Open the Source tabs to inspect the exact WKT roundtrip/overlay code.").arg(sampleTitle(sample)));

    layout->addWidget(viewer, 1);
    layout->addWidget(info);
    viewer->setViewExtent(gallerySimpleStyleExtent());
    return page;
}

QWidget* createLivePage(SampleId sample, std::function<void()> ready = {})
{
    if (isFileBackedGallerySample(sample))
        return createGenericLayerLoadLivePage(sample);

    if (isXyzGallerySample(sample))
        return createGenericXyzLivePage(sample);

    if (isSerializationGallerySample(sample))
        return createGenericSerializationLivePage(sample);

    switch (sample)
    {
    case SampleId::AddLayers:
        return createAddLayersLivePage();
    case SampleId::Project:
        return createProjectLivePage(std::move(ready));
    case SampleId::Scalebar:
        return createScalebarLivePage();
    case SampleId::Minimap:
        return createMinimapLivePage();
    case SampleId::AddPointInteractive:
        return createAddPointInteractiveLivePage();
    case SampleId::AddPolylineInteractive:
        return createAddPolylineInteractiveLivePage();
    case SampleId::AddPolygonInteractive:
        return createAddPolygonInteractiveLivePage();
    case SampleId::AddPointProgrammatic:
        return createAddPointProgrammaticLivePage();
    case SampleId::AddPolylineProgrammatic:
        return createAddPolylineProgrammaticLivePage();
    case SampleId::AddPolygonProgrammatic:
        return createAddPolygonProgrammaticLivePage();
    case SampleId::DeleteFeature:
        return createDeleteFeatureLivePage();
    case SampleId::MoveFeatureTool:
        return createMoveFeatureToolLivePage();
    case SampleId::EditSession:
        return createEditSessionLivePage();
    case SampleId::LayerAddRemove:
        return createLayerAddRemoveLivePage();
    case SampleId::LayerReorder:
        return createLayerReorderLivePage();
    case SampleId::LayerExtent:
        return createLayerExtentLivePage();
    case SampleId::LayerEvents:
        return createLayerEventsLivePage();
    case SampleId::InMemoryLayers:
        return createInMemoryLayersLivePage();
    case SampleId::LayerRefresh:
        return createLayerRefreshLivePage();
    case SampleId::LayerLoadCancel:
        return createLayerLoadCancelLivePage();
    case SampleId::LayerLoadOptions:
        return createLayerLoadOptionsLivePage();
    case SampleId::LayerZoomTo:
        return createLayerZoomToLivePage();
    case SampleId::LayerVisibility:
        return createLayerVisibilityLivePage();
    case SampleId::CategorizedRenderer:
        return createCategorizedRendererLivePage();
    case SampleId::ClearRenderer:
        return createClearRendererLivePage();
    case SampleId::GraduatedRenderer:
        return createGraduatedRendererLivePage();
    case SampleId::GraduatedRendererSize:
        return createGraduatedRendererSizeLivePage();
    case SampleId::ClassificationMethods:
        return createClassificationMethodsLivePage();
    case SampleId::RuleBasedRenderer:
        return createRuleBasedRendererLivePage();
    case SampleId::SimpleStyle:
        return createSimpleStyleLivePage();
    case SampleId::SelectionStyle:
        return createSelectionStyleLivePage();
    case SampleId::StylePerFeature:
        return createStylePerFeatureLivePage();
    case SampleId::HelloMap:
    default:
        return createHelloMapLivePage();
    }
}

QWidget* createDescriptionPage(SampleId sample)
{
    auto* label = new QLabel();
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    label->setMargin(16);

    if (sample == SampleId::AddLayers)
    {
        label->setText(QStringLiteral(
            "<h2>AddLayers</h2>"
            "<p>Loads multiple layer types in one map: OSM, GeoTIFF, SHP and KML.</p>"
            "<p><b>APIs:</b> addOpenStreetMapLayer, addLayerFromPath, mapLayerAt, zoomToLayer.</p>"));
    }
    else if (sample == SampleId::Project)
    {
        label->setText(QStringLiteral(
            "<h2>Project</h2>"
            "<p>Loads a GeoKernel project file and displays progress while project layers are opened.</p>"
            "<p><b>APIs:</b> openProject, progress callback, full map navigation tools.</p>"));
    }
    else if (sample == SampleId::LayerVisibility)
    {
        label->setText(QStringLiteral(
            "<h2>LayerVisibility</h2>"
            "<p>Shows loaded layers in a list and toggles the selected layer on or off.</p>"
            "<p><b>APIs:</b> mapLayerAt, setLayerVisible, layersChanged, layerDisplayText.</p>"));
    }
    else if (sample == SampleId::Scalebar)
    {
        label->setText(QStringLiteral(
            "<h2>Scalebar</h2>"
            "<p>Displays a map with a bottom-right scale bar control.</p>"
            "<p><b>APIs:</b> GisScaleBar, setViewer, setAnchor.</p>"));
    }
    else if (sample == SampleId::Minimap)
    {
        label->setText(QStringLiteral(
            "<h2>MiniMap</h2>"
            "<p>Displays a top-right overview map synchronized with the main view extent.</p>"
            "<p><b>APIs:</b> GisMiniMap, setViewer, setAnchor.</p>"));
    }
    else if (sample == SampleId::AddPointInteractive)
    {
        label->setText(QStringLiteral(
            "<h2>AddPointInteractive</h2>"
            "<p>Starts an editable point layer and uses the Add Point tool to add features by clicking the map.</p>"
            "<p><b>APIs:</b> GisViewerTool::AddPoint, beginEditLayer, setActiveEditLayerIndex, layerEditStateChanged.</p>"));
    }
    else if (sample == SampleId::AddPolylineInteractive)
    {
        label->setText(QStringLiteral(
            "<h2>AddPolylineInteractive</h2>"
            "<p>Starts an editable polyline layer and uses the Add Polyline tool to digitize line features on the map.</p>"
            "<p><b>APIs:</b> GisViewerTool::AddPolyline, beginEditLayer, setActiveEditLayerIndex, layerEditStateChanged.</p>"));
    }
    else if (sample == SampleId::AddPolygonInteractive)
    {
        label->setText(QStringLiteral(
            "<h2>AddPolygonInteractive</h2>"
            "<p>Starts an editable polygon layer and uses the Add Polygon tool to digitize polygon features on the map.</p>"
            "<p><b>APIs:</b> GisViewerTool::AddPolygon, beginEditLayer, setActiveEditLayerIndex, layerEditStateChanged.</p>"));
    }
    else if (sample == SampleId::AddPointProgrammatic)
    {
        label->setText(QStringLiteral(
            "<h2>AddPointProgrammatic</h2>"
            "<p>Adds point features to an edit layer directly from code.</p>"
            "<p><b>APIs:</b> addPointToEditLayer, beginEditLayer, setActiveEditLayerIndex, refreshLayers.</p>"));
    }
    else if (sample == SampleId::AddPolylineProgrammatic)
    {
        label->setText(QStringLiteral(
            "<h2>AddPolylineProgrammatic</h2>"
            "<p>Adds polyline features to an edit layer directly from code.</p>"
            "<p><b>APIs:</b> addPolylineToEditLayer, beginEditLayer, setActiveEditLayerIndex, refreshLayers.</p>"));
    }
    else if (sample == SampleId::AddPolygonProgrammatic)
    {
        label->setText(QStringLiteral(
            "<h2>AddPolygonProgrammatic</h2>"
            "<p>Adds polygon features to an edit layer directly from code.</p>"
            "<p><b>APIs:</b> addPolygonToEditLayer, beginEditLayer, setActiveEditLayerIndex, refreshLayers.</p>"));
    }
    else if (sample == SampleId::DeleteFeature)
    {
        label->setText(QStringLiteral(
            "<h2>DeleteFeature</h2>"
            "<p>Selects editable point features and deletes either one feature by id or all selected features.</p>"
            "<p><b>APIs:</b> deleteShapeFromEditLayer, deleteSelectedFeaturesFromEditLayer, selectedFeatures, selectionChanged.</p>"));
    }
    else if (sample == SampleId::MoveFeatureTool)
    {
        label->setText(QStringLiteral(
            "<h2>MoveFeatureTool</h2>"
            "<p>Selects editable point features and moves the selected feature by dragging it on the map.</p>"
            "<p><b>APIs:</b> GisViewerTool::MoveFeature, beginEditLayer, setActiveEditLayerIndex, selectedFeatures, layerEditStateChanged.</p>"));
    }
    else if (sample == SampleId::EditSession)
    {
        label->setText(QStringLiteral(
            "<h2>EditSession</h2>"
            "<p>Shows the basic edit transaction flow: start editing, add an in-memory feature, commit changes or roll them back.</p>"
            "<p><b>APIs:</b> beginEditLayer, addShapeEdit, commitEditLayer, rollbackEditLayer, isEditing.</p>"));
    }
    else if (sample == SampleId::LayerAddRemove)
    {
        label->setText(QStringLiteral(
            "<h2>LayerAddRemove</h2>"
            "<p>Adds, removes and clears named map layers without changing the current map extent.</p>"
            "<p><b>APIs:</b> addLayerFromPath, removeLayerByName, clearLayers.</p>"));
    }
    else if (sample == SampleId::LayerReorder)
    {
        label->setText(QStringLiteral(
            "<h2>LayerReorder</h2>"
            "<p>Moves the selected layer up or down in the draw order.</p>"
            "<p><b>APIs:</b> mapLayerAt, moveLayer, layerDisplayText.</p>"));
    }
    else if (sample == SampleId::LayerExtent)
    {
        label->setText(QStringLiteral(
            "<h2>LayerExtent</h2>"
            "<p>Loads the California layer and draws a rectangle around the layer extent.</p>"
            "<p><b>APIs:</b> layer extent, addPolygonLayer, fullExtent.</p>"));
    }
    else if (sample == SampleId::LayerEvents)
    {
        label->setText(QStringLiteral(
            "<h2>LayerEvents</h2>"
            "<p>Logs layer lifecycle, visibility, ordering and edit-session events while layers are added, removed and reordered.</p>"
            "<p><b>APIs:</b> layersChanged, layerAdded, layerRemoved, layerVisibilityChanged, layerOrderChanged, layerEditSessionStarted/Committed/RolledBack.</p>"));
    }
    else if (sample == SampleId::InMemoryLayers)
    {
        label->setText(QStringLiteral(
            "<h2>InMemoryLayers</h2>"
            "<p>Creates point, line and polygon layers entirely in memory, then adds new shapes without writing a file.</p>"
            "<p><b>APIs:</b> addPointLayer, addPolylineLayer, addPolygonLayer, beginEditLayer, commitEditLayer.</p>"));
    }
    else if (sample == SampleId::LayerRefresh)
    {
        label->setText(QStringLiteral(
            "<h2>LayerRefresh</h2>"
            "<p>Changes layer style values first, then redraws the map only when Refresh Layer is pressed.</p>"
            "<p><b>APIs:</b> layer style properties, setLayerStyle, refreshLayers, invalidateRenderCache.</p>"));
    }
    else if (sample == SampleId::LayerLoadCancel)
    {
        label->setText(QStringLiteral(
            "<h2>LayerLoadCancel</h2>"
            "<p>Loads a large point shapefile with a cancellable RTree index preparation callback.</p>"
            "<p><b>APIs:</b> LayerLoadOptions, isCancellationRequested, progress callback, spatial index state callback.</p>"));
    }
    else if (sample == SampleId::LayerLoadOptions)
    {
        label->setText(QStringLiteral(
            "<h2>LayerLoadOptions</h2>"
            "<p>Loads the same shapefile with and without an RTree spatial index, then runs repeated extent queries to compare query behavior.</p>"
            "<p><b>APIs:</b> LayerLoadOptions, spatialIndexType, addLayerFromPath, hitTestFeaturesInExtent.</p>"));
    }
    else if (sample == SampleId::LayerZoomTo)
    {
        label->setText(QStringLiteral(
            "<h2>LayerZoomTo</h2>"
            "<p>Loads California city layers, fills a combo box with layer names, and zooms to the selected layer.</p>"
            "<p><b>APIs:</b> addLayerFromPath, zoomToLayer, fullExtent, label style properties.</p>"));
    }
    else if (sample == SampleId::CategorizedRenderer)
    {
        label->setText(QStringLiteral(
            "<h2>CategorizedRenderer</h2>"
            "<p>Styles USA states by unique STATE values and displays a category legend.</p>"
            "<p><b>APIs:</b> ApplyLayerCategorizedRenderer, GisSymbolRendererFactory::createCategorizedRenderer, color ramps, renderer legend items.</p>"));
    }
    else if (sample == SampleId::ClearRenderer)
    {
        label->setText(QStringLiteral(
            "<h2>ClearRenderer</h2>"
            "<p>Applies a categorized renderer to USA states, then clears it to return to the layer's default style.</p>"
            "<p><b>APIs:</b> ClearLayerSymbolRenderer, clearSymbolRenderer, ApplyLayerCategorizedRenderer, layer default style.</p>"));
    }
    else if (sample == SampleId::GraduatedRenderer)
    {
        label->setText(QStringLiteral(
            "<h2>GraduatedRenderer</h2>"
            "<p>Styles California counties by POPULATION using Natural Breaks classes and a selectable color ramp.</p>"
            "<p><b>APIs:</b> ApplyLayerGraduatedRenderer, GisSymbolRendererFactory::createGraduatedRenderer, classification methods, color ramps, renderer legend items.</p>"));
    }
    else if (sample == SampleId::GraduatedRendererSize)
    {
        label->setText(QStringLiteral(
            "<h2>GraduatedRendererSize</h2>"
            "<p>Styles city points by POP_CLASS using progressively larger point symbols.</p>"
            "<p><b>APIs:</b> SetLayerCategorizedRenderer, GisSymbolRendererFactory::createCategorizedRenderer, GisSymbolStyleTarget::SizeOrWidth, renderer legend items.</p>"));
    }
    else if (sample == SampleId::ClassificationMethods)
    {
        label->setText(QStringLiteral(
            "<h2>ClassificationMethods</h2>"
            "<p>Compares Equal Interval, Quantile and Standard Deviation classification against the POPULATION field.</p>"
            "<p><b>APIs:</b> ApplyLayerGraduatedRenderer, GisSymbolRendererFactory::createGraduatedRenderer, GisClassificationMethod, renderer legend items.</p>"));
    }
    else if (sample == SampleId::RuleBasedRenderer)
    {
        label->setText(QStringLiteral(
            "<h2>RuleBasedRenderer</h2>"
            "<p>Styles city points with explicit rules against the POP_CLASS attribute.</p>"
            "<p><b>APIs:</b> SetLayerRuleBasedRenderer, GisRuleBasedSymbolRenderer, GisSymbolRule, renderer legend items.</p>"));
    }
    else if (sample == SampleId::SimpleStyle)
    {
        label->setText(QStringLiteral(
            "<h2>SimpleStyle</h2>"
            "<p>Changes fill color, line color, line width and point size for in-memory vector layers.</p>"
            "<p><b>APIs:</b> layer style properties, refreshLayers, addPointLayer, addPolylineLayer, addPolygonLayer.</p>"));
    }
    else if (sample == SampleId::SelectionStyle)
    {
        label->setText(QStringLiteral(
            "<h2>SelectionStyle</h2>"
            "<p>Uses the Select tool and changes the selected feature outline color and width.</p>"
            "<p><b>APIs:</b> GisViewerTool::Select, selectedLineColor, selectedLineWidth, selectionChanged, clearSelectedFeatures.</p>"));
    }
    else if (sample == SampleId::StylePerFeature)
    {
        label->setText(QStringLiteral(
            "<h2>StylePerFeature</h2>"
            "<p>Updates per-feature attributes and lets a rule-based renderer choose the style dynamically from each feature's zone value.</p>"
            "<p><b>APIs:</b> shape attributes, SetShapeAttributesInEditLayer, SetLayerRuleBasedRenderer, rule-based symbol styles.</p>"));
    }
    else if (isFileBackedGallerySample(sample))
    {
        label->setText(QStringLiteral(
            "<h2>%1</h2>"
            "<p>Loads the sample data file and shows the resulting map layer in the gallery.</p>"
            "<p><b>Data:</b> %2</p>"
            "<p><b>APIs:</b> addLayerFromPath, map layer styling, fullExtent.</p>")
            .arg(sampleTitle(sample), gallerySampleDataPath(sample)));
    }
    else if (isXyzGallerySample(sample))
    {
        label->setText(QStringLiteral(
            "<h2>%1</h2>"
            "<p>Demonstrates an XYZ tile based raster layer. The gallery preview opens an OSM tile layer; the standalone sample exposes the specific option set.</p>"
            "<p><b>APIs:</b> addOpenStreetMapLayer, GisLayerXYZ, tile cache / zoom / attribution options.</p>")
            .arg(sampleTitle(sample)));
    }
    else if (isSerializationGallerySample(sample))
    {
        label->setText(QStringLiteral(
            "<h2>%1</h2>"
            "<p>Shows geometry serialization workflow source beside a live preview of the geometry used by the sample.</p>"
            "<p><b>APIs:</b> WKT/WKB/GeoJSON readers and writers, shape conversion, in-memory layers.</p>")
            .arg(sampleTitle(sample)));
    }
    else
    {
        label->setText(QStringLiteral(
            "<h2>HelloMap</h2>"
            "<p>Loads a single shapefile and exposes the basic map navigation tools.</p>"
            "<p><b>APIs:</b> addLayerFromPath, fullExtent, zoomIn, zoomOut, ZoomBox, Pan.</p>"));
    }

    return label;
}

QWidget* createFlutterSoonPage()
{
    auto* label = new QLabel(QStringLiteral("Will be available soon."));
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral("font-size: 18px; color: #4f5b66;"));
    return label;
}

QTabWidget* finalizeSourceTabs(QTabWidget* tabs)
{
    tabs->addTab(createFlutterSoonPage(), QStringLiteral("Flutter"));
    return tabs;
}

QWidget* createSourcePage(SampleId sample);

void addSourceTabs(QTabWidget* targetTabs, SampleId sample)
{
    auto* sourceTabs = qobject_cast<QTabWidget*>(createSourcePage(sample));
    if (sourceTabs == nullptr)
        return;

    while (sourceTabs->count() > 0)
    {
        QWidget* page = sourceTabs->widget(0);
        QString platform = sourceTabs->tabText(0);
        if (platform == QStringLiteral("WinForms"))
            platform = QStringLiteral("Winforms");
        sourceTabs->removeTab(0);
        targetTabs->addTab(page, QStringLiteral("Source (%1)").arg(platform));
    }

    sourceTabs->deleteLater();
}

QWidget* createSourcePage(SampleId sample)
{
    auto* tabs = new QTabWidget();
    const QString standardFolder = standardTripleSourceFolder(sample);
    if (!standardFolder.isEmpty())
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/%1/main.cpp").arg(standardFolder)), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/%1.Winforms/MainForm.cs").arg(standardFolder)), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/%1.Wpf/MainWindow.xaml").arg(standardFolder)) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/%1.Wpf/MainWindow.xaml.cs").arg(standardFolder)),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::AddLayers)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/Basic/AddLayers/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/AddLayers.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/AddLayers.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/AddLayers.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::Project)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/Basic/Project/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/Project.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/Project.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/Project.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::LayerVisibility)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/LayerManagement/LayerVisibility/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/LayerVisibility.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/LayerVisibility.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/LayerVisibility.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::AddPointInteractive)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/EditingAndDigitizing/AddPointInteractive/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/AddPointInteractive.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/AddPointInteractive.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/AddPointInteractive.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::EditSession)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/EditingAndDigitizing/EditSession/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/EditSession.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/EditSession.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/EditSession.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::Scalebar)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/Basic/Scalebar/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/Scalebar.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/Scalebar.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/Scalebar.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::Minimap)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/Basic/Minimap/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/Minimap.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/Minimap.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/Minimap.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::LayerAddRemove)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/LayerManagement/LayerAddRemove/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/LayerAddRemove.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/LayerAddRemove.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/LayerAddRemove.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::LayerReorder)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/LayerManagement/LayerReorder/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/LayerReorder.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/LayerReorder.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/LayerReorder.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::LayerZoomTo)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/LayerManagement/LayerZoomTo/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/LayerZoomTo.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/LayerZoomTo.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/LayerZoomTo.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::LayerEvents)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/LayerManagement/LayerEvents/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/LayerEvents.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/LayerEvents.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/LayerEvents.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::LayerLoadOptions)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/LayerManagement/LayerLoadOptions/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/LayerLoadOptions.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/LayerLoadOptions.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/LayerLoadOptions.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::LayerLoadCancel)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/LayerManagement/LayerLoadCancel/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/LayerLoadCancel.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/LayerLoadCancel.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/LayerLoadCancel.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::InMemoryLayers)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/LayerManagement/InMemoryLayers/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/InMemoryLayers.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/InMemoryLayers.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/InMemoryLayers.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::LayerRefresh)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/LayerManagement/LayerRefresh/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/LayerRefresh.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/LayerRefresh.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/LayerRefresh.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::LayerExtent)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/LayerManagement/LayerExtent/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/LayerExtent.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/LayerExtent.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/LayerExtent.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::SimpleStyle)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/StyleAndSymbology/SimpleStyle/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/SimpleStyle.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/SimpleStyle.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/SimpleStyle.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::CategorizedRenderer)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/StyleAndSymbology/CategorizedRenderer/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/CategorizedRenderer.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/CategorizedRenderer.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/CategorizedRenderer.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::ClearRenderer)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/StyleAndSymbology/ClearRenderer/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/ClearRenderer.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/ClearRenderer.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/ClearRenderer.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::GraduatedRenderer)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/StyleAndSymbology/GraduatedRenderer/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/GraduatedRenderer.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/GraduatedRenderer.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/GraduatedRenderer.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::GraduatedRendererSize)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/StyleAndSymbology/GraduatedRendererSize/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/GraduatedRendererSize.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/GraduatedRendererSize.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/GraduatedRendererSize.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::ClassificationMethods)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/StyleAndSymbology/ClassificationMethods/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/ClassificationMethods.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/ClassificationMethods.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/ClassificationMethods.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::RuleBasedRenderer)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/StyleAndSymbology/RuleBasedRenderer/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/RuleBasedRenderer.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/RuleBasedRenderer.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/RuleBasedRenderer.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::SelectionStyle)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/StyleAndSymbology/SelectionStyle/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/SelectionStyle.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/SelectionStyle.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/SelectionStyle.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    if (sample == SampleId::StylePerFeature)
    {
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/StyleAndSymbology/StylePerFeature/main.cpp")), QStringLiteral("cpp")),
            QStringLiteral("Qt"));
        tabs->addTab(
            createSourceView(readTextFile(QStringLiteral("examples/StylePerFeature.Winforms/MainForm.cs")), QStringLiteral("csharp")),
            QStringLiteral("WinForms"));
        tabs->addTab(
            createSourceView(
                readTextFile(QStringLiteral("examples/StylePerFeature.Wpf/MainWindow.xaml")) +
                QStringLiteral("\n\n") +
                readTextFile(QStringLiteral("examples/StylePerFeature.Wpf/MainWindow.xaml.cs")),
                QStringLiteral("xaml")),
            QStringLiteral("WPF"));
        return finalizeSourceTabs(tabs);
    }

    tabs->addTab(
        createSourceView(readTextFile(QStringLiteral("examples/Basic/HelloMap/main.cpp")), QStringLiteral("cpp")),
        QStringLiteral("Qt"));
    tabs->addTab(
        createSourceView(readTextFile(QStringLiteral("examples/HelloMap.Winforms/MainForm.cs")), QStringLiteral("csharp")),
        QStringLiteral("WinForms"));
    tabs->addTab(
        createSourceView(
            readTextFile(QStringLiteral("examples/HelloMap.Wpf/MainWindow.xaml")) +
            QStringLiteral("\n\n") +
            readTextFile(QStringLiteral("examples/HelloMap.Wpf/MainWindow.xaml.cs")),
            QStringLiteral("xaml")),
        QStringLiteral("WPF"));
    return finalizeSourceTabs(tabs);
}

QWidget* createSampleContent(SampleId sample, std::function<void()> ready = {})
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* title = new QLabel(sampleTitle(sample));
    title->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 600; padding: 8px 10px;"));
    layout->addWidget(title);

    auto* tabs = new QTabWidget();
    tabs->addTab(createLivePage(sample, ready), QStringLiteral("Live sample"));
    tabs->addTab(createDescriptionPage(sample), QStringLiteral("Description"));
    addSourceTabs(tabs, sample);
    layout->addWidget(tabs, 1);

    if (!sampleKeepsGalleryBusyUntilReady(sample) && ready)
        QTimer::singleShot(0, page, std::move(ready));

    return page;
}

QListWidget* createSidebar()
{
    auto* list = new QListWidget();

    const auto addHeader = [list](const QString& title)
    {
        auto* item = new QListWidgetItem(title);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        QFont font = item->font();
        font.setBold(true);
        item->setFont(font);
        list->addItem(item);
    };

    const auto addSample = [list](SampleId sample)
    {
        auto* item = new QListWidgetItem(QStringLiteral("  %1").arg(sampleTitle(sample)));
        item->setData(Qt::UserRole, static_cast<int>(sample));
        list->addItem(item);
    };

    addHeader(QStringLiteral("Basic"));
    addSample(SampleId::HelloMap);
    addSample(SampleId::AddLayers);
    addSample(SampleId::Project);
    addSample(SampleId::Scalebar);
    addSample(SampleId::Minimap);

    addHeader(QStringLiteral("Layer Management"));
    addSample(SampleId::LayerAddRemove);
    addSample(SampleId::LayerReorder);
    addSample(SampleId::LayerExtent);
    addSample(SampleId::LayerEvents);
    addSample(SampleId::InMemoryLayers);
    addSample(SampleId::LayerRefresh);
    addSample(SampleId::LayerLoadCancel);
    addSample(SampleId::LayerLoadOptions);
    addSample(SampleId::LayerZoomTo);
    addSample(SampleId::LayerVisibility);

    addHeader(QStringLiteral("Symbology"));
    addSample(SampleId::CategorizedRenderer);
    addSample(SampleId::ClearRenderer);
    addSample(SampleId::GraduatedRenderer);
    addSample(SampleId::GraduatedRendererSize);
    addSample(SampleId::ClassificationMethods);
    addSample(SampleId::RuleBasedRenderer);
    addSample(SampleId::SimpleStyle);
    addSample(SampleId::SelectionStyle);
    addSample(SampleId::StylePerFeature);

    addHeader(QStringLiteral("Labels"));
    addSample(SampleId::LabelCollisionOff);

    addHeader(QStringLiteral("Editing"));
    addSample(SampleId::AddPointInteractive);
    addSample(SampleId::AddPolylineInteractive);
    addSample(SampleId::AddPolygonInteractive);
    addSample(SampleId::AddPointProgrammatic);
    addSample(SampleId::AddPolylineProgrammatic);
    addSample(SampleId::AddPolygonProgrammatic);
    addSample(SampleId::DeleteFeature);
    addSample(SampleId::MoveFeatureTool);
    addSample(SampleId::EditSession);

    addHeader(QStringLiteral("Selection & Query"));
    addHeader(QStringLiteral("Topology & Geometry"));

    addHeader(QStringLiteral("Raster & XYZ Tiles"));
    addSample(SampleId::OpenStreetMap);
    addSample(SampleId::GeoTiffLoad);
    addSample(SampleId::RasterWorldTransform);
    addSample(SampleId::RasterOverview);
    addSample(SampleId::RasterTileCache);
    addSample(SampleId::XyzPresets);
    addSample(SampleId::XyzCustomUrl);
    addSample(SampleId::XyzLocalCache);
    addSample(SampleId::XyzTileSize);
    addSample(SampleId::XyzMinMaxZoom);
    addSample(SampleId::XyzAttribution);
    addSample(SampleId::XyzDiagnostics);

    addHeader(QStringLiteral("Serialization"));
    addSample(SampleId::WktOverlay);
    addSample(SampleId::WktRoundtrip);

    addHeader(QStringLiteral("Formats"));
    addSample(SampleId::ShapefileLoad);
    addSample(SampleId::ShapefileSaveAs);
    addSample(SampleId::TabLoad);
    addSample(SampleId::MifLoad);
    addSample(SampleId::GeoPackageLoad);
    addSample(SampleId::KmlLoad);
    addSample(SampleId::DxfLoad);

    list->setCurrentRow(1);
    list->setFrameShape(QFrame::NoFrame);

    return list;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1400, 820);
    window.setWindowTitle(QStringLiteral("GeoKernel Samples Gallery"));

    auto* root = new QWidget(&window);
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* header = new QLabel(QStringLiteral("GeoKernel Samples Gallery"));
    header->setStyleSheet(QStringLiteral(
        "background: #5f008f; color: white; font-size: 18px; font-weight: 600; padding: 12px;"));
    rootLayout->addWidget(header);

    auto* sidebarPanel = new QWidget();
    auto* sidebarLayout = new QVBoxLayout(sidebarPanel);
    sidebarLayout->setContentsMargins(10, 10, 10, 10);

    auto* search = new QLineEdit();
    search->setPlaceholderText(QStringLiteral("Search..."));
    sidebarLayout->addWidget(search);

    auto* sampleList = createSidebar();
    sidebarLayout->addWidget(sampleList, 1);

    auto* contentHost = new QWidget();
    auto* contentLayout = new QVBoxLayout(contentHost);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(createSampleContent(SampleId::HelloMap));

    auto navigationBusy = std::make_shared<bool>(false);
    QObject::connect(sampleList, &QListWidget::itemClicked, contentHost, [contentLayout, sampleList, navigationBusy](QListWidgetItem* item)
    {
        if (*navigationBusy)
            return;

        const QVariant sampleValue = item->data(Qt::UserRole);
        if (!sampleValue.isValid())
            return;

        *navigationBusy = true;
        sampleList->setEnabled(false);

        while (QLayoutItem* child = contentLayout->takeAt(0))
        {
            if (QWidget* widget = child->widget())
                widget->deleteLater();
            delete child;
        }

        const SampleId sample = static_cast<SampleId>(sampleValue.toInt());

        QPointer<QListWidget> guardedSampleList(sampleList);
        contentLayout->addWidget(createSampleContent(sample, [guardedSampleList, navigationBusy]
        {
            *navigationBusy = false;
            if (guardedSampleList != nullptr)
                guardedSampleList->setEnabled(true);
        }));
    });

    auto* splitter = new QSplitter();
    splitter->addWidget(sidebarPanel);
    splitter->addWidget(contentHost);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({ 300, 1100 });
    rootLayout->addWidget(splitter, 1);

    window.setCentralWidget(root);
    window.show();

    return app.exec();
}
