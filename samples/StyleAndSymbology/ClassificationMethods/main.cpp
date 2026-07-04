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

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Symbology;

QIcon sampleIcon(const QString& fileName)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString path = QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/images/%1").arg(fileName)));
    QIcon icon;

    for (const auto mode : { QIcon::Normal, QIcon::Active, QIcon::Selected, QIcon::Disabled })
    {
        icon.addFile(path, QSize(), mode, QIcon::Off);
        icon.addFile(path, QSize(), mode, QIcon::On);
    }

    return icon;
}

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

GisClassificationMethod methodFromText(const QString& text)
{
    if (text == QStringLiteral("Quantile"))
        return GisClassificationMethod::Quantile;
    if (text == QStringLiteral("Standard Deviation"))
        return GisClassificationMethod::StandardDeviation;

    return GisClassificationMethod::EqualInterval;
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
    app.setWindowIcon(sampleIcon(QStringLiteral("GeoKernelAppIcon.ico")));

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("ClassificationMethods"));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(228, 228, 228));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    auto* methodCombo = new QComboBox(toolbar);
    methodCombo->addItems({
        QStringLiteral("Equal Interval"),
        QStringLiteral("Quantile"),
        QStringLiteral("Standard Deviation")
    });
    toolbar->addWidget(new QLabel(QStringLiteral("Method: "), toolbar));
    toolbar->addWidget(methodCombo);
    window.addToolBar(toolbar);

    methodCombo->setCurrentIndex(1);

    auto* legendList = new QListWidget(&window);
    auto* legendDock = new QDockWidget(QStringLiteral("POPULATION - Equal Interval"), &window);
    legendDock->setWidget(legendList);
    legendDock->setMinimumWidth(245);
    window.addDockWidget(Qt::LeftDockWidgetArea, legendDock);

    const QString path = sampleDataPath(QStringLiteral("shapefile/california.shp"));
    if (!QFileInfo::exists(path))
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("ClassificationMethods"),
            QStringLiteral("Sample shapefile was not found:\n%1").arg(path));
        return 1;
    }

    QString errorMessage;
    if (!viewer->addLayerFromPath(path, &errorMessage))
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("ClassificationMethods"),
            QStringLiteral("Layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? path : errorMessage));
        return 1;
    }

    auto* countiesLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
    if (countiesLayer == nullptr)
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("ClassificationMethods"),
            QStringLiteral("Loaded layer is not a vector layer."));
        return 1;
    }

    countiesLayer->setName(QStringLiteral("California counties - classification methods"));
    countiesLayer->style() = baseCountyStyle();

    auto applyRenderer = [&]() -> bool
    {
        const QString methodText = methodCombo->currentText();
        auto renderer = GisSymbolRendererFactory::createGraduatedRenderer(
            *countiesLayer,
            QStringLiteral("POPULATION"),
            methodFromText(methodText),
            5,
            GisColorRampRegistry::ramp(QStringLiteral("GreenBlue")),
            baseCountyStyle(),
            1.0);

        if (!renderer)
            return false;

        countiesLayer->setSymbolRenderer(std::move(renderer));
        updateLegend(*legendList, *countiesLayer);
        legendDock->setWindowTitle(QStringLiteral("POPULATION - %1").arg(methodText));
        viewer->invalidateRenderCache(true, true);
        viewer->update();
        window.statusBar()->showMessage(
            QStringLiteral("Classification method applied: POPULATION / %1").arg(methodText));
        return true;
    };

    if (!applyRenderer())
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("ClassificationMethods"),
            QStringLiteral("Could not create graduated renderer from POPULATION field."));
        return 1;
    }

    QObject::connect(methodCombo, &QComboBox::currentTextChanged, &window, [&]
    {
        if (!applyRenderer())
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("ClassificationMethods"),
                QStringLiteral("Could not create graduated renderer from POPULATION field."));
        }
    });

    window.show();
    viewer->fullExtent();

    return app.exec();
}
