#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QSize>
#include <QString>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QMainWindow>
#include <QMessageBox>
#include <QToolBar>
#include <QLabel>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPointer>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QComboBox>
#include <QFileInfo>
#include <QFileInfoList>
#include <QFile>
#include <QDirIterator>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTextEdit>
#include <QTime>

#include "Viewer/GisViewer.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <array>

inline QString sampleAssetPath(const QString& relativePath)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/%1").arg(relativePath)));
}

inline QString sampleImagePath(const QString& relativePath)
{
    return sampleAssetPath(QStringLiteral("images/%1").arg(relativePath));
}

inline QString sampleDataPath(const QString& relativePath)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("data/%1").arg(relativePath)));
}

inline bool downloadFile(const QUrl& url, const QString& targetPath, QWidget* parent = nullptr)
{
    QDir().mkpath(QFileInfo(targetPath).absolutePath());

    const QString partialPath = targetPath + QStringLiteral(".part");
    QFile partialFile(partialPath);
    if (!partialFile.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(parent, QStringLiteral("GeoKernel Sample Data"),
            QStringLiteral("Could not create download file:\n%1").arg(partialPath));
        return false;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GeoKernel Qt Examples"));

    QNetworkReply* reply = manager.get(request);
    QProgressDialog progress(
        QStringLiteral("Preparing sample data...\nDownloading %1").arg(QFileInfo(targetPath).fileName()),
        QStringLiteral("Cancel"),
        0,
        0,
        parent);
    progress.setWindowTitle(QStringLiteral("GeoKernel Sample Data"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.resize(560, 160);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::readyRead, &progress, [&]
    {
        partialFile.write(reply->readAll());
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, &progress, [&](qint64 received, qint64 total)
    {
        if (total > 0)
        {
            progress.setRange(0, static_cast<int>(total));
            progress.setValue(static_cast<int>(received));
            progress.setLabelText(QStringLiteral("Downloading sample data...\n%1 MB / %2 MB (%3%)")
                .arg(received / 1024.0 / 1024.0, 0, 'f', 1)
                .arg(total / 1024.0 / 1024.0, 0, 'f', 1)
                .arg(static_cast<int>((received * 100) / total)));
        }
        else
        {
            progress.setRange(0, 0);
            progress.setLabelText(QStringLiteral("Downloading sample data...\n%1 MB downloaded")
                .arg(received / 1024.0 / 1024.0, 0, 'f', 1));
        }
    });
    QObject::connect(&progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    loop.exec();
    partialFile.write(reply->readAll());
    partialFile.close();

    const bool ok = reply->error() == QNetworkReply::NoError;
    const QString errorText = reply->errorString();
    reply->deleteLater();

    if (!ok)
    {
        QFile::remove(partialPath);
        QMessageBox::critical(parent, QStringLiteral("GeoKernel Sample Data"),
            QStringLiteral("Sample data could not be downloaded.\n\n%1").arg(errorText));
        return false;
    }

    QFile::remove(targetPath);
    if (!QFile::rename(partialPath, targetPath))
    {
        QFile::remove(partialPath);
        QMessageBox::critical(parent, QStringLiteral("GeoKernel Sample Data"),
            QStringLiteral("Could not finalize download:\n%1").arg(targetPath));
        return false;
    }

    return true;
}

inline bool extractZip(const QString& zipPath, const QString& targetDir, QWidget* parent = nullptr)
{
    QDir().mkpath(targetDir);

    QProgressDialog progress(
        QStringLiteral("Installing sample data...\nExtracting %1").arg(QFileInfo(zipPath).fileName()),
        QStringLiteral("Cancel"),
        0,
        0,
        parent);
    progress.setWindowTitle(QStringLiteral("GeoKernel Sample Data"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.resize(560, 160);
    progress.show();
    QApplication::processEvents();

    QProcess process;
    process.setWorkingDirectory(targetDir);
    process.start(QStringLiteral("tar"), QStringList() << QStringLiteral("-xf") << zipPath);

    while (!process.waitForFinished(50))
    {
        QApplication::processEvents();
        if (progress.wasCanceled())
        {
            process.kill();
            process.waitForFinished();
            QMessageBox::critical(parent, QStringLiteral("GeoKernel Sample Data"),
                QStringLiteral("Sample data extraction was cancelled."));
            return false;
        }
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        const QString errorText = QString::fromLocal8Bit(process.readAllStandardError());
        QMessageBox::critical(parent, QStringLiteral("GeoKernel Sample Data"),
            QStringLiteral("Sample data could not be extracted.\n\n%1")
                .arg(errorText.isEmpty() ? QStringLiteral("tar extraction failed.") : errorText));
        return false;
    }

    progress.setValue(1);
    return true;
}

inline QString findRequiredSampleFile(const QString& targetDir, const QString& requiredFileName)
{
    const QString expectedPath = QDir(targetDir).absoluteFilePath(requiredFileName);
    if (QFileInfo::exists(expectedPath))
        return expectedPath;

    QDirIterator iterator(targetDir, QStringList() << requiredFileName, QDir::Files, QDirIterator::Subdirectories);
    if (iterator.hasNext())
        return iterator.next();

    return QString();
}

inline QString ensureSampleFile(
    const QUrl& zipUrl,
    const QString& zipName,
    const QString& targetFolder,
    const QString& requiredFileName,
    QWidget* parent = nullptr)
{
    const QString dataDir = sampleDataPath(QString());
    QDir().mkpath(dataDir);

    const QString targetDir = QDir(dataDir).absoluteFilePath(targetFolder);
    const QString existingPath = findRequiredSampleFile(targetDir, requiredFileName);
    if (!existingPath.isEmpty())
        return existingPath;

    const QString zipPath = QDir(dataDir).absoluteFilePath(zipName);
    if (!QFileInfo::exists(zipPath) && !downloadFile(zipUrl, zipPath, parent))
        return QString();

    if (!extractZip(zipPath, targetDir, parent))
        return QString();

    QFile::remove(zipPath);

    const QString extractedPath = findRequiredSampleFile(targetDir, requiredFileName);
    if (!extractedPath.isEmpty())
        return extractedPath;

    QMessageBox::critical(parent, QStringLiteral("GeoKernel Sample Data"),
        QStringLiteral("Downloaded sample data does not contain:\n%1")
            .arg(QDir(targetDir).absoluteFilePath(requiredFileName)));
    return QString();
}

inline QIcon sampleIcon(const QString& fileName)
{
    QIcon icon;
    QStringList candidates;
    candidates << fileName;

    const QFileInfo requestedFile(fileName);
    const QString suffix = requestedFile.suffix();
    if (suffix.compare(QStringLiteral("svg"), Qt::CaseInsensitive) == 0)
        candidates << QStringLiteral("%1.png").arg(requestedFile.completeBaseName());
    else if (suffix.compare(QStringLiteral("png"), Qt::CaseInsensitive) == 0)
        candidates << QStringLiteral("%1.svg").arg(requestedFile.completeBaseName());

    for (const QString& candidate : candidates)
    {
        const QString path = sampleImagePath(candidate);
        if (!QFileInfo::exists(path))
            continue;

        for (const auto mode : { QIcon::Normal, QIcon::Active, QIcon::Selected, QIcon::Disabled })
        {
            icon.addFile(path, QSize(), mode, QIcon::Off);
            icon.addFile(path, QSize(), mode, QIcon::On);
        }

        if (!icon.isNull())
            break;
    }

    return icon;
}

inline bool loadLayer(GeoKernel::Viewer::GisViewer& viewer, const QString& path, QWidget* parent = nullptr)
{
    QString errorMessage;
    if (viewer.addLayerFromPath(path, &errorMessage))
        return true;

    QMessageBox::critical(
        parent,
        QStringLiteral("Layer Load"),
        QStringLiteral("Layer could not be loaded:\n%1")
            .arg(errorMessage.isEmpty() ? path : errorMessage));
    return false;
}

inline QAction* addToolAction(QToolBar* toolbar, const QString& iconName, const QString& text)
{
    if (toolbar == nullptr)
        return nullptr;

    QAction* action = toolbar->addAction(sampleIcon(iconName), text);
    action->setToolTip(text);
    action->setStatusTip(text);
    return action;
}

inline void addNavigationActions(QToolBar* toolbar, GeoKernel::Viewer::GisViewer& viewer)
{
    if (toolbar == nullptr)
        return;

    QAction* zoomInAction = addToolAction(toolbar, QStringLiteral("ZoomIn.svg"), QStringLiteral("Zoom In"));
    QAction* zoomOutAction = addToolAction(toolbar, QStringLiteral("ZoomOut.svg"), QStringLiteral("Zoom Out"));
    QAction* fullExtentAction = addToolAction(toolbar, QStringLiteral("FullExtent.svg"), QStringLiteral("Full Extent"));
    toolbar->addSeparator();

    auto* toolGroup = new QActionGroup(toolbar);
    toolGroup->setExclusive(true);

    QAction* zoomRectAction = addToolAction(toolbar, QStringLiteral("RectangularZoom.svg"), QStringLiteral("Zoom Rect"));
    zoomRectAction->setCheckable(true);
    toolGroup->addAction(zoomRectAction);

    QAction* panAction = addToolAction(toolbar, QStringLiteral("Pan.svg"), QStringLiteral("Pan"));
    panAction->setCheckable(true);
    toolGroup->addAction(panAction);

    QObject::connect(zoomInAction, &QAction::triggered, &viewer, [&viewer]
    {
        viewer.zoomIn();
    });

    QObject::connect(zoomOutAction, &QAction::triggered, &viewer, [&viewer]
    {
        viewer.zoomOut();
    });

    QObject::connect(fullExtentAction, &QAction::triggered, &viewer, &GeoKernel::Viewer::GisViewer::fullExtent);

    QObject::connect(zoomRectAction, &QAction::triggered, &viewer, [&viewer]
    {
        viewer.setActiveTool(GeoKernel::Viewer::GisViewerTool::ZoomBox);
    });

    QObject::connect(panAction, &QAction::triggered, &viewer, [&viewer]
    {
        viewer.setActiveTool(GeoKernel::Viewer::GisViewerTool::Pan);
    });

    panAction->setChecked(true);
    viewer.setActiveTool(GeoKernel::Viewer::GisViewerTool::Pan);
}

inline QToolBar* createNavigationToolbar(QMainWindow& window, GeoKernel::Viewer::GisViewer& viewer)
{
    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    window.addToolBar(toolbar);
    addNavigationActions(toolbar, viewer);
    return toolbar;
}
