#pragma once
#include <GeoKernel.Viewer3D/AreaMeasurement.h>
#include <GeoKernel.Viewer3D/ViewerController.h>
#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <cmath>

// All stored points are physical ENU metres, never exaggerated render coordinates.
class MeasurementPanel : public QWidget
{
  public:
    explicit MeasurementPanel(GeoKernel3D::ViewerController& viewer, QWidget* parent = nullptr)
        : QWidget(parent), viewer_(viewer)
    {
        auto* layout = new QVBoxLayout(this);
        auto* help = new QLabel("Enable Add points, then click the terrain. Dragging still "
                                "navigates. Area closes automatically after three points.");
        help->setWordWrap(true);
        layout->addWidget(help);
        mode_ = new QComboBox;
        mode_->addItems({"Distance / polyline", "Area / polygon"});
        layout->addWidget(mode_);
        capture_ = new QCheckBox("Add points on click");
        capture_->setChecked(true);
        capture_->setEnabled(false);
        layout->addWidget(capture_);
        finish_ = new QPushButton("Finish measurement");
        layout->addWidget(finish_);
        undo_ = new QPushButton("Undo last point (Backspace)");
        layout->addWidget(undo_);
        clear_ = new QPushButton("Clear measurement (Esc)");
        layout->addWidget(clear_);
        result_ = new QLabel;
        result_->setWordWrap(true);
        result_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(result_);
        table_ = new QTableWidget(0, 3);
        table_->setHorizontalHeaderLabels({"East (m)", "North (m)", "Up (m)"});
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table_->setMinimumWidth(300);
        layout->addWidget(table_);
        auto* note = new QLabel(
            "Local ENU measurements. 3D length joins points with straight segments; it is not "
            "terrain-following length. Area is horizontal plan area, not terrain surface area. "
            "Vertical exaggeration does not change results. DEM datum is unverified.");
        note->setWordWrap(true);
        layout->addWidget(note);
        connect(mode_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this] { clearPoints(); });
        connect(capture_, &QCheckBox::toggled, this,
                [this](bool on) { viewer_.setTerrainQueryEnabled(on && bool(reference_)); });
        connect(finish_, &QPushButton::clicked, this, [this] { capture_->setChecked(false); });
        connect(undo_, &QPushButton::clicked, this, [this] { undoPoint(); });
        connect(clear_, &QPushButton::clicked, this, [this] { clearPoints(); });
        auto* backspace = new QShortcut(QKeySequence(Qt::Key_Backspace), this);
        backspace->setContext(Qt::WindowShortcut);
        connect(backspace, &QShortcut::activated, this, [this] { undoPoint(); });
        auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
        escape->setContext(Qt::WindowShortcut);
        connect(escape, &QShortcut::activated, this, [this] { clearPoints(); });
        viewer_.addTerrainQueryClickedHandler([this](QPointF screen) {
            // Do not call back into the SDK while its notification is being dispatched.
            const auto expected = reference_;
            QTimer::singleShot(0, this, [this, screen, expected] {
                if (!capture_->isChecked() || !expected || expected != reference_ ||
                    expected != viewer_.reference())
                    return;
                if (points_.size() >= 64)
                {
                    result_->setText("Maximum 64 points. Undo or clear to continue.");
                    return;
                }
                try
                {
                    const auto hit = viewer_.terrainPointAt(screen);
                    if (!hit)
                    {
                        result_->setText(
                            "No terrain at this position. Click inside the loaded terrain.");
                        return;
                    }
                    const auto point =
                        expected->fromRender({hit->x(), hit->y(), hit->z()}, viewer_.heightScale());
                    if (!points_.empty())
                    {
                        const auto last = points_.back();
                        if (std::hypot(std::hypot(point.x - last.x, point.y - last.y),
                                       point.z - last.z) < .001)
                            return;
                    }
                    points_.push_back(point);
                    refresh();
                }
                catch (const std::exception& e)
                {
                    result_->setText(QString::fromUtf8(e.what()));
                }
            });
        });
        refresh();
    }
    void resetScene()
    {
        reference_ = viewer_.reference();
        points_.clear();
        capture_->setEnabled(bool(reference_));
        viewer_.setTerrainQueryEnabled(capture_->isChecked() && bool(reference_));
        refresh();
    }

  private:
    void undoPoint()
    {
        if (!points_.empty())
            points_.pop_back();
        refresh();
    }
    void clearPoints()
    {
        points_.clear();
        refresh();
    }
    void refresh()
    {
        using namespace GeoKernel3D;
        table_->setRowCount(int(points_.size()));
        for (int i = 0; i < int(points_.size()); ++i)
        {
            const auto p = points_[i];
            const double values[] = {p.x, p.y, p.z};
            for (int c = 0; c < 3; ++c)
                table_->setItem(i, c, new QTableWidgetItem(QString::number(values[c], 'f', 2)));
        }
        undo_->setEnabled(!points_.empty());
        clear_->setEnabled(!points_.empty());
        finish_->setEnabled(false);
        if (!reference_)
        {
            result_->setText("Load the terrain to start measuring.");
            return;
        }
        const bool area = mode_->currentIndex() == 1;
        bool closed = false;
        QString areaText;
        if (area && points_.size() >= 3)
        {
            try
            {
                const auto a = planArea(points_);
                areaText = QString("Plan area: %1 m²\n").arg(a.plan, 0, 'f', 2);
                closed = true;
            }
            catch (const std::exception& e)
            {
                areaText = QString("Invalid polygon: %1\nUndo the last point or clear.\n")
                               .arg(QString::fromUtf8(e.what()));
            }
        }
        if (!viewer_.setMeasurementLine(reference_, points_, closed))
        {
            result_->setText("Measurement overlay rejected. Reload the scene.");
            return;
        }
        double horizontal = 0, length = 0;
        auto segment = [&](Vec3d a, Vec3d b) {
            const double h = std::hypot(b.x - a.x, b.y - a.y);
            horizontal += h;
            length += std::hypot(h, b.z - a.z);
        };
        for (size_t i = 1; i < points_.size(); ++i)
            segment(points_[i - 1], points_[i]);
        if (closed)
            segment(points_.back(), points_.front());
        const QString totals = QString("%1 points\n%2Horizontal %3: %4 m\n3D segment total: %5 m")
                                   .arg(points_.size())
                                   .arg(areaText)
                                   .arg(area ? "perimeter" : "length")
                                   .arg(horizontal, 0, 'f', 2)
                                   .arg(length, 0, 'f', 2);
        if (points_.size() < size_t(area ? 3 : 2))
            result_->setText(QString("%1 points. Add at least %2 points.\n")
                                 .arg(points_.size())
                                 .arg(area ? 3 : 2) +
                             totals);
        else
        {
            result_->setText(totals +
                             (area ? QString()
                                   : QString("\nEnd-to-start ΔUp: %1 m")
                                         .arg(points_.back().z - points_.front().z, 0, 'f', 2)));
            finish_->setEnabled(!area || closed);
        }
    }
    GeoKernel3D::ViewerController& viewer_;
    std::shared_ptr<const GeoKernel3D::SceneReference> reference_;
    std::vector<GeoKernel3D::Vec3d> points_;
    QComboBox* mode_;
    QCheckBox* capture_;
    QPushButton *finish_, *undo_, *clear_;
    QLabel* result_;
    QTableWidget* table_;
};
