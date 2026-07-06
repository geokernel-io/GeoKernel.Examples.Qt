#include <QApplication>
#include <QComboBox>
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

QString recordDetails(const CrsDatabaseRecord& record, const QString& databasePath, const QString& authority, int authoritySrid)
{
    QStringList lines;
    lines << QStringLiteral("CrsDatabase::findByAuthority(\"%1\", %2)").arg(authority).arg(authoritySrid);
    lines << QString();
    lines << QStringLiteral("Database");
    lines << databasePath;
    lines << QString();
    lines << QStringLiteral("Record");
    lines << QStringLiteral("Internal SRID: %1").arg(record.srid());
    lines << QStringLiteral("Authority: %1").arg(record.authName());
    lines << QStringLiteral("Authority SRID: %1").arg(record.authSrid());
    lines << QString();
    lines << QStringLiteral("Usage");
    lines << QStringLiteral("CrsDatabase database(CrsDatabaseOptions(databasePath));");
    lines << QStringLiteral("auto record = database.findByAuthority(QStringLiteral(\"%1\"), %2);").arg(authority).arg(authoritySrid);
    lines << QString();
    lines << QStringLiteral("WKT / srtext");
    lines << previewText(record.srText());
    lines << QString();
    lines << QStringLiteral("PROJ.4 / proj4text");
    lines << (record.proj4Text().trimmed().isEmpty() ? QStringLiteral("(empty)") : record.proj4Text().trimmed());

    return lines.join(QStringLiteral("\n"));
}

void lookupAuthority(
    const CrsDatabase& database,
    const QString& authority,
    int authoritySrid,
    const QString& databasePath,
    QLineEdit& summary,
    QPlainTextEdit& details,
    QStatusBar& statusBar)
{
    try
    {
        const auto record = database.findByAuthority(authority, authoritySrid);
        if (!record.has_value())
        {
            summary.setText(QStringLiteral("%1:%2 not found").arg(authority).arg(authoritySrid));
            details.setPlainText(QStringLiteral("No CRS record found for authority %1:%2.").arg(authority).arg(authoritySrid));
            statusBar.showMessage(QStringLiteral("No CRS record found."), 3000);
            return;
        }

        summary.setText(record->toString());
        details.setPlainText(recordDetails(*record, databasePath, authority, authoritySrid));
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
            QStringLiteral("CrsByAuthority"),
            QStringLiteral("CRS database could not be found:\n%1").arg(databasePath));
        return 1;
    }

    CrsDatabase database{ CrsDatabaseOptions(databasePath) };

    QMainWindow window;
    window.resize(1040, 720);
    window.setWindowTitle(QStringLiteral("CrsByAuthority"));

    auto* central = new QWidget(&window);
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(8);
    window.setCentralWidget(central);

    auto* controls = new QGroupBox(QStringLiteral("Authority code lookup"), central);
    auto* controlsLayout = new QHBoxLayout(controls);
    auto* authorityLabel = new QLabel(QStringLiteral("Authority:"), controls);
    auto* authorityCombo = new QComboBox(controls);
    authorityCombo->setEditable(true);
    authorityCombo->addItems({ QStringLiteral("EPSG"), QStringLiteral("ESRI"), QStringLiteral("IGNF") });
    authorityCombo->setCurrentText(QStringLiteral("EPSG"));
    authorityCombo->setMinimumWidth(120);

    auto* codeLabel = new QLabel(QStringLiteral("Code:"), controls);
    auto* authoritySridSpin = new QSpinBox(controls);
    authoritySridSpin->setRange(1, 999999);
    authoritySridSpin->setValue(32635);
    authoritySridSpin->setSingleStep(1);
    authoritySridSpin->setMinimumWidth(110);

    auto* lookupButton = new QPushButton(QStringLiteral("Find by Authority"), controls);
    auto* summary = new QLineEdit(controls);
    summary->setReadOnly(true);
    summary->setPlaceholderText(QStringLiteral("Lookup result"));

    controlsLayout->addWidget(authorityLabel);
    controlsLayout->addWidget(authorityCombo);
    controlsLayout->addWidget(codeLabel);
    controlsLayout->addWidget(authoritySridSpin);
    controlsLayout->addWidget(lookupButton);
    controlsLayout->addWidget(summary, 1);
    rootLayout->addWidget(controls);

    auto* details = new QPlainTextEdit(central);
    details->setReadOnly(true);
    details->setLineWrapMode(QPlainTextEdit::NoWrap);
    details->setFont(QFont(QStringLiteral("Consolas"), 10));
    rootLayout->addWidget(details, 1);

    auto doLookup = [&database, databasePath, authorityCombo, authoritySridSpin, summary, details, &window]
    {
        lookupAuthority(
            database,
            authorityCombo->currentText().trimmed().toUpper(),
            authoritySridSpin->value(),
            databasePath,
            *summary,
            *details,
            *window.statusBar());
    };

    QObject::connect(lookupButton, &QPushButton::clicked, &window, doLookup);
    QObject::connect(authorityCombo, &QComboBox::currentTextChanged, &window, doLookup);
    QObject::connect(authoritySridSpin, &QSpinBox::editingFinished, &window, doLookup);

    window.statusBar()->showMessage(QStringLiteral("CrsDatabase::findByAuthority(\"EPSG\", 32635)"));
    lookupAuthority(database, QStringLiteral("EPSG"), 32635, databasePath, *summary, *details, *window.statusBar());

    window.show();
    return app.exec();
}
