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
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    auto* rampCombo = new QComboBox(toolbar);
    rampCombo->addItems(GisColorRampRegistry::names());
    rampCombo->setCurrentIndex(rampCombo->findText(QStringLiteral("GreenBlue"), Qt::MatchFixedString));
    rampCombo->setEnabled(false);
    toolbar->addWidget(new QLabel(QStringLiteral("Color ramp: "), toolbar));
    toolbar->addWidget(rampCombo);
    window.addToolBar(toolbar);

    auto* legendList = new QListWidget(&window);
    auto* legendDock = new QDockWidget(QStringLiteral("POPULATION classes"), &window);
    legendDock->setWidget(legendList);
    legendDock->setMinimumWidth(230);
    window.addDockWidget(Qt::LeftDockWidgetArea, legendDock);

    window.show();

    QMetaObject::invokeMethod(&window, [&window, viewer, legendList, rampCombo]
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
                QStringLiteral("GraduatedRenderer"),
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
                QStringLiteral("GraduatedRenderer"),
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

        countiesLayer->setName(QStringLiteral("California counties - graduated by POPULATION"));
        countiesLayer->style() = countyStyle;

        auto applyRenderer = [viewer, legendList, rampCombo, countiesLayer, &window, countyStyle]() -> bool
        {
            auto renderer = GisSymbolRendererFactory::createGraduatedRenderer(
                *countiesLayer,
                QStringLiteral("POPULATION"),
                GisClassificationMethod::NaturalBreaks,
                5,
                GisColorRampRegistry::ramp(rampCombo->currentText()),
                countyStyle);

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
            legendList->clear();
            legendList->addItem(QStringLiteral("Graduated renderer could not be created."));
            window.statusBar()->showMessage(QStringLiteral("Graduated renderer could not be created."));
            return;
        }

        QObject::connect(rampCombo, &QComboBox::currentTextChanged, &window, [applyRenderer]
        {
            if (!applyRenderer())
            {
                QMessageBox::critical(
                    nullptr,
                    QStringLiteral("GraduatedRenderer"),
                    QStringLiteral("Could not create graduated renderer from POPULATION field."));
            }
        });

        rampCombo->setEnabled(true);
        viewer->zoomToLayer(0);
    });

    return app.exec();
}
