#ifndef GEOKERNEL_XYZ_PRESETS_SAMPLE_SUPPORT_H
#define GEOKERNEL_XYZ_PRESETS_SAMPLE_SUPPORT_H

#include "Viewer/GisViewer.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QStandardPaths>
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
    request.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("GeoKernel LayerLoadCancel"));
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
    QObject::connect(reply, &QNetworkReply::downloadProgress,
        [&](qint64 received, qint64 total)
    {
        if (total > 0)
        {
            progress.setRange(0, 100);
            progress.setValue(static_cast<int>((received * 100) / total));
        }
    });
    QObject::connect(&progress, &QProgressDialog::canceled,
        reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished,
        &loop, &QEventLoop::quit);
    loop.exec();

    partialFile.write(reply->readAll());
    partialFile.close();
    const bool succeeded = reply->error() == QNetworkReply::NoError;
    const QString error = reply->errorString();
    reply->deleteLater();

    if (!succeeded)
    {
        QFile::remove(partialPath);
        QMessageBox::critical(parent, QStringLiteral("LayerLoadCancel"),
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

inline QString ensureSampleFile(
    const QUrl& url,
    const QString& archiveName,
    const QString& folderName,
    const QString& requiredFileName,
    QWidget* parent)
{
    const QString dataRoot = sampleDataPath();
    const QString targetDir = QDir(dataRoot).absoluteFilePath(folderName);
    const QString existing = findFile(targetDir, requiredFileName);
    if (!existing.isEmpty())
        return existing;

    QDir().mkpath(targetDir);
    const QString archivePath = QDir(dataRoot).absoluteFilePath(archiveName);
    if (!QFileInfo::exists(archivePath) && !downloadFile(url, archivePath, parent))
        return {};

#ifdef Q_OS_WIN
    const QString extractorProgram =
        QStandardPaths::findExecutable(QStringLiteral("tar"));
    const QStringList extractorArguments = {
        QStringLiteral("-xf"), QDir::toNativeSeparators(archivePath)
    };
#else
    const QString extractorProgram =
        QStandardPaths::findExecutable(QStringLiteral("unzip"));
    const QStringList extractorArguments = {
        QStringLiteral("-o"), archivePath, QStringLiteral("-d"), targetDir
    };
#endif

    if (extractorProgram.isEmpty())
    {
        QMessageBox::critical(parent, QStringLiteral("LayerLoadCancel"),
            QStringLiteral("Sample ZIP extractor was not found."));
        return {};
    }

    QProcess extractor;
    extractor.setWorkingDirectory(targetDir);
    extractor.start(extractorProgram, extractorArguments);
    const bool finished = extractor.waitForFinished(120000);
    if (!finished || extractor.exitStatus() != QProcess::NormalExit
        || extractor.exitCode() != 0)
    {
        const QString details =
            QString::fromLocal8Bit(extractor.readAllStandardError()).trimmed();
        QMessageBox::critical(parent, QStringLiteral("LayerLoadCancel"),
            details.isEmpty()
                ? QStringLiteral("Sample ZIP could not be extracted.")
                : QStringLiteral("Sample ZIP could not be extracted:\n%1").arg(details));
        return {};
    }

    QFile::remove(archivePath);
    return findFile(targetDir, requiredFileName);
}

inline bool loadLayer(GeoKernel::Viewer::GisViewer& viewer,
    const QString& path, QWidget* parent)
{
    QString error;
    if (viewer.addLayerFromPath(path, &error))
        return true;

    QMessageBox::critical(parent, QStringLiteral("LayerLoadCancel"),
        QStringLiteral("Layer could not be loaded:\n%1").arg(error));
    return false;
}

#endif
