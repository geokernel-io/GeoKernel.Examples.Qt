#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <memory>

#include "../Common/Helpers.h"
#include "Layers/GisLayerVector.h"
#include "Symbology/GisColorRampRegistry.h"
#include "Symbology/GisSymbolRendererFactory.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Layers::Defs;
using namespace GeoKernel::Core::Symbology;
using namespace GeoKernel::Viewer;

constexpr const char* AdminAreasFile = "shapefile/california.shp";

enum class RendererKind
{
    Categorized = 0,
    Graduated = 1
};

bool isNumericType(GisAttributeType type)
{
    return type == GisAttributeType::Integer || type == GisAttributeType::Double;
}

int enumValue(RendererKind value)
{
    return static_cast<int>(value);
}

int enumValue(GisClassificationMethod value)
{
    return static_cast<int>(value);
}

int enumValue(GisColorRampMode value)
{
    return static_cast<int>(value);
}

int enumValue(GisSymbolStyleTarget value)
{
    return static_cast<int>(value);
}

RendererKind currentRendererKind(const QComboBox& combo)
{
    return static_cast<RendererKind>(combo.currentData().toInt());
}

GisClassificationMethod currentMethod(const QComboBox& combo)
{
    return static_cast<GisClassificationMethod>(combo.currentData().toInt());
}

GisColorRampMode currentRampMode(const QComboBox& combo)
{
    return static_cast<GisColorRampMode>(combo.currentData().toInt());
}

GisSymbolStyleTarget currentStyleTarget(const QComboBox& combo)
{
    return static_cast<GisSymbolStyleTarget>(combo.currentData().toInt());
}

GisLayerStyle classificationBaseStyle(const GisLayerVector& layer)
{
    GisLayerStyle style = layer.style();
    style.setFillOpacity(220);
    style.setLineColor(QStringLiteral("#536B68"));
    style.setLineWidth(0.8f);
    return style;
}

QVector<double> parseManualBreaks(const QString& text, bool& ok)
{
    ok = true;
    QVector<double> values;

    const QStringList parts = text.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);

    for (const QString& part : parts)
    {
        bool valueOk = false;
        const double value = part.toDouble(&valueOk);
        if (!valueOk || !std::isfinite(value))
        {
            ok = false;
            return {};
        }

        values.append(value);
    }

    std::sort(values.begin(), values.end());
    return values;
}

QIcon legendIcon(const GisLayerStyle& style)
{
    QPixmap pixmap(38, 22);
    pixmap.fill(Qt::transparent);

    QColor fill(style.fillColor());
    fill.setAlpha(std::clamp(style.fillOpacity(), 0, 255));

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(style.lineColor()), 2.0));
    painter.setBrush(fill);
    painter.drawRect(QRectF(5, 4, 28, 14));
    return QIcon(pixmap);
}

void updateLegend(QListWidget& legend, const GisLayerVector* layer)
{
    legend.clear();
    if (layer == nullptr || layer->symbolRenderer() == nullptr)
        return;

    const QVector<GisSymbolLegendItem> items = layer->symbolRenderer()->legendItems();
    for (const GisSymbolLegendItem& item : items)
    {
        if (!item.enabled)
            continue;

        auto* row = new QListWidgetItem(
            legendIcon(item.style),
            item.label.isEmpty() ? QStringLiteral("(unlabeled)") : item.label);
        legend.addItem(row);
    }
}

void populateFields(QComboBox& fieldCombo, const GisLayerVector* layer, RendererKind rendererKind)
{
    const QString selected = fieldCombo.currentText();
    fieldCombo.clear();
    if (layer == nullptr)
        return;

    const bool numericOnly = rendererKind == RendererKind::Graduated;
    for (const GisAttributeDefinition& definition : layer->attributeDefinitions())
    {
        const QString name = definition.name.trimmed();
        if (name.isEmpty())
            continue;

        if (!numericOnly || isNumericType(definition.type))
            fieldCombo.addItem(name);
    }

    const QString preferred = rendererKind == RendererKind::Graduated
        ? QStringLiteral("POPULATION")
        : QStringLiteral("STATEFP");
    int index = fieldCombo.findText(selected, Qt::MatchFixedString);
    if (index < 0)
        index = fieldCombo.findText(preferred, Qt::MatchFixedString);
    if (index >= 0)
        fieldCombo.setCurrentIndex(index);
}

