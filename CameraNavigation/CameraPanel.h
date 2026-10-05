#pragma once
#include <GeoKernel.Viewer3D/ViewerController.h>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QWidget>
#include <cmath>

// Camera positions are SDK render-space coordinates, not longitude/latitude.
// Bookmarks belong to this fixed sample dataset.
class CameraPanel : public QWidget
{
  public:
    CameraPanel(GeoKernel3D::ViewerController& viewer, bool persist, QWidget* parent = nullptr)
        : QWidget(parent), viewer_(viewer), persist_(persist)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        preset_ = new QComboBox;
        preset_->addItems({"Overview", "Top view", "East view", "West view", "Close oblique"});
        layout->addWidget(preset_);
        auto* go = new QPushButton("Go to preset");
        layout->addWidget(go);
        smooth_ = new QCheckBox("Smooth transition (1.2 seconds)");
        smooth_->setChecked(true);
        layout->addWidget(smooth_);
        auto* stopButton = new QPushButton("Stop transition");
        layout->addWidget(stopButton);
        auto* row = new QHBoxLayout;
        auto* zoomIn = new QPushButton("Zoom in");
        auto* zoomOut = new QPushButton("Zoom out");
        auto* north = new QPushButton("North up");
        row->addWidget(zoomIn);
        row->addWidget(zoomOut);
        row->addWidget(north);
        layout->addLayout(row);
        layout->addWidget(new QLabel("Saved views"));
        views_ = new QComboBox;
        layout->addWidget(views_);
        auto* save = new QPushButton("Save current view");
        layout->addWidget(save);
        auto* restore = new QPushButton("Restore saved view");
        layout->addWidget(restore);
        auto* remove = new QPushButton("Delete saved view");
        layout->addWidget(remove);
        status_ = new QLabel;
        status_->setWordWrap(true);
        layout->addWidget(status_);
        auto* help = new QLabel("Left: pan · Wheel/right: zoom\nMiddle/Ctrl+left: orbit · "
                                "Shift+left: look\nMouse or keyboard input stops a transition.");
        help->setWordWrap(true);
        layout->addWidget(help);
        animation_.setDuration(1200);
        animation_.setStartValue(0.0);
        animation_.setEndValue(1.0);
        animation_.setEasingCurve(QEasingCurve::InOutCubic);
        QObject::connect(
            &animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
                const float t = value.toFloat();
                auto lerp = [t](float a, float b) { return a + (b - a) * t; };
                auto c = start_;
                c.yaw = start_.yaw + std::remainder(target_.yaw - start_.yaw, 360.f) * t;
                c.pitch = lerp(start_.pitch, target_.pitch);
                c.distance = lerp(start_.distance, target_.distance);
                c.x = lerp(start_.x, target_.x);
                c.y = lerp(start_.y, target_.y);
                c.z = lerp(start_.z, target_.z);
                viewer_.setCamera(c);
            });
        QObject::connect(go, &QPushButton::clicked, this,
                         [this] { goPreset(preset_->currentIndex()); });
        QObject::connect(stopButton, &QPushButton::clicked, this, [this] { stop(); });
        QObject::connect(smooth_, &QCheckBox::toggled, this, [this] { stop(); });
        QObject::connect(zoomIn, &QPushButton::clicked, this, [this] {
            auto c = viewer_.camera();
            c.distance = std::max(.005f, c.distance * .7f);
            moveTo(c);
        });
        QObject::connect(zoomOut, &QPushButton::clicked, this, [this] {
            auto c = viewer_.camera();
            c.distance = std::min(20.f, c.distance / .7f);
            moveTo(c);
        });
        QObject::connect(north, &QPushButton::clicked, this, [this] {
            auto c = viewer_.camera();
            c.yaw = 0;
            moveTo(c);
        });
        QObject::connect(save, &QPushButton::clicked, this, [this] {
            bool ok = false;
            const auto name =
                QInputDialog::getText(this, "Save view", "Name:", QLineEdit::Normal, {}, &ok)
                    .trimmed();
            if (ok && !name.isEmpty())
                saveView(name);
        });
        QObject::connect(restore, &QPushButton::clicked, this, [this] { restoreView(); });
        QObject::connect(remove, &QPushButton::clicked, this, [this] { deleteView(); });
        auto refreshButtons = [this, restore, remove, save] {
            restore->setEnabled(views_->count() > 0);
            remove->setEnabled(views_->count() > 0);
            save->setEnabled(views_->count() < 20);
        };
        QObject::connect(views_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                         [refreshButtons] { refreshButtons(); });
        if (persist_)
        {
            const auto entries =
                QJsonDocument::fromJson(QSettings().value("sagrada-camera-views-v1").toByteArray())
                    .array();
            for (const auto& entry : entries)
            {
                const auto object = entry.toObject();
                const auto name = object["name"].toString();
                if (name.isEmpty() || views_->count() >= 20)
                    continue;
                try
                {
                    const auto c = GeoKernel3D::readCameraStateJson(object["camera"]);
                    views_->addItem(name, QJsonDocument(GeoKernel3D::cameraStateJson(c))
                                              .toJson(QJsonDocument::Compact));
                }
                catch (const std::exception&)
                { /* Ignore corrupt or incompatible saved entries. */
                }
            }
        }
        refreshButtons();
        telemetry_.setInterval(150);
        QObject::connect(&telemetry_, &QTimer::timeout, this, [this] {
            if (!ready_)
                return;
            const auto c = viewer_.camera();
            const float yaw = std::fmod(std::fmod(c.yaw, 360.f) + 360.f, 360.f);
            status_->setText(QString("Heading: %1° · Tilt: %2°\n%3")
                                 .arg(yaw, 0, 'f', 1)
                                 .arg(c.pitch, 0, 'f', 1)
                                 .arg(animating() ? "Transition in progress" : "Ready"));
        });
        telemetry_.start();
    }
    void setHome(const GeoKernel3D::CameraState& camera)
    {
        stop();
        home_ = camera;
        ready_ = true;
    }
    void goHome()
    {
        if (ready_)
            moveTo(home_);
    }
    void goPreset(int index)
    {
        if (!ready_)
            return;
        auto c = home_;
        if (index == 1)
        {
            c.yaw = 0;
            c.pitch = 85;
        }
        if (index == 2)
        {
            c.yaw = 90;
            c.pitch = 35;
        }
        if (index == 3)
        {
            c.yaw = -90;
            c.pitch = 35;
        }
        if (index == 4)
        {
            c.pitch = 30;
            c.distance = std::max(.005f, c.distance * .55f);
        }
        moveTo(c);
    }
    void stop()
    {
        animation_.stop();
    }
    bool animating() const
    {
        return animation_.state() == QAbstractAnimation::Running;
    }
    int viewCount() const
    {
        return views_->count();
    }
    void saveView(const QString& name)
    {
        if (!ready_ || name.trimmed().isEmpty() || views_->count() >= 20)
            return;
        // Freeze and save the actual current camera, including during a transition.
        stop();
        views_->addItem(name.trimmed(),
                        QJsonDocument(GeoKernel3D::cameraStateJson(viewer_.camera()))
                            .toJson(QJsonDocument::Compact));
        views_->setCurrentIndex(views_->count() - 1);
        persistViews();
    }
    void restoreView()
    {
        if (!ready_ || views_->currentIndex() < 0)
            return;
        try
        {
            moveTo(GeoKernel3D::readCameraStateJson(
                QJsonDocument::fromJson(views_->currentData().toByteArray()).object()));
        }
        catch (const std::exception&)
        {
            status_->setText("Saved view is invalid. Delete it and save another.");
        }
    }
    void deleteView()
    {
        if (views_->currentIndex() >= 0)
        {
            views_->removeItem(views_->currentIndex());
            persistViews();
        }
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        switch (event->type())
        {
        case QEvent::MouseButtonPress:
        case QEvent::Wheel:
        case QEvent::KeyPress:
        case QEvent::TouchBegin:
            stop();
            break;
        default:
            break;
        }
        return QWidget::eventFilter(watched, event);
    }

  private:
    void moveTo(const GeoKernel3D::CameraState& camera)
    {
        stop();
        start_ = viewer_.camera();
        target_ = camera;
        if (smooth_->isChecked())
            animation_.start();
        else
            viewer_.setCamera(target_);
    }
    void persistViews()
    {
        if (!persist_)
            return;
        QJsonArray entries;
        for (int i = 0; i < views_->count(); ++i)
            entries.append(QJsonObject{
                {"name", views_->itemText(i)},
                {"camera", QJsonDocument::fromJson(views_->itemData(i).toByteArray()).object()}});
        QSettings settings;
        settings.setValue("sagrada-camera-views-v1",
                          QJsonDocument(entries).toJson(QJsonDocument::Compact));
        settings.sync();
        if (settings.status() != QSettings::NoError)
            status_->setText("Could not persist saved views.");
    }
    GeoKernel3D::ViewerController& viewer_;
    bool persist_ = false, ready_ = false;
    GeoKernel3D::CameraState home_, start_, target_;
    QComboBox *preset_ = nullptr, *views_ = nullptr;
    QCheckBox* smooth_ = nullptr;
    QLabel* status_ = nullptr;
    QVariantAnimation animation_;
    QTimer telemetry_;
};
