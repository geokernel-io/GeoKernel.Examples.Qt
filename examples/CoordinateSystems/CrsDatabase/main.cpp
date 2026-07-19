#include <QApplication>
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

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(sampleIcon());
    QMainWindow window;
    window.resize(1000, 720);
    window.setWindowTitle(QStringLiteral("EPSG lookup (GDAL/PROJ)"));
    auto* central = new QWidget(&window);
    auto* layout = new QVBoxLayout(central);
    auto* controls = new QHBoxLayout;
    auto* code = new QSpinBox(central);
    code->setRange(1, 999999);
    code->setValue(4326);
    auto* lookup = new QPushButton(QStringLiteral("Find EPSG"), central);
    auto* summary = new QLineEdit(central);
    summary->setReadOnly(true);
    controls->addWidget(new QLabel(QStringLiteral("EPSG:"), central));
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
        try
        {
            const auto crs = CoordinateSystemFactory::fromEpsg(code->value());
            summary->setText(QStringLiteral("EPSG:%1 — %2").arg(crs->epsgCode()).arg(crs->name()));
            details->setPlainText(QStringLiteral(
                "CoordinateSystemFactory::fromEpsg(%1)\n\nName: %2\nKind: %3\nMeters per unit: %4\n\nDefinition\n%5")
                .arg(crs->epsgCode()).arg(crs->name())
                .arg(crs->isGeographic() ? QStringLiteral("Geographic") : QStringLiteral("Projected"))
                .arg(crs->metersPerUnit().has_value() ? QString::number(*crs->metersPerUnit(), 'g', 12) : QStringLiteral("n/a"))
                .arg(crs->definition()));
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
