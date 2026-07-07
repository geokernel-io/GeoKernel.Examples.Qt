#include <QApplication>
#include <QColor>
#include <QColorDialog>
#include <QCoreApplication>
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

struct StyleValues
{
    QColor fillColor = QColor(QStringLiteral("#F1D58A"));
    QColor lineColor = QColor(QStringLiteral("#266D8F"));
    double lineWidth = 2.0;
    double pointSize = 10.0;
};

GisShapePolygon samplePolygon()
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

GisShapePolyline samplePolyline()
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

GisExtent initialViewExtent()
{
    return GisExtent(-19.5, -14.2, 20.5, 18.9);
}

void setColorButtonSwatch(QPushButton& button, const QColor& color)
{
    button.setStyleSheet(QStringLiteral("QPushButton { background:%1; border:1px solid #7f8c8d; min-height:24px; }")
        .arg(color.name()));
}

void applyStyle(GisViewer& viewer, GisLayerVector* polygonLayer, GisLayerVector* lineLayer, GisLayerVector* pointLayer, const StyleValues& values)
{
    if (polygonLayer != nullptr)
    {
        polygonLayer->style().setFillColor(values.fillColor.name());
        polygonLayer->style().setFillOpacity(185);
        polygonLayer->style().setLineColor(values.lineColor.name());
        polygonLayer->style().setLineWidth(static_cast<float>(values.lineWidth));
    }

    if (lineLayer != nullptr)
    {
        lineLayer->style().setLineColor(values.lineColor.name());
        lineLayer->style().setLineWidth(static_cast<float>(values.lineWidth));
    }

    if (pointLayer != nullptr)
    {
        pointLayer->style().setPointColor(QStringLiteral("#D95F35"));
        pointLayer->style().setPointSize(static_cast<float>(values.pointSize));
    }

    viewer.refreshLayers();
}

QDoubleSpinBox* createSpinBox(QWidget* parent, double minValue, double maxValue, double value)
{
    auto* spinBox = new QDoubleSpinBox(parent);
    spinBox->setRange(minValue, maxValue);
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
    window.setWindowTitle(QStringLiteral("SimpleStyle"));

    auto* centralWidget = new QWidget(&window);
    auto* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* sidePanel = new QWidget(centralWidget);
    sidePanel->setFixedWidth(230);
    auto* sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(10, 10, 10, 10);
    sideLayout->setSpacing(8);

    auto* fillColorButton = new QPushButton(sidePanel);
    auto* lineColorButton = new QPushButton(sidePanel);
    auto* lineWidthSpin = createSpinBox(sidePanel, 0.5, 12.0, 2.0);
    auto* pointSizeSpin = createSpinBox(sidePanel, 2.0, 32.0, 10.0);
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

    auto* viewer = new GisViewer(centralWidget);
    viewer->setMapBackgroundColor(QColor(247, 248, 250));
    viewer->setActiveTool(GisViewerTool::Pan);

    mainLayout->addWidget(sidePanel);
    mainLayout->addWidget(viewer, 1);
    window.setCentralWidget(centralWidget);

    auto polygonLayer = std::make_unique<GisLayerVector>();
    polygonLayer->setName(QStringLiteral("Styled Polygon"));
    polygonLayer->addPolygon(samplePolygon());
    polygonLayer->open();
    GisLayerVector* polygonLayerPtr = polygonLayer.get();
    viewer->addLayer(polygonLayer);

    auto lineLayer = std::make_unique<GisLayerVector>();
    lineLayer->setName(QStringLiteral("Styled Polyline"));
    lineLayer->addPolyline(samplePolyline());
    lineLayer->open();
    GisLayerVector* lineLayerPtr = lineLayer.get();
    viewer->addLayer(lineLayer);

    auto pointLayer = std::make_unique<GisLayerVector>();
    pointLayer->setName(QStringLiteral("Styled Points"));
    pointLayer->addPoint(GisShapePoint(-6.0, 9.0));
    pointLayer->addPoint(GisShapePoint(0.0, 8.0));
    pointLayer->addPoint(GisShapePoint(7.0, 7.0));
    pointLayer->open();
    GisLayerVector* pointLayerPtr = pointLayer.get();
    viewer->addLayer(pointLayer);

    StyleValues values;
    setColorButtonSwatch(*fillColorButton, values.fillColor);
    setColorButtonSwatch(*lineColorButton, values.lineColor);
    applyStyle(*viewer, polygonLayerPtr, lineLayerPtr, pointLayerPtr, values);

    QObject::connect(fillColorButton, &QPushButton::clicked, viewer, [&]
    {
        const QColor color = QColorDialog::getColor(values.fillColor, &window, QStringLiteral("Fill Color"));
        if (!color.isValid())
            return;

        values.fillColor = color;
        setColorButtonSwatch(*fillColorButton, values.fillColor);
        applyStyle(*viewer, polygonLayerPtr, lineLayerPtr, pointLayerPtr, values);
    });

    QObject::connect(lineColorButton, &QPushButton::clicked, viewer, [&]
    {
        const QColor color = QColorDialog::getColor(values.lineColor, &window, QStringLiteral("Line Color"));
        if (!color.isValid())
            return;

        values.lineColor = color;
        setColorButtonSwatch(*lineColorButton, values.lineColor);
        applyStyle(*viewer, polygonLayerPtr, lineLayerPtr, pointLayerPtr, values);
    });

    QObject::connect(lineWidthSpin, &QDoubleSpinBox::valueChanged, viewer, [&](double value)
    {
        values.lineWidth = value;
        applyStyle(*viewer, polygonLayerPtr, lineLayerPtr, pointLayerPtr, values);
    });

    QObject::connect(pointSizeSpin, &QDoubleSpinBox::valueChanged, viewer, [&](double value)
    {
        values.pointSize = value;
        applyStyle(*viewer, polygonLayerPtr, lineLayerPtr, pointLayerPtr, values);
    });

    QObject::connect(resetButton, &QPushButton::clicked, viewer, [&]
    {
        values = StyleValues {};
        lineWidthSpin->setValue(values.lineWidth);
        pointSizeSpin->setValue(values.pointSize);
        setColorButtonSwatch(*fillColorButton, values.fillColor);
        setColorButtonSwatch(*lineColorButton, values.lineColor);
        applyStyle(*viewer, polygonLayerPtr, lineLayerPtr, pointLayerPtr, values);
    });
    
    window.show();
    viewer->setViewExtent(initialViewExtent());

    return app.exec();
}
