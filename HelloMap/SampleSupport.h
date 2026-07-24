#ifndef _SAMPLESUPPORT_H_
#define _SAMPLESUPPORT_H_

#include "Viewer/GisViewer.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMainWindow>
#include <QSize>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QToolBar>
#include <QUrl>

inline QString sampleDataPath()
{
    return QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("data"));
}

inline bool downloadFile(const QUrl& url, const QString& targetPath, QWidget* parent)
{
    QDir().mkpath(QFileInfo(targetPath).absolutePath());
    const QString partialPath = targetPath + QStringLiteral(".part");
    QFile partialFile(partialPath);
    if (!partialFile.open(QIODevice::WriteOnly))
        return false;

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GeoKernel HelloMap"));
    QNetworkReply* reply = manager.get(request);

    QProgressDialog progress(QStringLiteral("Downloading sample data..."),
        QStringLiteral("Cancel"), 0, 0, parent);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::readyRead, [&]
    {
        partialFile.write(reply->readAll());
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, [&](qint64 received, qint64 total)
    {
        if (total > 0)
        {
            progress.setRange(0, 100);
            progress.setValue(static_cast<int>((received * 100) / total));
        }
    });
    QObject::connect(&progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    partialFile.write(reply->readAll());
    partialFile.close();
    const bool succeeded = reply->error() == QNetworkReply::NoError;
    const QString error = reply->errorString();
    reply->deleteLater();

    if (!succeeded)
    {
        QFile::remove(partialPath);
        QMessageBox::critical(parent, QStringLiteral("HelloMap"),
            QStringLiteral("Sample data download failed:\n%1").arg(error));
        return false;
    }

    QFile::remove(targetPath);
    return QFile::rename(partialPath, targetPath);
}

inline QString findFile(const QString& folder, const QString& fileName)
{
    QDirIterator iterator(folder, QStringList() << fileName, QDir::Files,
        QDirIterator::Subdirectories);
    return iterator.hasNext() ? iterator.next() : QString();
}

inline QString ensureWorldLayer(QWidget* parent)
{
    const QString dataRoot = sampleDataPath();
    const QString targetDir = QDir(dataRoot).absoluteFilePath(QStringLiteral("world_4326"));
    const QString existing = findFile(targetDir, QStringLiteral("world_4326.shp"));
    if (!existing.isEmpty())
        return existing;

    QDir().mkpath(targetDir);
    const QString zipPath = QDir(dataRoot).absoluteFilePath(QStringLiteral("world_4326.zip"));
    if (!QFileInfo::exists(zipPath) &&
        !downloadFile(QUrl(QStringLiteral("https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/world_4326.zip")), zipPath, parent))
        return {};

    QProcess extractor;
    extractor.setWorkingDirectory(targetDir);
#ifdef Q_OS_WIN
    const QString extractorProgram = QStandardPaths::findExecutable(QStringLiteral("tar"));
    const QStringList extractorArguments = {
        QStringLiteral("-xf"),
        QDir::toNativeSeparators(zipPath)
    };
#else
    const QString extractorProgram = QStandardPaths::findExecutable(QStringLiteral("unzip"));
    const QStringList extractorArguments = {
        QStringLiteral("-o"),
        zipPath,
        QStringLiteral("-d"),
        targetDir
    };
#endif
    if (extractorProgram.isEmpty())
    {
        QMessageBox::critical(parent, QStringLiteral("HelloMap"),
#ifdef Q_OS_WIN
            QStringLiteral("Sample ZIP could not be extracted because the tar command was not found."));
#else
            QStringLiteral("Sample ZIP could not be extracted because unzip is not installed.\n"
                           "Install it with: sudo apt install unzip"));
#endif
        return {};
    }

    extractor.start(extractorProgram, extractorArguments);
    const bool finished = extractor.waitForFinished(120000);
    if (!finished || extractor.exitStatus() != QProcess::NormalExit || extractor.exitCode() != 0)
    {
        const QString details = QString::fromLocal8Bit(extractor.readAllStandardError()).trimmed();
        QMessageBox::critical(parent, QStringLiteral("HelloMap"),
            details.isEmpty()
                ? QStringLiteral("Sample ZIP could not be extracted.")
                : QStringLiteral("Sample ZIP could not be extracted:\n%1").arg(details));
        return {};
    }
    QFile::remove(zipPath);
    return findFile(targetDir, QStringLiteral("world_4326.shp"));
}

inline bool loadLayer(GeoKernel::Viewer::GisViewer& viewer, const QString& path, QWidget* parent)
{
    QString error;
    if (viewer.addLayerFromPath(path, &error))
        return true;
    QMessageBox::critical(parent, QStringLiteral("HelloMap"),
        QStringLiteral("Layer could not be loaded:\n%1").arg(error));
    return false;
}

inline QToolBar* createNavigationToolbar(QMainWindow& window, GeoKernel::Viewer::GisViewer& viewer)
{
    auto* toolbar = window.addToolBar(QStringLiteral("Navigation"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(24, 24));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    QAction* zoomIn = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/zoom-in.svg")),
        QStringLiteral("Zoom In"));
    QAction* zoomOut = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/zoom-out.svg")),
        QStringLiteral("Zoom Out"));
    QAction* fullExtent = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/full-extent.svg")),
        QStringLiteral("Full Extent"));
    toolbar->addSeparator();
    QAction* zoomBox = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/zoom-box.svg")),
        QStringLiteral("Zoom Box"));
    QAction* pan = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/pan.svg")),
        QStringLiteral("Pan"));

    auto* tools = new QActionGroup(toolbar);
    tools->setExclusive(true);
    zoomBox->setCheckable(true);
    pan->setCheckable(true);
    tools->addAction(zoomBox);
    tools->addAction(pan);
    pan->setChecked(true);

    QObject::connect(zoomIn, &QAction::triggered, &viewer, [&viewer] { viewer.zoomIn(); });
    QObject::connect(zoomOut, &QAction::triggered, &viewer, [&viewer] { viewer.zoomOut(); });
    QObject::connect(fullExtent, &QAction::triggered, &viewer, &GeoKernel::Viewer::GisViewer::fullExtent);
    QObject::connect(zoomBox, &QAction::triggered, &viewer, [&viewer]
    {
        viewer.setActiveTool(GeoKernel::Viewer::GisViewerTool::ZoomBox);
    });
    QObject::connect(pan, &QAction::triggered, &viewer, [&viewer]
    {
        viewer.setActiveTool(GeoKernel::Viewer::GisViewerTool::Pan);
    });
    return toolbar;
}

#endif
