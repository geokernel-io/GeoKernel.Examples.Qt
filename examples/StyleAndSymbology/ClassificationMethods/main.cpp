#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDockWidget>
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

#include "Helpers.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Symbology;

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
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("ClassificationMethods"));

    auto* viewer = new GisViewer(&window);
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
    methodCombo->setEnabled(false);

    auto* legendList = new QListWidget(&window);
    auto* legendDock = new QDockWidget(QStringLiteral("POPULATION - Equal Interval"), &window);
    legendDock->setWidget(legendList);
    legendDock->setMinimumWidth(245);
    window.addDockWidget(Qt::LeftDockWidgetArea, legendDock);

    window.show();

    QMetaObject::invokeMethod(&window, [&window, viewer, methodCombo, legendList, legendDock]
    {
        legendList->clear();
        legendList->addItem(QStringLiteral("Preparing California sample data..."));

        const QString path = ensureSampleFile(
            QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/california.zip")),
            QStringLiteral("california.zip"),
            QStringLiteral("california"),
            QStringLiteral("california.shp"),
            &window);
        if (path.isEmpty())
        {
            legendList->clear();
            legendList->addItem(QStringLiteral("Sample data could not be prepared."));
            window.statusBar()->showMessage(QStringLiteral("Sample data could not be prepared."));
            return;
        }

        viewer->addOpenStreetMapLayer();

        QString errorMessage;
        if (!viewer->addLayerFromPath(path, &errorMessage))
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("ClassificationMethods"),
                QStringLiteral("Layer could not be loaded:\n%1")
                    .arg(errorMessage.isEmpty() ? path : errorMessage));
            legendList->clear();
            legendList->addItem(QStringLiteral("Layer could not be loaded."));
            window.statusBar()->showMessage(QStringLiteral("Layer could not be loaded."));
            return;
        }

        auto* countiesLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
        if (countiesLayer == nullptr)
        {
            QMessageBox::critical(
                &window,
                QStringLiteral("ClassificationMethods"),
                QStringLiteral("Loaded layer is not a vector layer."));
            legendList->clear();
            legendList->addItem(QStringLiteral("Loaded layer is not a vector layer."));
            window.statusBar()->showMessage(QStringLiteral("Loaded layer is not a vector layer."));
            return;
        }

        GisLayerStyle countyStyle;
        countyStyle.setFillColor(QStringLiteral("#DCE8E4"));
        countyStyle.setFillOpacity(225);
        countyStyle.setLineColor(QStringLiteral("#536B68"));
        countyStyle.setLineWidth(0.8f);

        countiesLayer->setName(QStringLiteral("California counties - classification methods"));
        countiesLayer->style() = countyStyle;

        auto applyRenderer = [methodCombo, legendList, legendDock, viewer, countiesLayer, countyStyle, &window]() -> bool
        {
            const QString methodText = methodCombo->currentText();
            auto renderer = GisSymbolRendererFactory::createGraduatedRenderer(
                *countiesLayer,
                QStringLiteral("POPULATION"),
                methodFromText(methodText),
                5,
                GisColorRampRegistry::ramp(QStringLiteral("GreenBlue")),
                countyStyle,
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
            legendList->clear();
            legendList->addItem(QStringLiteral("Graduated renderer could not be created."));
            window.statusBar()->showMessage(QStringLiteral("Graduated renderer could not be created."));
            return;
        }

        QObject::connect(methodCombo, &QComboBox::currentTextChanged, &window, [applyRenderer]
        {
            if (!applyRenderer())
            {
                QMessageBox::critical(
                    nullptr,
                    QStringLiteral("ClassificationMethods"),
                    QStringLiteral("Could not create graduated renderer from POPULATION field."));
            }
        });

        methodCombo->setEnabled(true);
        viewer->zoomToLayer(0);
    });

    return app.exec();
}
