#include <QApplication>
#include <QColor>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

#include "Viewer/GisViewer.h"
#include "Shapes/GisExtent.h"
#include "Layers/GisLayerVector.h"

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;
using namespace GeoKernel::Core::Shapes;

struct SelectionStyleValues
{
    QColor selectedLineColor = QColor(QStringLiteral("#F59E0B"));
    double selectedLineWidth = 4.0;
};

GisShapePolygon polygonA()
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

GisShapePolygon polygonB()
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

GisShapePolyline sampleLine()
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

void setColorButtonSwatch(QPushButton& button, const QColor& color)
{
    button.setStyleSheet(QStringLiteral("QPushButton { background:%1; border:1px solid #7f8c8d; min-height:24px; }")
        .arg(color.name()));
}

void applySelectionStyle(
    GisViewer& viewer,
    GisLayerVector* polygonLayer,
    GisLayerVector* lineLayer,
    GisLayerVector* pointLayer,
    const SelectionStyleValues& values)
{
    const QString selectedColor = values.selectedLineColor.name();
    const auto selectedWidth = static_cast<float>(values.selectedLineWidth);

    for (GisLayerVector* layer : { polygonLayer, lineLayer, pointLayer })
    {
        if (layer == nullptr)
            continue;

        layer->style().setSelectedLineColor(selectedColor);
        layer->style().setSelectedLineWidth(selectedWidth);
    }

    viewer.refreshLayers();
}

QDoubleSpinBox* createWidthSpinBox(QWidget* parent, double value)
{
    auto* spinBox = new QDoubleSpinBox(parent);
    spinBox->setRange(1.0, 16.0);
    spinBox->setDecimals(1);
    spinBox->setSingleStep(0.5);
    spinBox->setValue(value);
    return spinBox;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1100, 720);
    window.setWindowTitle(QStringLiteral("SelectionStyle"));

    auto* centralWidget = new QWidget(&window);
    auto* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* sidePanel = new QWidget(centralWidget);
    sidePanel->setFixedWidth(230);
    auto* sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(10, 10, 10, 10);
    sideLayout->setSpacing(8);

    auto* selectedColorButton = new QPushButton(sidePanel);
    auto* selectedWidthSpin = createWidthSpinBox(sidePanel, 4.0);
    auto* clearSelectionButton = new QPushButton(QStringLiteral("Clear Selection"), sidePanel);
    auto* resetButton = new QPushButton(QStringLiteral("Reset Style"), sidePanel);

    sideLayout->addWidget(new QLabel(QStringLiteral("Selected Line Color"), sidePanel));
    sideLayout->addWidget(selectedColorButton);
    sideLayout->addWidget(new QLabel(QStringLiteral("Selected Line Width"), sidePanel));
    sideLayout->addWidget(selectedWidthSpin);
    sideLayout->addWidget(clearSelectionButton);
    sideLayout->addWidget(resetButton);
    sideLayout->addStretch(1);

    auto* viewer = new GisViewer(centralWidget);
    viewer->setMapBackgroundColor(QColor(247, 248, 250));
    viewer->setActiveTool(GisViewerTool::Select);

    mainLayout->addWidget(sidePanel);
    mainLayout->addWidget(viewer, 1);
    window.setCentralWidget(centralWidget);

    auto polygonLayer = std::make_unique<GisLayerVector>();
    polygonLayer->setName(QStringLiteral("Selectable Polygons"));
    polygonLayer->style().setFillColor(QStringLiteral("#F1D58A"));
    polygonLayer->style().setFillOpacity(180);
    polygonLayer->style().setLineColor(QStringLiteral("#266D8F"));
    polygonLayer->style().setLineWidth(1.8f);
    polygonLayer->addPolygon(polygonA());
    polygonLayer->addPolygon(polygonB());
    polygonLayer->open();
    GisLayerVector* polygonLayerPtr = polygonLayer.get();
    viewer->addLayer(polygonLayer);

    auto lineLayer = std::make_unique<GisLayerVector>();
    lineLayer->setName(QStringLiteral("Selectable Polyline"));
    lineLayer->style().setLineColor(QStringLiteral("#266D8F"));
    lineLayer->style().setLineWidth(2.2f);
    lineLayer->addPolyline(sampleLine());
    lineLayer->open();
    GisLayerVector* lineLayerPtr = lineLayer.get();
    viewer->addLayer(lineLayer);

    auto pointLayer = std::make_unique<GisLayerVector>();
    pointLayer->setName(QStringLiteral("Selectable Points"));
    pointLayer->style().setPointColor(QStringLiteral("#D95F35"));
    pointLayer->style().setPointSize(10.0f);
    pointLayer->addPoint(GisShapePoint(-8.0, 8.0));
    pointLayer->addPoint(GisShapePoint(0.0, 7.0));
    pointLayer->addPoint(GisShapePoint(8.0, 8.0));
    pointLayer->open();
    GisLayerVector* pointLayerPtr = pointLayer.get();
    viewer->addLayer(pointLayer);

    SelectionStyleValues values;
    setColorButtonSwatch(*selectedColorButton, values.selectedLineColor);
    applySelectionStyle(*viewer, polygonLayerPtr, lineLayerPtr, pointLayerPtr, values);

    QObject::connect(selectedColorButton, &QPushButton::clicked, viewer, [&]
    {
        const QColor color = QColorDialog::getColor(values.selectedLineColor, &window, QStringLiteral("Selected Line Color"));
        if (!color.isValid())
            return;

        values.selectedLineColor = color;
        setColorButtonSwatch(*selectedColorButton, values.selectedLineColor);
        applySelectionStyle(*viewer, polygonLayerPtr, lineLayerPtr, pointLayerPtr, values);
    });

    QObject::connect(selectedWidthSpin, &QDoubleSpinBox::valueChanged, viewer, [&](double value)
    {
        values.selectedLineWidth = value;
        applySelectionStyle(*viewer, polygonLayerPtr, lineLayerPtr, pointLayerPtr, values);
    });

    QObject::connect(clearSelectionButton, &QPushButton::clicked, viewer, &GisViewer::clearSelectedFeatures);

    QObject::connect(resetButton, &QPushButton::clicked, viewer, [&]
    {
        values = SelectionStyleValues {};
        selectedWidthSpin->setValue(values.selectedLineWidth);
        setColorButtonSwatch(*selectedColorButton, values.selectedLineColor);
        applySelectionStyle(*viewer, polygonLayerPtr, lineLayerPtr, pointLayerPtr, values);
    });
        
    window.show();
    viewer->setViewExtent(GisExtent(-15.0, -9.0, 15.0, 11.0));

    return app.exec();
}
