#ifndef _SAMPLESUPPORT_H_
#define _SAMPLESUPPORT_H_

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

inline void sampleError(QWidget *parent, const QString &, const QString &message)
{
    QMessageBox::critical(parent, "TerrainAndImagery", message);
}

inline QString sampleDataPath()
{
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("data"));
}

inline bool downloadFile(const QUrl &url, const QString &targetPath, QWidget *parent)
{
    QDir().mkpath(QFileInfo(targetPath).absolutePath());
    const QString partialPath = targetPath + QStringLiteral(".part");
    QFile partialFile(partialPath);
    if (!partialFile.open(QIODevice::WriteOnly))
    {
        sampleError(parent, "TerrainAndImagery",
                    "Cannot write the sample data folder next to the application.");
        return false;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("GeoKernel TerrainAndImagery"));
    request.setTransferTimeout(60000);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    QNetworkReply *reply = manager.get(request);
    bool writeFailed = false;
    QTimer idleTimer;
    idleTimer.setSingleShot(true);
    idleTimer.setInterval(60000);
    QObject::connect(&idleTimer, &QTimer::timeout, reply, &QNetworkReply::abort);
    idleTimer.start();

    QProgressDialog progress(QStringLiteral("Downloading sample data..."), QStringLiteral("Cancel"),
                             0, 0, parent);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::readyRead, [&] {
        idleTimer.start();
        const auto bytes = reply->readAll();
        if (partialFile.write(bytes) != bytes.size())
        {
            writeFailed = true;
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, [&](qint64 received, qint64 total) {
        if (total > 0)
        {
            progress.setRange(0, 100);
            progress.setValue(static_cast<int>((received * 100) / total));
        }
    });
    QObject::connect(&progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    idleTimer.stop();

    const auto remaining = reply->readAll();
    if (partialFile.write(remaining) != remaining.size())
        writeFailed = true;
    partialFile.close();
    const bool succeeded = reply->error() == QNetworkReply::NoError && !writeFailed;
    const QString error =
        writeFailed ? "Could not write sample data to disk." : reply->errorString();
    reply->deleteLater();

    if (!succeeded)
    {
        QFile::remove(partialPath);
        sampleError(parent, QStringLiteral("TerrainAndImagery"),
                    QStringLiteral("Sample data download failed:\n%1").arg(error));
        return false;
    }

    QFile::remove(targetPath);
    if (QFile::rename(partialPath, targetPath))
        return true;
    sampleError(parent, "TerrainAndImagery", "Could not save the downloaded sample ZIP.");
    return false;
}

inline QString findFile(const QString &folder, const QString &fileName)
{
    QDirIterator iterator(folder, QStringList() << fileName, QDir::Files,
                          QDirIterator::Subdirectories);
    return iterator.hasNext() ? iterator.next() : QString();
}

inline bool completeSample(const QString &path)
{
    if (path.isEmpty() || QFileInfo(path).size() == 0)
        return false;
    if (QFileInfo(path).suffix().compare("gltf", Qt::CaseInsensitive) == 0)
    {
        const QDir folder(QFileInfo(path).absolutePath());
        for (const auto *name : {"model.bin", "basecolor.png", "license.txt"})
            if (QFileInfo(folder.filePath(name)).size() == 0)
                return false;
        return true;
    }
    if (QFileInfo(path).suffix().compare("shp", Qt::CaseInsensitive) != 0)
        return true;
    const auto base = QFileInfo(path).absolutePath() + "/" + QFileInfo(path).completeBaseName();
    for (const auto *suffix : {".dbf", ".shx", ".prj"})
        if (!QFileInfo(base + suffix).isFile() || QFileInfo(base + suffix).size() == 0)
            return false;
    return true;
}

inline QString ensureSampleFile(const QUrl &url, const QString &archiveName,
                                const QString &folderName, const QString &requiredFileName,
                                QWidget *parent)
{
    const QString dataRoot = sampleDataPath();
    const QString targetDir = QDir(dataRoot).absoluteFilePath(folderName);
    const QString existing = findFile(targetDir, requiredFileName);
    if (completeSample(existing) && QFileInfo::exists(QDir(targetDir).filePath(".ready")))
    {
        return existing;
    }

    QDir().mkpath(targetDir);
    const QString archivePath = QDir(dataRoot).absoluteFilePath(archiveName);
    if (!QFileInfo::exists(archivePath) && !downloadFile(url, archivePath, parent))
        return {};

#ifdef Q_OS_WIN
    const QString extractorProgram = QStandardPaths::findExecutable(QStringLiteral("tar"));
    const QStringList extractorArguments = {QStringLiteral("-xf"),
                                            QDir::toNativeSeparators(archivePath)};
#else
    const QString extractorProgram = QStandardPaths::findExecutable(QStringLiteral("unzip"));
    const QStringList extractorArguments = {QStringLiteral("-o"), archivePath, QStringLiteral("-d"),
                                            targetDir};
#endif

    if (extractorProgram.isEmpty())
    {
        sampleError(
            parent, QStringLiteral("TerrainAndImagery"),
#ifdef Q_OS_WIN
            QStringLiteral(
                "Sample ZIP could not be extracted because the tar command was not found."));
#else
            QStringLiteral("Sample ZIP could not be extracted because unzip is not installed.\n"
                           "Install it with: sudo apt install unzip"));
#endif
        return {};
    }

    QProcess extractor;
    extractor.setWorkingDirectory(targetDir);
    extractor.start(extractorProgram, extractorArguments);
    const bool finished = extractor.waitForFinished(120000);
    if (!finished || extractor.exitStatus() != QProcess::NormalExit || extractor.exitCode() != 0)
    {
        const QString details = QString::fromLocal8Bit(extractor.readAllStandardError()).trimmed();
        sampleError(parent, QStringLiteral("TerrainAndImagery"),
                    details.isEmpty()
                        ? QStringLiteral("Sample ZIP could not be extracted.")
                        : QStringLiteral("Sample ZIP could not be extracted:\n%1").arg(details));
        return {};
    }

    const auto result = findFile(targetDir, requiredFileName);
    if (!completeSample(result))
    {
        sampleError(parent, "TerrainAndImagery",
                    "The sample ZIP is missing the expected file or its companion files.");
        return {};
    }
    if (!result.isEmpty())
    {
        QFile marker(QDir(targetDir).filePath(".ready"));
        if (!marker.open(QIODevice::WriteOnly))
        {
            sampleError(parent, "TerrainAndImagery", "Cannot save the sample cache marker.");
            return {};
        }
        marker.write("v1");
        marker.close();
        QFile::remove(archivePath);
    }
    return result;
}

#endif
