#pragma once
#include "ViewshedOverlay.h"
#include <GeoKernel.Viewer3D/ViewerController.h>
#include <GeoKernel.Viewer3D/Viewshed.h>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

// A north-up grid of sampled visibility, independent of the 3D camera.
class ViewshedMap : public QWidget
{
  public:
    GeoKernel3D::Viewshed result;
    explicit ViewshedMap(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(320, 260);
    }
    static QColor cellColor(GeoKernel3D::ViewshedCell state)
    {
        using C = GeoKernel3D::ViewshedCell;
        switch (state)
        {
        case C::Clear:
            return QColor("#239b56");
        case C::Blocked:
            return QColor("#d35445");
        case C::Unknown:
            return QColor("#858585");
        case C::Pending:
            return QColor("#dce3eb");
        default:
            return Qt::transparent;
        }
    }

  protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), palette().base());
        p.setPen(palette().text().color());
        if (result.cells.empty())
        {
            p.drawText(rect(), Qt::AlignCenter,
                       "Select an observer on the terrain.\nThen calculate visibility.");
            return;
        }
        const double side = std::min(width() - 50, height() - 55);
        const QRectF box((width() - side) / 2., 25, side, side);
        const double cell = side / result.width;
        p.setPen(Qt::NoPen);
        for (size_t i = 0; i < result.cells.size(); ++i)
        {
            p.setBrush(cellColor(result.cells[i]));
            p.drawRect(QRectF(box.left() + (i % result.width) * cell,
                              box.top() + (i / result.width) * cell, cell, cell));
        }
        p.setPen(palette().text().color());
        p.setBrush(Qt::NoBrush);
        p.drawText(QRectF(box.left(), 0, side, 22), Qt::AlignCenter, "N ↑");
        p.drawText(QRectF(0, box.center().y() - 10, 22, 20), Qt::AlignCenter, "W");
        p.drawText(QRectF(width() - 22, box.center().y() - 10, 22, 20), Qt::AlignCenter, "E");
        p.drawText(QRectF(0, box.bottom() + 5, width(), 24), Qt::AlignCenter,
                   QString("S ↓     Radius: %1 m").arg(result.radius, 0, 'f', 0));
        p.setPen(QPen(Qt::black, 3));
        p.drawLine(box.center() + QPointF(-6, 0), box.center() + QPointF(6, 0));
        p.drawLine(box.center() + QPointF(0, -6), box.center() + QPointF(0, 6));
        p.setPen(QPen(Qt::white, 1));
        p.drawEllipse(box.center(), 4, 4);
    }
};

