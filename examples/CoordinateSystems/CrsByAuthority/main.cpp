#include <QApplication>
#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>

#include "CoordinateSystems/CoordinateSystemFactory.h"

#define GEOKERNEL_SAMPLE_ICONS_ONLY
#include "Helpers.h"
#undef GEOKERNEL_SAMPLE_ICONS_ONLY

using namespace GeoKernel::Core::CoordinateSystems;

QString detailsFor(const CoordinateSystem& crs, const QString& authorityCode)
{
    return QStringLiteral(
        "CoordinateSystemFactory::fromUserInput(\"%1\")\n\n"
        "EPSG code: %2\nName: %3\nKind: %4\nMeters per unit: %5\n\nDefinition\n%6")
        .arg(authorityCode)
        .arg(crs.epsgCode())
        .arg(crs.name())
        .arg(crs.isGeographic() ? QStringLiteral("Geographic") : QStringLiteral("Projected"))
        .arg(crs.metersPerUnit().has_value() ? QString::number(*crs.metersPerUnit(), 'g', 12) : QStringLiteral("n/a"))
        .arg(crs.definition());
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());

    QMainWindow window;
    window.resize(1040, 720);
    window.setWindowTitle(QStringLiteral("CRS by Authority (GDAL/PROJ)"));

    auto* central = new QWidget(&window);
    auto* layout = new QVBoxLayout(central);
    auto* controls = new QHBoxLayout;
    auto* authority = new QComboBox(central);
    authority->setEditable(true);
    authority->addItems({ QStringLiteral("EPSG"), QStringLiteral("ESRI"), QStringLiteral("IGNF") });
    auto* code = new QSpinBox(central);
    code->setRange(1, 999999);
    code->setValue(32635);
    auto* lookup = new QPushButton(QStringLiteral("Resolve"), central);
    auto* summary = new QLineEdit(central);
    summary->setReadOnly(true);
    controls->addWidget(new QLabel(QStringLiteral("Authority:"), central));
    controls->addWidget(authority);
    controls->addWidget(new QLabel(QStringLiteral("Code:"), central));
    controls->addWidget(code);
    controls->addWidget(lookup);
    controls->addWidget(summary, 1);
    layout->addLayout(controls);
    auto* details = new QPlainTextEdit(central);
    details->setReadOnly(true);
    details->setFont(QFont(QStringLiteral("Consolas"), 10));
    layout->addWidget(details, 1);
    window.setCentralWidget(central);

    auto resolve = [&]
    {
        const QString authorityCode = QStringLiteral("%1:%2")
            .arg(authority->currentText().trimmed().toUpper())
            .arg(code->value());
        try
        {
            const auto crs = CoordinateSystemFactory::fromUserInput(authorityCode);
            summary->setText(QStringLiteral("%1 — %2").arg(authorityCode, crs->name()));
            details->setPlainText(detailsFor(*crs, authorityCode));
            window.statusBar()->showMessage(QStringLiteral("Resolved with GDAL/PROJ."), 3000);
        }
        catch (const std::exception& ex)
        {
            summary->setText(QStringLiteral("Lookup failed"));
            details->setPlainText(QString::fromUtf8(ex.what()));
        }
    };
    QObject::connect(lookup, &QPushButton::clicked, &window, resolve);
    QObject::connect(code, &QSpinBox::editingFinished, &window, resolve);
    resolve();
    window.show();
    return app.exec();
}
