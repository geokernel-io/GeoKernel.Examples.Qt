#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSize>
#include <QSpinBox>
#include <QStatusBar>
#include <QString>
#include <QTextEdit>
#include <QVBoxLayout>

#include "CoordinateSystems/Database/CrsDatabase.h"
#include "CoordinateSystems/Database/CrsDatabaseOptions.h"
#include "CoordinateSystems/Database/CrsDatabaseRecord.h"

using namespace GeoKernel::Core::CoordinateSystems::Database;

QIcon sampleIcon(const QString& fileName)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString path = QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/images/%1").arg(fileName)));

    QIcon icon;
    for (const auto mode : { QIcon::Normal, QIcon::Active, QIcon::Selected, QIcon::Disabled })
    {
        icon.addFile(path, QSize(), mode, QIcon::Off);
        icon.addFile(path, QSize(), mode, QIcon::On);
    }

    return icon;
}

QString assetPath(const QString& relativePath)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/%1").arg(relativePath)));
}

QString previewText(const QString& text, int maxCharacters = 2200)
{
    const QString trimmed = text.trimmed();
    if (trimmed.size() <= maxCharacters)
        return trimmed;

    return trimmed.left(maxCharacters) + QStringLiteral("\n...");
}

QString recordDetails(const CrsDatabaseRecord& record, const QString& databasePath)
{
    QStringList lines;
    lines << QStringLiteral("CrsDatabase::findBySrid(%1)").arg(record.srid());
    lines << QString();
    lines << QStringLiteral("Database");
    lines << databasePath;
    lines << QString();
    lines << QStringLiteral("Record");
    lines << QStringLiteral("SRID: %1").arg(record.srid());
    lines << QStringLiteral("Authority: %1").arg(record.authName());
    lines << QStringLiteral("Authority SRID: %1").arg(record.authSrid());
    lines << QString();
    lines << QStringLiteral("Usage");
    lines << QStringLiteral("CrsDatabase database(CrsDatabaseOptions(databasePath));");
    lines << QStringLiteral("auto record = database.findBySrid(%1);").arg(record.srid());
    lines << QString();
    lines << QStringLiteral("WKT / srtext");
    lines << previewText(record.srText());
    lines << QString();
    lines << QStringLiteral("PROJ.4 / proj4text");
    lines << (record.proj4Text().trimmed().isEmpty() ? QStringLiteral("(empty)") : record.proj4Text().trimmed());

    return lines.join(QStringLiteral("\n"));
}

void lookupSrid(const CrsDatabase& database, int srid, const QString& databasePath, QLineEdit& summary, QPlainTextEdit& details, QStatusBar& statusBar)
{
    try
    {
        const auto record = database.findBySrid(srid);
        if (!record.has_value())
        {
            summary.setText(QStringLiteral("EPSG:%1 not found").arg(srid));
            details.setPlainText(QStringLiteral("No CRS record found for SRID %1.").arg(srid));
            statusBar.showMessage(QStringLiteral("No CRS record found."), 3000);
            return;
        }

        summary.setText(record->toString());
        details.setPlainText(recordDetails(*record, databasePath));
        statusBar.showMessage(QStringLiteral("Loaded CRS record %1:%2").arg(record->authName()).arg(record->authSrid()), 3000);
    }
    catch (const std::exception& ex)
    {
        summary.setText(QStringLiteral("Lookup failed"));
        details.setPlainText(QString::fromUtf8(ex.what()));
        statusBar.showMessage(QStringLiteral("CRS lookup failed."), 3000);
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon(QStringLiteral("GeoKernelAppIcon.ico")));

    const QString databasePath = assetPath(QStringLiteral("spatial_ref_sys.sqlite"));
    if (!QFileInfo::exists(databasePath))
    {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("CrsDatabase"),
            QStringLiteral("CRS database could not be found:\n%1").arg(databasePath));
        return 1;
    }

    CrsDatabase database{ CrsDatabaseOptions(databasePath) };

    QMainWindow window;
    window.resize(1000, 720);
    window.setWindowTitle(QStringLiteral("CrsDatabase"));

    auto* central = new QWidget(&window);
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(8);
    window.setCentralWidget(central);

    auto* controls = new QGroupBox(QStringLiteral("EPSG code lookup"), central);
    auto* controlsLayout = new QHBoxLayout(controls);
    auto* sridLabel = new QLabel(QStringLiteral("SRID:"), controls);
    auto* sridSpin = new QSpinBox(controls);
    sridSpin->setRange(1, 999999);
    sridSpin->setValue(4326);
    sridSpin->setSingleStep(1);
    sridSpin->setMinimumWidth(110);
    auto* lookupButton = new QPushButton(QStringLiteral("Find by SRID"), controls);
    auto* summary = new QLineEdit(controls);
    summary->setReadOnly(true);
    summary->setPlaceholderText(QStringLiteral("Lookup result"));

    controlsLayout->addWidget(sridLabel);
    controlsLayout->addWidget(sridSpin);
    controlsLayout->addWidget(lookupButton);
    controlsLayout->addWidget(summary, 1);
    rootLayout->addWidget(controls);

    auto* details = new QPlainTextEdit(central);
    details->setReadOnly(true);
    details->setLineWrapMode(QPlainTextEdit::NoWrap);
    details->setFont(QFont(QStringLiteral("Consolas"), 10));
    rootLayout->addWidget(details, 1);

    auto doLookup = [&database, databasePath, sridSpin, summary, details, &window]
    {
        lookupSrid(database, sridSpin->value(), databasePath, *summary, *details, *window.statusBar());
    };

    QObject::connect(lookupButton, &QPushButton::clicked, &window, doLookup);
    QObject::connect(sridSpin, &QSpinBox::editingFinished, &window, doLookup);

    window.statusBar()->showMessage(QStringLiteral("CrsDatabase::findBySrid(4326)"));
    lookupSrid(database, 4326, databasePath, *summary, *details, *window.statusBar());

    window.show();
    return app.exec();
}
