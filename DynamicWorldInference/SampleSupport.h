#pragma once

#include <QCoreApplication>
#include <QCryptographicHash>
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
#include <QTimer>
#include <QDebug>

inline void sampleError(QWidget* parent, const QString& title, const QString& message) {
    if (QCoreApplication::instance()->property("headless").toBool()) qWarning().noquote() << title << message;
    else QMessageBox::critical(parent, title, message);
}

inline QString sampleDataPath()
{
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("data"));
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
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    request.setTransferTimeout(60000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GeoKernel Project"));
    QNetworkReply* reply = manager.get(request);
    QProgressDialog progress(QStringLiteral("Downloading Dynamic World model / sample..."),
        QStringLiteral("Cancel"), 0, 0, parent);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::readyRead, [&] { partialFile.write(reply->readAll()); });
    QObject::connect(reply, &QNetworkReply::downloadProgress, [&](qint64 received, qint64 total) {
        if (total > 0) {
            progress.setRange(0, 100);
            progress.setValue(static_cast<int>((received * 100) / total));
        }
    });
    QObject::connect(&progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    deadline.start(120000);
    loop.exec();
    deadline.stop();

    partialFile.write(reply->readAll());
    partialFile.close();
    const bool succeeded = reply->error() == QNetworkReply::NoError;
    const QString error = reply->errorString();
    reply->deleteLater();
    if (!succeeded) {
        QFile::remove(partialPath);
        sampleError(parent, QStringLiteral("DynamicWorldInference"),
            QStringLiteral("Sample download failed:\n%1").arg(error));
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

inline QString ensureSampleFile(const QUrl& url, const QString& archiveName,
    const QString& folderName, const QString& requiredFileName, QWidget* parent, const QByteArray& expectedSha256)
{
    const QString dataRoot = sampleDataPath();
    const QString targetDir = QDir(dataRoot).absoluteFilePath(folderName);
    const QString existing = findFile(targetDir, requiredFileName);
    if (!existing.isEmpty()) {
        QFile marker(QDir(targetDir).filePath(".archive-sha256"));
        if (marker.open(QIODevice::ReadOnly) && marker.readAll().trimmed() == expectedSha256) return existing;
    }

    QDir().mkpath(targetDir);
    const QString archivePath = QDir(dataRoot).absoluteFilePath(archiveName);
    if (!QFileInfo::exists(archivePath) && !downloadFile(url, archivePath, parent))
        return {};

    QFile archive(archivePath); QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!archive.open(QIODevice::ReadOnly) || !hash.addData(&archive) || hash.result().toHex() != expectedSha256) {
        archive.close(); QFile::remove(archivePath);
        sampleError(parent, "Download integrity", "ZIP SHA-256 mismatch. Download again."); return {};
    }
    archive.close();
#ifdef Q_OS_WIN
    const QString extractorProgram = QStandardPaths::findExecutable(QStringLiteral("tar"));
    const QStringList extractorArguments = {QStringLiteral("-xf"), QDir::toNativeSeparators(archivePath)};
#else
    const QString extractorProgram = QStandardPaths::findExecutable(QStringLiteral("unzip"));
    const QStringList extractorArguments = {
        QStringLiteral("-o"), archivePath, QStringLiteral("-d"), targetDir};
#endif
    if (extractorProgram.isEmpty()) {
        sampleError(parent, QStringLiteral("DynamicWorldInference"),
            QStringLiteral("Sample ZIP extractor was not found."));
        return {};
    }

    QProcess extractor;
    extractor.setWorkingDirectory(targetDir);
    extractor.start(extractorProgram, extractorArguments);
    QEventLoop extractionLoop;
    QObject::connect(&extractor, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &extractionLoop, &QEventLoop::quit);
    QObject::connect(&extractor, &QProcess::errorOccurred, &extractionLoop, &QEventLoop::quit);
    if (extractor.state() != QProcess::NotRunning) extractionLoop.exec();
    const bool finished = extractor.state() == QProcess::NotRunning;
    if (!finished || extractor.exitStatus() != QProcess::NormalExit || extractor.exitCode() != 0) {
        const QString details = QString::fromLocal8Bit(extractor.readAllStandardError()).trimmed();
        sampleError(parent, QStringLiteral("DynamicWorldInference"),
            details.isEmpty() ? QStringLiteral("Sample ZIP could not be extracted.")
                              : QStringLiteral("Sample ZIP could not be extracted:\n%1").arg(details));
        return {};
    }
    QFile marker(QDir(targetDir).filePath(".archive-sha256"));
    if (marker.open(QIODevice::WriteOnly)) marker.write(expectedSha256);
    QFile::remove(archivePath);
    return findFile(targetDir, requiredFileName);
}
