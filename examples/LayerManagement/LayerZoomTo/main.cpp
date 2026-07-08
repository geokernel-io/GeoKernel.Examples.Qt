#include "Helpers.h"
#include "Viewer/GisViewer.h"
#include "Layers/GisLayer.h"

using namespace GeoKernel::Viewer;
using namespace GeoKernel::Core::Layers;

struct CityLayer
{
    QString name;
    QString path;
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

QVector<CityLayer> prepareCityLayerList(QWidget* parent)
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

    const QString firstCityPath = ensureSampleFile(
        QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/california_cities.zip")),
        QStringLiteral("california_cities.zip"),
        QStringLiteral("california_cities"),
        QStringLiteral("alameda.shp"),
        parent);
    if (firstCityPath.isEmpty())
        return {};

    const QDir cityDir(QFileInfo(firstCityPath).absolutePath());
    const QFileInfoList files = cityDir.entryInfoList(
        QStringList { QStringLiteral("*.shp") },
        QDir::Files,
        QDir::Name);

    QVector<CityLayer> layers;
    layers.reserve(files.size());

    for (int i = 0; i < files.size(); ++i)
    {
        const QFileInfo& file = files[i];
        layers.append(CityLayer {
            displayNameFromFileName(file.fileName()),
            file.absoluteFilePath(),
            palette[i % palette.size()]
        });
    }

    return layers;
}

bool addLayer(GisViewer& viewer, const CityLayer& city)
{
    QString errorMessage;
    if (!viewer.addLayerFromPath(city.path, &errorMessage))
    {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("LayerZoomTo"),
            QStringLiteral("Layer could not be loaded:\n%1")
            .arg(errorMessage.isEmpty() ? city.path : errorMessage));
        return false;
    }

    if (GisLayer* layer = viewer.mapLayerAt(0))
    {
        layer->setName(city.name);
        layer->style().setFillColor(city.fillColor);
        layer->style().setFillOpacity(150);
        layer->style().setLineColor(QStringLiteral("#5F7772"));
        layer->style().setLineWidth(0.8f);
        layer->style().setShowLabels(true);
        layer->style().setLabelFontSize(12.0f);
        layer->style().setLabelAllowOverlap(true);
        layer->style().setLabelAvoidObstacles(false);
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
    auto layer = viewer.mapLayerByName(name);
    return viewer.mapLayerIndex(layer);
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("Layer ZoomTo"));

    auto* centralWidget = new QWidget(&window);
    auto* layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* topPanel = new QWidget(centralWidget);
    auto* topLayout = new QHBoxLayout(topPanel);
    topLayout->setContentsMargins(6, 4, 6, 4);
    topLayout->setSpacing(6);

    auto* comboBox = new QComboBox(topPanel);
    comboBox->setMinimumWidth(220);
    comboBox->addItem(QStringLiteral("-"));
    topLayout->addWidget(comboBox);
    topLayout->addStretch(1);

    auto* viewer = new GisViewer(centralWidget);
    viewer->setActiveTool(GisViewerTool::Pan);

    layout->addWidget(topPanel);
    layout->addWidget(viewer, 1);
    window.setCentralWidget(centralWidget);

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

    window.show();

    QMetaObject::invokeMethod(&window, [&window, viewer, comboBox]
    {
        const QVector<CityLayer> cities = prepareCityLayerList(&window);
        if (cities.isEmpty())
            return;

        for (const CityLayer& city : cities)
            comboBox->addItem(city.name);

        for (const CityLayer& city : cities)
        {
            if (!addLayer(*viewer, city))
                return;
        }

        viewer->refreshLayers();
        viewer->fullExtent();
    });

    return app.exec();
}
