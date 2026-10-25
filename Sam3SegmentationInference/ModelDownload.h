#pragma once
#include <QtCore>
#include <QtNetwork>
#include <functional>

// Run in a worker thread. No GUI or credentials are used here.
namespace Sam3Download
{
using Cancel = std::function<bool()>;
using Progress = std::function<void(int, const QString &)>;
inline void require(bool ok, const QString &why)
{
    if (!ok)
        throw std::runtime_error(why.toStdString());
}
inline void check(const Cancel &cancel)
{
    require(!cancel || !cancel(), "Download cancelled.");
}
inline QByteArray hash(const QString &path, const Cancel &cancel = {})
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash digest(QCryptographicHash::Sha256);
    while (!file.atEnd())
    {
        check(cancel);
        auto bytes = file.read(4 * 1024 * 1024);
        require(!bytes.isEmpty(), "Cannot read cached file.");
        digest.addData(bytes);
    }
    return digest.result().toHex();
}
inline bool valid(const QString &path, const QJsonObject &item, const Cancel &cancel)
{
    return QFileInfo(path).isFile() && QFileInfo(path).size() == item["bytes"].toInteger() &&
           hash(path, cancel) == item["sha256"].toString().toLatin1();
}
inline QString safePath(const QString &name)
{
    require(!name.isEmpty() && !QDir::isAbsolutePath(name) && !name.contains('\\') && !name.contains(':') &&
                QDir::cleanPath(name) == name && !name.startsWith("../") && name != "..",
            "Unsafe package path.");
    return name;
}
inline void download(QUrl url, const QString &destination, const QJsonObject &asset, const Cancel &cancel,
                     const Progress &progress)
{
    if (valid(destination, asset, cancel))
        return;
    QSaveFile output(destination);
    require(output.open(QIODevice::WriteOnly), "Cannot create download file.");
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setTransferTimeout(120000);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    request.setHeader(QNetworkRequest::UserAgentHeader, "GeoKernel-SAM3-Example");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto reply = manager.get(request);
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(50);
    qint64 received = 0;
    int lastPercent = -1;
    bool failed = false;
    QCryptographicHash digest(QCryptographicHash::Sha256);
    const qint64 expected = asset["bytes"].toInteger();
    auto drain = [&]
    {
        auto bytes = reply->readAll();
        received += bytes.size();
        if (received > expected || output.write(bytes) != bytes.size())
        {
            failed = true;
            reply->abort();
        }
        digest.addData(bytes);
        const int percent = expected ? int(std::min<qint64>(99, received * 100 / expected)) : 0;
        if (progress && percent != lastPercent)
        {
            lastPercent = percent;
            progress(percent, "Downloading " + asset["name"].toString());
        }
    };
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, drain);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&poll, &QTimer::timeout, &loop,
                     [&]
                     {
                         if (cancel && cancel())
                             reply->abort();
                     });
    poll.start();
    loop.exec();
    drain();
    poll.stop();
    const auto error = reply->error();
    const auto detail = reply->errorString();
    reply->deleteLater();
    check(cancel);
    require(!failed && error == QNetworkReply::NoError, "Download failed: " + detail);
    require(received == expected && digest.result().toHex() == asset["sha256"].toString().toLatin1(),
            "Download size or SHA-256 mismatch.");
    require(output.commit(), "Cannot commit verified download.");
}
inline QByteArray process(const QString &program, QStringList args, const QString &directory,
                          const Cancel &cancel)
{
    QProcess proc;
    proc.setWorkingDirectory(directory);
    proc.start(program, args);
    require(proc.waitForStarted(5000), "Cannot start ZIP extractor.");
    QElapsedTimer timer;
    timer.start();
    while (!proc.waitForFinished(50))
    {
        if ((cancel && cancel()) || timer.elapsed() > 600000)
        {
            proc.kill();
            proc.waitForFinished(5000);
            check(cancel);
            throw std::runtime_error("ZIP extraction timed out.");
        }
    }
    check(cancel);
    require(proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0,
            "ZIP extraction failed: " + QString::fromLocal8Bit(proc.readAllStandardError()));
    return proc.readAllStandardOutput();
}
inline QString ensure(const QJsonObject &catalog, const QString &kind, const QString &root,
                      const Cancel &cancel = {}, const Progress &progress = {}, QUrl base = {})
{
    require(catalog["schema"] == "geokernel.sam3-download/1", "Unknown download catalog.");
    const auto profile = catalog["profiles"].toObject()[kind].toObject();
    require(!profile.isEmpty(), "Unknown SAM3 profile.");
    const auto manifest = safePath(profile["manifest"].toString());
    const auto revision = safePath(catalog["revision"].toString());
    const auto destination = QDir(root).filePath(revision + "/" + safePath(kind));
    auto verify = [&](const QString &folder)
    {
        for (const auto &value : profile["files"].toArray())
        {
            auto file = value.toObject();
            if (!valid(QDir(folder).filePath(safePath(file["path"].toString())), file, cancel))
                return false;
        }
        return true;
    };
    if (verify(destination))
        return QDir(destination).filePath(manifest);
    require(!QFileInfo::exists(destination),
            "Cached model is incomplete or corrupt. Remove this profile folder and retry: " + destination);
    require(QDir().mkpath(QFileInfo(destination).absolutePath()), "Cannot create model cache.");
    QLockFile lock(destination + ".lock");
    require(lock.tryLock(0), "Another process is preparing this SAM3 profile.");
    check(cancel);
    if (verify(destination))
        return QDir(destination).filePath(manifest);
    auto archives = QDir(root).filePath("downloads");
    require(QDir().mkpath(archives), "Cannot create archive cache.");
    qint64 needed = 0;
    for (auto v : profile["files"].toArray())
        needed += v.toObject()["bytes"].toInteger();
    for (auto v : profile["assets"].toArray())
    {
        auto a = v.toObject();
        if (!QFileInfo::exists(QDir(archives).filePath(a["name"].toString())))
            needed += a["bytes"].toInteger();
    }
    QStorageInfo disk(QFileInfo(destination).absolutePath());
    require(!disk.isValid() || disk.bytesAvailable() > needed + 256ll * 1024 * 1024,
            "Insufficient disk space for model and archives.");
    QTemporaryDir stage(QFileInfo(destination).absolutePath() + "/.sam3-install-XXXXXX");
    require(stage.isValid(), "Cannot create installation staging folder.");
    QString tar = QStandardPaths::findExecutable("tar");
    require(!tar.isEmpty(), "ZIP extractor 'tar' was not found.");
    if (base.isEmpty())
        base = profile.contains("base_url")
                   ? QUrl(profile["base_url"].toString())
                   : QUrl("https://github.com/" + catalog["repository"].toString() + "/releases/download/" +
                          catalog["release"].toString() + "/");
    for (auto v : profile["assets"].toArray())
    {
        check(cancel);
        auto asset = v.toObject();
        auto name = safePath(asset["name"].toString());
        QString archive = QDir(archives).absoluteFilePath(name);
        download(base.resolved(QUrl(name)), archive, asset, cancel, progress);
        if (progress)
            progress(0, "Extracting " + name);
        auto listed = QString::fromUtf8(process(tar, {"-tf", archive}, stage.path(), cancel))
                          .split('\n', Qt::SkipEmptyParts);
        QSet<QString> actual, expected;
        for (auto item : listed)
            actual.insert(safePath(item.trimmed()));
        for (auto item : asset["files"].toArray())
            expected.insert(safePath(item.toString()));
        require(actual == expected, "ZIP entries differ from the pinned catalog.");
        process(tar, {"-xf", archive}, stage.path(), cancel);
    }
    if (progress)
        progress(0, "Verifying installed model files…");
    require(verify(stage.path()), "Extracted model failed integrity verification.");
    check(cancel);
    require(QDir().rename(stage.path(), destination), "Cannot publish verified model cache.");
    stage.setAutoRemove(false);
    // The verified installed files are the cache; release ZIPs need not remain duplicated.
    for (auto value : profile["assets"].toArray())
        QFile::remove(QDir(archives).filePath(safePath(value.toObject()["name"].toString())));
    if (progress)
        progress(100, "Model ready");
    return QDir(destination).filePath(manifest);
}
} // namespace Sam3Download
