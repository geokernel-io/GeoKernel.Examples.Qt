#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QStatusBar>
#include <QString>
#include <QToolBar>

#include <memory>

#include "Viewer/GisViewer.h"
#include "Layers/GisLayerVector.h"
#include "Symbology/GisColorRampRegistry.h"
#include "Symbology/GisSymbolRendererFactory.h"

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Symbology;

QString sampleDataPath(const QString& fileName)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/data/%1").arg(fileName)));
}

GisLayerStyle baseCountyStyle()
{
    GisLayerStyle style;
    style.setFillColor(QStringLiteral("#DCE8E4"));
    style.setFillOpacity(225);
    style.setLineColor(QStringLiteral("#536B68"));
    style.setLineWidth(0.8f);
    return style;
}

QIcon legendIcon(const GisLayerStyle& style)
{
    QPixmap pixmap(42, 24);
    pixmap.fill(Qt::transparent);

    QColor fill(style.fillColor());
    fill.setAlpha(style.fillOpacity());

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(style.lineColor()), 1.5));
    painter.setBrush(fill);
    painter.drawRect(QRectF(5, 4, 32, 16));
    return QIcon(pixmap);
}

void updateLegend(QListWidget& legend, const GisLayerVector& layer)
{
    legend.clear();
    if (layer.symbolRenderer() == nullptr)
        return;

    for (const GisSymbolLegendItem& item : layer.symbolRenderer()->legendItems())
    {
        if (!item.enabled)
            continue;

        auto* row = new QListWidgetItem(
            legendIcon(item.style),
            item.label.isEmpty() ? QStringLiteral("(empty)") : item.label);
        legend.addItem(row);
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("GraduatedRenderer"));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(247, 248, 250));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    auto* rampCombo = new QComboBox(toolbar);
    rampCombo->addItems(GisColorRampRegistry::names());
    rampCombo->setCurrentIndex(rampCombo->findText(QStringLiteral("GreenBlue"), Qt::MatchFixedString));
    toolbar->addWidget(new QLabel(QStringLiteral("Color ramp: "), toolbar));
    toolbar->addWidget(rampCombo);
    window.addToolBar(toolbar);

    auto* legendList = new QListWidget(&window);
    auto* legendDock = new QDockWidget(QStringLiteral("POPULATION classes"), &window);
    legendDock->setWidget(legendList);
    legendDock->setMinimumWidth(230);
    window.addDockWidget(Qt::LeftDockWidgetArea, legendDock);

    const QString path = sampleDataPath(QStringLiteral("shapefile/california.shp"));
    if (!QFileInfo::exists(path))
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("GraduatedRenderer"),
            QStringLiteral("Sample shapefile was not found:\n%1").arg(path));
        return 1;
    }

    viewer->addOpenStreetMapLayer();

    QString errorMessage;
    if (!viewer->addLayerFromPath(path, &errorMessage))
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("GraduatedRenderer"),
            QStringLiteral("Layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? path : errorMessage));
        return 1;
    }

    auto* countiesLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
    if (countiesLayer == nullptr)
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("GraduatedRenderer"),
            QStringLiteral("Loaded layer is not a vector layer."));
        return 1;
    }

    countiesLayer->setName(QStringLiteral("California counties - graduated by POPULATION"));
    countiesLayer->style() = baseCountyStyle();

    auto applyRenderer = [&]() -> bool
    {
        auto renderer = GisSymbolRendererFactory::createGraduatedRenderer(
            *countiesLayer,
            QStringLiteral("POPULATION"),
            GisClassificationMethod::NaturalBreaks,
            5,
            GisColorRampRegistry::ramp(rampCombo->currentText()),
            baseCountyStyle());

        if (!renderer)
            return false;

        countiesLayer->setSymbolRenderer(std::move(renderer));
        updateLegend(*legendList, *countiesLayer);
        viewer->invalidateRenderCache(true, true);
        viewer->update();
        window.statusBar()->showMessage(
            QStringLiteral("Graduated renderer applied: POPULATION / %1").arg(rampCombo->currentText()));
        return true;
    };

    if (!applyRenderer())
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("GraduatedRenderer"),
            QStringLiteral("Could not create graduated renderer from POPULATION field."));
        return 1;
    }

    QObject::connect(rampCombo, &QComboBox::currentTextChanged, &window, [&]
    {
        if (!applyRenderer())
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("GraduatedRenderer"),
                QStringLiteral("Could not create graduated renderer from POPULATION field."));
        }
    });    

    window.show();
    viewer->zoomToLayer(0);

    return app.exec();
}
