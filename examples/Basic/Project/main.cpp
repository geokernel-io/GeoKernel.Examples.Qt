#include "Helpers.h"
#include "Viewer/GisViewer.h"

using namespace GeoKernel::Viewer;

void finishProgressAfterRender(GisViewer& viewer, QProgressBar& progressBar, QLabel& progressLabel)
{
    progressBar.setValue(100);
    progressLabel.setText(QStringLiteral("Rendering map..."));

    QObject::connect(&viewer, &GisViewer::afterPaint, &viewer, [&progressBar, &progressLabel]
    {
        progressBar.setValue(100);
        progressLabel.setText(QStringLiteral("Project loaded."));

        QTimer::singleShot(900, &progressBar, [&progressBar, &progressLabel]
        {
            progressBar.setValue(0);
        });
    }, Qt::SingleShotConnection);

    viewer.update();
}

bool loadProject(GisViewer& viewer, QProgressBar& progressBar, QLabel& progressLabel)
{
    progressBar.show();
    progressBar.setValue(0);
    progressLabel.setText(QStringLiteral("Preparing Andalucia sample data..."));

    const QString projectPath = ensureSampleFile(
        QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/andalucia.zip")),
        QStringLiteral("andalucia.zip"),
        QStringLiteral("andalucia"),
        QStringLiteral("andalucia.geokernel"),
        &viewer);

    if (projectPath.isEmpty())
    {
        progressBar.setValue(0);
        progressLabel.setText(QStringLiteral("Project sample data could not be prepared."));
        return false;
    }

    progressLabel.setText(QStringLiteral("Loading andalucia.geokernel..."));

    auto progress = [&progressBar, &progressLabel](int value, const QString& text)
    {
        progressBar.show();
        progressBar.setValue(std::clamp(value, 0, 100));
        progressLabel.setText(text);
        QApplication::processEvents();
    };

    if (!viewer.openProject(projectPath, progress))
    {
        progressBar.setValue(0);
        progressLabel.setText(QStringLiteral("Project could not be loaded."));
        QMessageBox::critical(
            &viewer,
            QStringLiteral("Project"),
            QStringLiteral("Project could not be loaded:\n%1").arg(projectPath));
        return false;
    }

    finishProgressAfterRender(viewer, progressBar, progressLabel);
    return true;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1200, 800);
    window.setWindowTitle(QStringLiteral("Project"));

    auto* centralWidget = new QWidget(&window);
    auto* layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* viewer = new GisViewer(centralWidget);
    viewer->setMapBackgroundColor(QColor(244, 246, 245));
    viewer->setActiveTool(GisViewerTool::Pan);
    layout->addWidget(viewer, 1);

    auto* progressWidget = new QWidget(centralWidget);
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
    layout->addWidget(progressWidget);

    window.setCentralWidget(centralWidget);
    createNavigationToolbar(window, *viewer);

    window.show();

    QMetaObject::invokeMethod(&window, [viewer, progressBar, progressLabel]
    {
        loadProject(*viewer, *progressBar, *progressLabel);
    });

    return app.exec();
}