class ViewshedPanel : public QWidget
{
  public:
    explicit ViewshedPanel(GeoKernel3D::ViewerController& viewer, QWidget* parent = nullptr)
        : QWidget(parent), viewer_(viewer)
    {
        auto* layout = new QVBoxLayout(this);
        auto* help =
            new QLabel("Select observer, then click the terrain. The 3D marker shows the eye "
                       "height. Calculate to fill the north-up visibility grid below.");
        help->setWordWrap(true);
        layout->addWidget(help);
        pick_ = new QCheckBox("Select observer on terrain");
        pick_->setEnabled(false);
        layout->addWidget(pick_);
        location_ = new QLabel("No observer selected.");
        location_->setWordWrap(true);
        layout->addWidget(location_);
        auto* form = new QFormLayout;
        layout->addLayout(form);
        eye_ = spin(form, "Observer above terrain", 0.1, 100, 1.7);
        target_ = spin(form, "Target above terrain", 0, 100, 1.7);
        radius_ = spin(form, "Radius", 10, 2000, 300);
        spacing_ = spin(form, "Ray sample spacing", 1, 50, 5);
        grid_ = new QComboBox;
        grid_->addItems({"17 × 17 — fast", "33 × 33 — balanced", "65 × 65 — detailed"});
        grid_->setCurrentIndex(1);
        form->addRow("Grid", grid_);
        start_ = new QPushButton("Calculate");
        start_->setEnabled(false);
        layout->addWidget(start_);
        pause_ = new QPushButton("Pause calculation");
        pause_->setEnabled(false);
        layout->addWidget(pause_);
        auto* clear = new QPushButton("Clear observer and result");
        layout->addWidget(clear);
        progress_ = new QProgressBar;
        progress_->setRange(0, 100);
        progress_->setValue(0);
        layout->addWidget(progress_);
        map_ = new ViewshedMap;
        layout->addWidget(map_);
        showOverlay_ = new QCheckBox("Show result on terrain");
        showOverlay_->setChecked(true);
        layout->addWidget(showOverlay_);
        overlayStatus_ = new QLabel;
        overlayStatus_->setWordWrap(true);
        layout->addWidget(overlayStatus_);
        connect(showOverlay_, &QCheckBox::toggled, this,
                [this](bool visible) { viewer_.setModel(visible ? overlayModel_ : nullptr); });
        overlayTimer_.setInterval(15);
        connect(&overlayTimer_, &QTimer::timeout, this, [this] {
            if (!overlayJob_ || ref_ != viewer_.reference())
            {
                overlayTimer_.stop();
                return;
            }
            try
            {
                const auto end = std::min(overlayJob_->total, overlayJob_->next + 1024);
                while (overlayJob_->next < end)
                {
                    auto face = viewer_.terrainTriangle(ref_, overlayJob_->next++);
                    if (face)
                        overlayJob_->add(*face);
                }
                overlayStatus_->setText(QString("Preparing terrain overlay: %1%")
                                            .arg(overlayJob_->total
                                                     ? 100 * overlayJob_->next / overlayJob_->total
                                                     : 100));
                if (overlayJob_->next == overlayJob_->total)
                {
                    overlayTimer_.stop();
                    overlayModel_ = overlayJob_->finish();
                    overlayJob_.reset();
                    viewer_.setModel(showOverlay_->isChecked() ? overlayModel_ : nullptr);
                    overlayStatus_->setText("Terrain overlay ready (55% opacity).");
                }
            }
            catch (const std::exception& e)
            {
                overlayTimer_.stop();
                overlayJob_.reset();
                overlayStatus_->setText(QString::fromUtf8(e.what()));
            }
        });
        auto* legend = new QLabel(
            "<span style='color:#239b56'>■ Visible</span> &nbsp; <span style='color:#d35445'>■ "
            "Blocked</span><br><span style='color:#858585'>■ Unknown / NoData</span> &nbsp; <span "
            "style='color:#8795a6'>■ Pending</span>");
        layout->addWidget(legend);
        status_ = new QLabel("Load terrain to begin.");
        status_->setWordWrap(true);
        layout->addWidget(status_);
        auto* note =
            new QLabel("Terrain-only, sampled visibility; buildings and vegetation are excluded. "
                       "The loaded DEM mesh has 512 samples per side. Small obstacles between "
                       "samples can be missed. No atmospheric refraction. Vertical datum is "
                       "unverified. Exaggeration changes only the 3D view.");
        note->setWordWrap(true);
        layout->addWidget(note);
        connect(pick_, &QCheckBox::toggled, this,
                [this](bool on) { viewer_.setTerrainQueryEnabled(on && bool(ref_)); });
        for (auto* s : {eye_, target_, radius_, spacing_})
            connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] {
                invalidate();
                markObserver();
            });
        connect(grid_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this] { invalidate(); });
        connect(start_, &QPushButton::clicked, this, [this] { start(); });
        connect(pause_, &QPushButton::clicked, this, [this] {
            timer_.stop();
            pause_->setEnabled(false);
            start_->setEnabled(true);
            start_->setText("Resume calculation");
            status_->setText("Paused — partial result. Resume or change settings.");
            beginOverlay();
        });
        connect(clear, &QPushButton::clicked, this, [this] {
            observer_.reset();
            invalidate();
            location_->setText("No observer selected.");
            viewer_.clearMeasurementLine();
        });
        timer_.setInterval(15);
        connect(&timer_, &QTimer::timeout, this, [this] {
            if (!ref_ || ref_ != viewer_.reference())
            {
                resetScene();
                return;
            }
            try
            {
                GeoKernel3D::advanceViewshed(
                    result_, *ref_,
                    [this](float x, float z) { return viewer_.terrainRenderHeight(ref_, x, z); },
                    256);
                publish();
                if (result_.complete())
                {
                    timer_.stop();
                    pause_->setEnabled(false);
                    start_->setEnabled(true);
                    start_->setText("Recalculate");
                    beginOverlay();
                }
            }
            catch (const std::exception& e)
            {
                invalidate();
                status_->setText(QString::fromUtf8(e.what()));
            }
        });
        viewer_.addTerrainQueryClickedHandler([this](QPointF screen) {
            const auto expected = ref_;
            QTimer::singleShot(0, this, [this, screen, expected] {
                if (!pick_->isChecked() || !expected || expected != ref_ ||
                    expected != viewer_.reference())
                    return;
                try
                {
                    const auto hit = viewer_.terrainPointAt(screen);
                    if (!hit)
                    {
                        status_->setText("Click inside the loaded terrain.");
                        return;
                    }
                    observer_ =
                        ref_->fromRender({hit->x(), hit->y(), hit->z()}, viewer_.heightScale());
                    pick_->setChecked(false);
                    invalidate();
                    markObserver();
                    const auto geo = ref_->fromEnu(*observer_);
                    location_->setText(QString("Observer: %1° E, %2° N")
                                           .arg(geo.longitude, 0, 'f', 6)
                                           .arg(geo.latitude, 0, 'f', 6));
                }
                catch (const std::exception& e)
                {
                    status_->setText(QString::fromUtf8(e.what()));
                }
            });
        });
    }
    void resetScene()
    {
        timer_.stop();
        ref_ = viewer_.reference();
        observer_.reset();
        invalidate();
        location_->setText("No observer selected.");
        pick_->setEnabled(bool(ref_));
        pick_->setChecked(bool(ref_));
        viewer_.setTerrainQueryEnabled(bool(ref_));
        viewer_.clearMeasurementLine();
    }

  private:
    QDoubleSpinBox* spin(QFormLayout* form, const QString& label, double low, double high,
                         double initial)
    {
        auto* s = new QDoubleSpinBox;
        s->setRange(low, high);
        s->setValue(initial);
        s->setSuffix(" m");
        form->addRow(label, s);
        return s;
    }
    void invalidate()
    {
        clearOverlay();
        timer_.stop();
        result_ = {};
        map_->result = {};
        map_->update();
        progress_->setValue(0);
        pause_->setEnabled(false);
        start_->setText("Calculate");
        start_->setEnabled(bool(ref_) && bool(observer_));
        status_->setText(observer_ ? "Ready — calculate with current settings."
                                   : "Select an observer on the terrain.");
    }
    void markObserver()
    {
        if (!ref_ || !observer_)
            return;
        auto eye = *observer_;
        eye.z += eye_->value();
        if (!viewer_.setMeasurementLine(ref_, {*observer_, eye}))
            status_->setText("Observer marker rejected. Reload terrain.");
    }
    void start()
    {
        if (!ref_ || !observer_ || ref_ != viewer_.reference())
            return;
        try
        {
            if (result_.cells.empty() || result_.complete())
            {
                auto eye = *observer_;
                eye.z += eye_->value();
                const unsigned widths[] = {17, 33, 65};
                result_ =
                    GeoKernel3D::planViewshed(eye, radius_->value(), widths[grid_->currentIndex()],
                                              target_->value(), spacing_->value());
            }
            clearOverlay();
            start_->setEnabled(false);
            pause_->setEnabled(true);
            pick_->setChecked(false);
            publish();
            timer_.start();
        }
        catch (const std::exception& e)
        {
            invalidate();
            status_->setText(QString::fromUtf8(e.what()));
        }
    }
    void clearOverlay()
    {
        overlayTimer_.stop();
        overlayJob_.reset();
        overlayModel_.reset();
        viewer_.setModel(nullptr);
        overlayStatus_->clear();
    }
    void beginOverlay()
    {
        overlayTimer_.stop();
        overlayJob_ =
            std::make_unique<ViewshedOverlay>(result_, ref_, viewer_.terrainTriangleCount(ref_));
        overlayStatus_->setText("Preparing terrain overlay…");
        overlayTimer_.start();
    }
    void publish()
    {
        using C = GeoKernel3D::ViewshedCell;
        map_->result = result_;
        map_->update();
        progress_->setValue(int(100 * result_.next / result_.cells.size()));
        status_->setText(QString("%1\nVisible: %2 | Blocked: %3\nUnknown: %4 | Pending: %5\nGrid "
                                 "spacing: %6 m\nMaximum ray spacing so far: %7 m")
                             .arg(result_.complete() ? "Complete" : "Calculating — partial result")
                             .arg(result_.count(C::Clear))
                             .arg(result_.count(C::Blocked))
                             .arg(result_.count(C::Unknown))
                             .arg(result_.count(C::Pending))
                             .arg(result_.cellSpacing, 0, 'f', 1)
                             .arg(result_.maxRaySpacing, 0, 'f', 1));
    }
    GeoKernel3D::ViewerController& viewer_;
    std::shared_ptr<const GeoKernel3D::SceneReference> ref_;
    std::optional<GeoKernel3D::Vec3d> observer_;
    GeoKernel3D::Viewshed result_;
    QTimer overlayTimer_;
    std::unique_ptr<ViewshedOverlay> overlayJob_;
    std::shared_ptr<const GeoKernel3D::PlacedModel> overlayModel_;
    QCheckBox* showOverlay_;
    QLabel* overlayStatus_;
    QTimer timer_;
    QCheckBox* pick_;
    QLabel *location_, *status_;
    QDoubleSpinBox *eye_, *target_, *radius_, *spacing_;
    QComboBox* grid_;
    QPushButton *start_, *pause_;
    QProgressBar* progress_;
    ViewshedMap* map_;
};
