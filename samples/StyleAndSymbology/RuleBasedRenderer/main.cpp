#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QIcon>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QSize>
#include <QStatusBar>
#include <QString>

#include <algorithm>
#include <memory>

#include "Viewer/GisViewer.h"
#include "Layers/GisLayerVector.h"
#include "Symbology/GisRuleBasedSymbolRenderer.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Symbology;

constexpr float MinimumPointSize = 3.0f;
constexpr float MaximumPointSize = 36.0f;

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

GisLayerStyle cityStyle(const QString& fillColor, const QString& outlineColor, float pointSize)
{
    GisLayerStyle style;
    QColor fill(fillColor);
    fill.setAlpha(165);
    QColor outline(outlineColor);
    outline.setAlpha(220);

    style.setPointColor(fill.name(QColor::HexArgb));
    style.setLineColor(outline.name(QColor::HexArgb));
    style.setPointSize(pointSize);
    style.setLineWidth(std::clamp(pointSize * 0.07f, 0.9f, 2.2f));
    return style;
}

GisSymbolRule popClassRule(
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
    rule.style = cityStyle(fillColor, outlineColor, pointSize);
    return rule;
}

QIcon legendIcon(const GisLayerStyle& style)
{
    QPixmap pixmap(72, 42);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(style.lineColor()), std::max(1.0f, style.lineWidth())));
    painter.setBrush(QColor(style.pointColor()));

    const double radius = std::clamp(static_cast<double>(style.pointSize()), static_cast<double>(MinimumPointSize), static_cast<double>(MaximumPointSize)) / 2.0;
    painter.drawEllipse(QPointF(36.0, 21.0), radius, radius);
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
            item.label.isEmpty() ? QStringLiteral("(unnamed rule)") : item.label);
        row->setSizeHint(QSize(210, 44));
        legend.addItem(row);
    }
}

std::unique_ptr<GisRuleBasedSymbolRenderer> createPopClassRenderer()
{
    auto renderer = std::make_unique<GisRuleBasedSymbolRenderer>();
    renderer->setDefaultStyle(cityStyle(QStringLiteral("#CBD5E1"), QStringLiteral("#64748B"), MinimumPointSize));

    renderer->addRule(popClassRule(
        QStringLiteral("Less than 50,000"),
        QStringLiteral("Less than 50,000"),
        QStringLiteral("#A8ADB7"),
        QStringLiteral("#626975"),
        3.0f));
    renderer->addRule(popClassRule(
        QStringLiteral("50,000 to 100,000"),
        QStringLiteral("50,000 to 100,000"),
        QStringLiteral("#5DADE2"),
        QStringLiteral("#21618C"),
        6.5f));
    renderer->addRule(popClassRule(
        QStringLiteral("100,000 to 250,000"),
        QStringLiteral("100,000 to 250,000"),
        QStringLiteral("#58D68D"),
        QStringLiteral("#1E8449"),
        11.0f));
    renderer->addRule(popClassRule(
        QStringLiteral("250,000 to 500,000"),
        QStringLiteral("250,000 to 500,000"),
        QStringLiteral("#F5B041"),
        QStringLiteral("#935116"),
        17.0f));
    renderer->addRule(popClassRule(
        QStringLiteral("500,000 to 1,000,000"),
        QStringLiteral("500,000 to 1,000,000"),
        QStringLiteral("#EC7063"),
        QStringLiteral("#943126"),
        25.0f));
    renderer->addRule(popClassRule(
        QStringLiteral("1,000,000 to 5,000,000"),
        QStringLiteral("1,000,000 to 5,000,000"),
        QStringLiteral("#8E2C1B"),
        QStringLiteral("#4A160E"),
        MaximumPointSize));

    return renderer;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon(QStringLiteral("GeoKernelAppIcon.ico")));

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("RuleBasedRenderer"));

    auto* viewer = new GisViewer(&window);
    viewer->setMapBackgroundColor(QColor(247, 248, 250));
    viewer->setActiveTool(GisViewerTool::Pan);
    window.setCentralWidget(viewer);

    auto* legendList = new QListWidget(&window);
    auto* legendDock = new QDockWidget(QStringLiteral("POP_CLASS rules"), &window);
    legendDock->setWidget(legendList);
    legendDock->setMinimumWidth(250);
    window.addDockWidget(Qt::LeftDockWidgetArea, legendDock);

    const QString path = sampleDataPath(QStringLiteral("shapefile/cities_4326.shp"));
    if (!QFileInfo::exists(path))
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("RuleBasedRenderer"),
            QStringLiteral("Sample shapefile was not found:\n%1").arg(path));
        return 1;
    }

    QString errorMessage;
    if (!viewer->addLayerFromPath(path, &errorMessage))
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("RuleBasedRenderer"),
            QStringLiteral("Layer could not be loaded:\n%1")
                .arg(errorMessage.isEmpty() ? path : errorMessage));
        return 1;
    }

    auto* citiesLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
    if (citiesLayer == nullptr)
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("RuleBasedRenderer"),
            QStringLiteral("Loaded layer is not a vector layer."));
        return 1;
    }

    citiesLayer->setName(QStringLiteral("Cities - rule based by POP_CLASS"));
    citiesLayer->style() = cityStyle(QStringLiteral("#CBD5E1"), QStringLiteral("#64748B"), MinimumPointSize);
    citiesLayer->setSymbolRenderer(createPopClassRenderer());
    updateLegend(*legendList, *citiesLayer);
    
    window.statusBar()->showMessage(QStringLiteral("Rule based renderer applied: POP_CLASS"));

    window.show();
    viewer->setViewExtent(GisExtent(-180.0, -58.0, 180.0, 82.0));

    return app.exec();
}