void syncControls(const QComboBox& rendererCombo, QComboBox& methodCombo, QLabel& classCountLabel, QSpinBox& classCountSpin, QLabel& intervalLabel, QDoubleSpinBox& intervalSpin, QLabel& manualBreaksLabel, QLineEdit& manualBreaksEdit, QComboBox& rampModeCombo)
{
    const bool graduated = currentRendererKind(rendererCombo) == RendererKind::Graduated;
    const GisClassificationMethod method = currentMethod(methodCombo);
    const bool usesClassCount =
        !graduated ||
        (method != GisClassificationMethod::Manual &&
            method != GisClassificationMethod::DefinedInterval &&
            method != GisClassificationMethod::Quartile &&
            method != GisClassificationMethod::StandardDeviation &&
            method != GisClassificationMethod::StandardDeviationWithCentral);
    const bool usesInterval =
        graduated &&
        (method == GisClassificationMethod::DefinedInterval ||
            method == GisClassificationMethod::StandardDeviation ||
            method == GisClassificationMethod::StandardDeviationWithCentral);
    const bool usesManualBreaks = graduated && method == GisClassificationMethod::Manual;

    methodCombo.setEnabled(graduated);
    classCountLabel.setText(graduated ? QStringLiteral("Classes") : QStringLiteral("Categories"));
    classCountLabel.setEnabled(usesClassCount);
    classCountSpin.setEnabled(usesClassCount);
    intervalLabel.setText(
        method == GisClassificationMethod::StandardDeviation ||
        method == GisClassificationMethod::StandardDeviationWithCentral
        ? QStringLiteral("Std dev step")
        : QStringLiteral("Interval"));
    intervalLabel.setEnabled(usesInterval);
    intervalSpin.setEnabled(usesInterval);
    manualBreaksLabel.setEnabled(usesManualBreaks);
    manualBreaksEdit.setEnabled(usesManualBreaks);
    rampModeCombo.setEnabled(graduated);
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.resize(1240, 760);
    window.setWindowTitle(QStringLiteral("Classification"));

    auto* central = new QWidget(&window);
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* controls = new QWidget(central);
    auto* controlsLayout = new QGridLayout(controls);
    controlsLayout->setContentsMargins(8, 6, 8, 6);
    controlsLayout->setHorizontalSpacing(8);
    controlsLayout->setVerticalSpacing(4);

    auto* rendererCombo = new QComboBox(controls);
    rendererCombo->addItem(QStringLiteral("Categorized"), enumValue(RendererKind::Categorized));
    rendererCombo->addItem(QStringLiteral("Graduated"), enumValue(RendererKind::Graduated));
    rendererCombo->setCurrentIndex(rendererCombo->findData(enumValue(RendererKind::Graduated)));

    auto* fieldCombo = new QComboBox(controls);

    auto* methodCombo = new QComboBox(controls);
    methodCombo->addItem(QStringLiteral("Equal Interval"), enumValue(GisClassificationMethod::EqualInterval));
    methodCombo->addItem(QStringLiteral("Quantile"), enumValue(GisClassificationMethod::Quantile));
    methodCombo->addItem(QStringLiteral("Quartile"), enumValue(GisClassificationMethod::Quartile));
    methodCombo->addItem(QStringLiteral("Natural Breaks"), enumValue(GisClassificationMethod::NaturalBreaks));
    methodCombo->addItem(QStringLiteral("Geometrical Interval"), enumValue(GisClassificationMethod::GeometricalInterval));
    methodCombo->addItem(QStringLiteral("K-Means"), enumValue(GisClassificationMethod::KMeans));
    methodCombo->addItem(QStringLiteral("K-Means Spatial"), enumValue(GisClassificationMethod::KMeansSpatial));
    methodCombo->addItem(QStringLiteral("Standard Deviation"), enumValue(GisClassificationMethod::StandardDeviation));
    methodCombo->addItem(QStringLiteral("Standard Deviation with Central"), enumValue(GisClassificationMethod::StandardDeviationWithCentral));
    methodCombo->addItem(QStringLiteral("Defined Interval"), enumValue(GisClassificationMethod::DefinedInterval));
    methodCombo->addItem(QStringLiteral("Manual"), enumValue(GisClassificationMethod::Manual));
    methodCombo->setCurrentIndex(methodCombo->findData(enumValue(GisClassificationMethod::NaturalBreaks)));

    auto* classCountLabel = new QLabel(QStringLiteral("Classes"), controls);
    auto* classCountSpin = new QSpinBox(controls);
    classCountSpin->setRange(2, 64);
    classCountSpin->setValue(15);

    auto* intervalLabel = new QLabel(QStringLiteral("Interval"), controls);
    auto* intervalSpin = new QDoubleSpinBox(controls);
    intervalSpin->setDecimals(4);
    intervalSpin->setRange(0.0001, 1000000000.0);
    intervalSpin->setValue(100000.0);

    auto* manualBreaksLabel = new QLabel(QStringLiteral("Manual breaks"), controls);
    auto* manualBreaksEdit = new QLineEdit(QStringLiteral("0, 100000, 500000, 1000000, 5000000, 10000000"), controls);

    auto* renderByCombo = new QComboBox(controls);
    renderByCombo->addItem(QStringLiteral("Color"), enumValue(GisSymbolStyleTarget::Color));
    renderByCombo->addItem(QStringLiteral("Size / Width"), enumValue(GisSymbolStyleTarget::SizeOrWidth));
    renderByCombo->addItem(QStringLiteral("Outline color"), enumValue(GisSymbolStyleTarget::OutlineColor));
    renderByCombo->addItem(QStringLiteral("Outline width"), enumValue(GisSymbolStyleTarget::OutlineWidth));

    auto* rampCombo = new QComboBox(controls);
    rampCombo->addItems(GisColorRampRegistry::names());
    rampCombo->setCurrentIndex(rampCombo->findText(QStringLiteral("GreenBlue"), Qt::MatchFixedString));

    auto* rampModeCombo = new QComboBox(controls);
    rampModeCombo->addItem(QStringLiteral("Continuous"), enumValue(GisColorRampMode::Continuous));
    rampModeCombo->addItem(QStringLiteral("Discrete"), enumValue(GisColorRampMode::Discrete));

    auto* reverseCheck = new QCheckBox(QStringLiteral("Reverse"), controls);
    auto* applyButton = new QPushButton(QStringLiteral("Apply"), controls);
    auto* clearButton = new QPushButton(QStringLiteral("Clear"), controls);
    auto* fullExtentButton = new QPushButton(QStringLiteral("Full extent"), controls);

    controlsLayout->addWidget(new QLabel(QStringLiteral("Renderer"), controls), 0, 0);
    controlsLayout->addWidget(rendererCombo, 0, 1);
    controlsLayout->addWidget(new QLabel(QStringLiteral("Field"), controls), 0, 2);
    controlsLayout->addWidget(fieldCombo, 0, 3);
    controlsLayout->addWidget(new QLabel(QStringLiteral("Method"), controls), 0, 4);
    controlsLayout->addWidget(methodCombo, 0, 5);
    controlsLayout->addWidget(classCountLabel, 0, 6);
    controlsLayout->addWidget(classCountSpin, 0, 7);
    controlsLayout->addWidget(intervalLabel, 1, 0);
    controlsLayout->addWidget(intervalSpin, 1, 1);
    controlsLayout->addWidget(manualBreaksLabel, 1, 2);
    controlsLayout->addWidget(manualBreaksEdit, 1, 3, 1, 2);
    controlsLayout->addWidget(new QLabel(QStringLiteral("Render by"), controls), 1, 5);
    controlsLayout->addWidget(renderByCombo, 1, 6);
    controlsLayout->addWidget(new QLabel(QStringLiteral("Ramp"), controls), 2, 0);
    controlsLayout->addWidget(rampCombo, 2, 1);
    controlsLayout->addWidget(new QLabel(QStringLiteral("Ramp mode"), controls), 2, 2);
    controlsLayout->addWidget(rampModeCombo, 2, 3);
    controlsLayout->addWidget(reverseCheck, 2, 4);
    controlsLayout->addWidget(applyButton, 2, 5);
    controlsLayout->addWidget(clearButton, 2, 6);
    controlsLayout->addWidget(fullExtentButton, 2, 7);
    controlsLayout->setColumnStretch(3, 1);

    auto* viewer = new GisViewer(central);
    viewer->setMapBackgroundColor(QColor(128, 128, 128));

    rootLayout->addWidget(controls);
    rootLayout->addWidget(viewer, 1);
    window.setCentralWidget(central);

    auto* legendList = new QListWidget(&window);
    auto* legendDock = new QDockWidget(QStringLiteral("Legend"), &window);
    legendDock->setWidget(legendList);
    legendDock->setMinimumWidth(240);
    window.addDockWidget(Qt::LeftDockWidgetArea, legendDock);

    GisLayerVector* vectorLayer = nullptr;
    const QString path = sampleDataPath(QString::fromLatin1(AdminAreasFile));
    if (!QFileInfo::exists(path))
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("Classification"),
            QStringLiteral("%1 was not found:\n%2").arg(QString::fromLatin1(AdminAreasFile), path));
        return 1;
    }

    QString errorMessage;
    if (!viewer->addLayerFromPath(path, &errorMessage))
    {
        QMessageBox::critical(
            &window,
            QStringLiteral("Classification"),
            QStringLiteral("%1 could not be loaded:\n%2").arg(QString::fromLatin1(AdminAreasFile), errorMessage));
        return 1;
    }

    vectorLayer = dynamic_cast<GisLayerVector*>(viewer->mapLayerAt(0));
    if (vectorLayer == nullptr)
    {
        QMessageBox::critical(&window, QStringLiteral("Classification"), QStringLiteral("Loaded layer is not a vector layer."));
        return 1;
    }

    vectorLayer->setName(QStringLiteral("Andalucia admin areas"));
    vectorLayer->style().setFillColor(QStringLiteral("#DCE8E4"));
    vectorLayer->style().setFillOpacity(220);
    vectorLayer->style().setLineColor(QStringLiteral("#536B68"));
    vectorLayer->style().setLineWidth(0.8f);

    auto applyClassification = [&]
    {
        if (vectorLayer == nullptr)
            return;

        const QString fieldName = fieldCombo->currentText().trimmed();
        if (fieldName.isEmpty())
        {
            QMessageBox::information(&window, QStringLiteral("Classification"), QStringLiteral("Select a field first."));
            return;
        }

        const GisColorRamp ramp = GisColorRampRegistry::ramp(rampCombo->currentText());
        const GisLayerStyle baseStyle = classificationBaseStyle(*vectorLayer);
        std::unique_ptr<GisSymbolRenderer> renderer;

        if (currentRendererKind(*rendererCombo) == RendererKind::Categorized)
        {
            renderer = GisSymbolRendererFactory::createCategorizedRenderer(
                *vectorLayer,
                fieldName,
                ramp,
                baseStyle,
                classCountSpin->value(),
                reverseCheck->isChecked(),
                GisSymbolRendererFactory::DefaultProviderBackedSampleRowLimit,
                currentStyleTarget(*renderByCombo));
        }
        else
        {
            bool manualOk = true;
            const QVector<double> manualBreaks =
                currentMethod(*methodCombo) == GisClassificationMethod::Manual
                    ? parseManualBreaks(manualBreaksEdit->text(), manualOk)
                    : QVector<double>();
            if (!manualOk || (currentMethod(*methodCombo) == GisClassificationMethod::Manual && manualBreaks.size() < 2))
            {
                QMessageBox::information(
                    &window,
                    QStringLiteral("Classification"),
                    QStringLiteral("Manual mode needs at least two numeric break values."));
                return;
            }

            renderer = GisSymbolRendererFactory::createGraduatedRenderer(
                *vectorLayer,
                fieldName,
                currentMethod(*methodCombo),
                classCountSpin->value(),
                ramp,
                baseStyle,
                intervalSpin->value(),
                manualBreaks,
                currentRampMode(*rampModeCombo),
                reverseCheck->isChecked(),
                GisSymbolRendererFactory::DefaultProviderBackedSampleRowLimit,
                currentStyleTarget(*renderByCombo));
        }

        if (!renderer)
        {
            QMessageBox::information(
                &window,
                QStringLiteral("Classification"),
                QStringLiteral("Renderer could not be created for field '%1'.").arg(fieldName));
            return;
        }

        vectorLayer->setSymbolRenderer(std::move(renderer));
        updateLegend(*legendList, vectorLayer);
        viewer->invalidateRenderCache(true, true);
        viewer->update();
        window.statusBar()->showMessage(QStringLiteral("Classification applied on %1").arg(fieldName));
    };

    QObject::connect(rendererCombo, qOverload<int>(&QComboBox::currentIndexChanged), &window, [&]
    {
        populateFields(*fieldCombo, vectorLayer, currentRendererKind(*rendererCombo));
        syncControls(*rendererCombo, *methodCombo, *classCountLabel, *classCountSpin, *intervalLabel, *intervalSpin, *manualBreaksLabel, *manualBreaksEdit, *rampModeCombo);

        const QString preferredRamp = currentRendererKind(*rendererCombo) == RendererKind::Categorized
            ? QStringLiteral("Unique")
            : QStringLiteral("GreenBlue");
        const int rampIndex = rampCombo->findText(preferredRamp, Qt::MatchFixedString);
        if (rampIndex >= 0)
            rampCombo->setCurrentIndex(rampIndex);
    });

    QObject::connect(methodCombo, qOverload<int>(&QComboBox::currentIndexChanged), &window, [&]
    {
        if ((currentMethod(*methodCombo) == GisClassificationMethod::StandardDeviation ||
             currentMethod(*methodCombo) == GisClassificationMethod::StandardDeviationWithCentral) &&
            intervalSpin->value() > 10.0)
        {
            intervalSpin->setValue(1.0);
        }

        syncControls(*rendererCombo, *methodCombo, *classCountLabel, *classCountSpin, *intervalLabel, *intervalSpin, *manualBreaksLabel, *manualBreaksEdit, *rampModeCombo);
    });

    QObject::connect(applyButton, &QPushButton::clicked, &window, applyClassification);
    QObject::connect(clearButton, &QPushButton::clicked, &window, [&]
    {
        if (vectorLayer == nullptr)
            return;

        vectorLayer->clearSymbolRenderer();
        legendList->clear();
        viewer->invalidateRenderCache(true, true);
        viewer->update();
        window.statusBar()->showMessage(QStringLiteral("Renderer cleared"));
    });
    QObject::connect(fullExtentButton, &QPushButton::clicked, viewer, &GisViewer::fullExtent);

    populateFields(*fieldCombo, vectorLayer, currentRendererKind(*rendererCombo));
    syncControls(*rendererCombo, *methodCombo, *classCountLabel, *classCountSpin, *intervalLabel, *intervalSpin, *manualBreaksLabel, *manualBreaksEdit, *rampModeCombo);
    applyClassification();
    
    window.show();
    viewer->fullExtent();

    return app.exec();
}
