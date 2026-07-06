#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QStringList>

namespace
{
    QIcon loadGeoKernelSampleAppIcon()
    {
        const QDir appDir(QCoreApplication::applicationDirPath());
        const QStringList candidates = {
            QStringLiteral("GeoKernelAppIcon.ico"),
            QStringLiteral("ico/GeoKernelAppIcon.ico"),
            QStringLiteral("GeoKernelAppIcon.svg"),
            QStringLiteral("svg/GeoKernelAppIcon.svg"),
            QStringLiteral("GeoKernel.svg"),
            QStringLiteral("svg/GeoKernel.svg")
        };

        for (const QString& candidate : candidates)
        {
            const QString path = QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/images/%1").arg(candidate)));
            if (!QFileInfo::exists(path))
                continue;

            const QIcon icon(path);
            if (!icon.isNull())
                return icon;
        }

        return {};
    }

    void setGeoKernelSampleAppIcon()
    {
        const QIcon icon = loadGeoKernelSampleAppIcon();
        if (!icon.isNull())
            QApplication::setWindowIcon(icon);
    }
}

Q_COREAPP_STARTUP_FUNCTION(setGeoKernelSampleAppIcon)
