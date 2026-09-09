#ifndef GEOKERNEL_SELECT_CLEAR_SAMPLE_SUPPORT_H
#define GEOKERNEL_SELECT_CLEAR_SAMPLE_SUPPORT_H

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
#include <QThread>
#include <QUrl>

inline QString sampleDataPath()
{
    return QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("data"));
}

inline QString sampleApplicationName()
{
    const QString name = QCoreApplication::applicationName();
    return name.isEmpty() ? QStringLiteral("GeoKernel Sample") : name;
}

inline bool downloadFile(const QUrl& url, const QString& targetPath, QWidget* parent)
{
    QDir().mkpath(QFileInfo(targetPath).absolutePath());
    const QString partialPath = targetPath + QStringLiteral(".part");
    QProgressDialog progress(QStringLiteral("Downloading sample data..."),
        QStringLiteral("Cancel"), 0, 0, parent);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);

    QString lastError;
    for (int attempt = 1; attempt <= 3; ++attempt)
    {
        QFile::remove(partialPath);
        QFile partialFile(partialPath);
        if (!partialFile.open(QIODevice::WriteOnly))
            return false;

        progress.setLabelText(attempt == 1
            ? QStringLiteral("Downloading sample data...")
            : QStringLiteral("Downloading sample data... (attempt %1 of 3)").arg(attempt));

        QNetworkAccessManager manager;
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
            QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader,
            QStringLiteral("GeoKernel %1").arg(sampleApplicationName()));
        QNetworkReply* reply = manager.get(request);

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
        const bool canceled = reply->error() == QNetworkReply::OperationCanceledError;
        const int httpStatus = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        lastError = reply->errorString();
        reply->deleteLater();

        if (succeeded)
        {
            QFile::remove(targetPath);
            return QFile::rename(partialPath, targetPath);
        }

        QFile::remove(partialPath);
        if (canceled)
            return false;

        const bool retryable = httpStatus >= 500
            || reply->error() == QNetworkReply::TemporaryNetworkFailureError
            || reply->error() == QNetworkReply::TimeoutError
            || reply->error() == QNetworkReply::RemoteHostClosedError;
        if (!retryable || attempt == 3)
            break;

        QThread::msleep(static_cast<unsigned long>(attempt * 1000));
    }

    QMessageBox::critical(parent, sampleApplicationName(),
        QStringLiteral("Sample data download failed after 3 attempts:\n%1").arg(lastError));
    return false;
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
        QMessageBox::critical(parent, sampleApplicationName(),
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
        QMessageBox::critical(parent, sampleApplicationName(),
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

    QMessageBox::critical(parent, sampleApplicationName(),
        QStringLiteral("Layer could not be loaded:\n%1").arg(error));
    return false;
}

#endif
