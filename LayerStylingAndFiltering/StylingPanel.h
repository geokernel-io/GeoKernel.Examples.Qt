#pragma once
#include <GeoKernel.Viewer3D/ViewerController.h>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QWidget>

class StylingPanel : public QWidget
{
  public:
    explicit StylingPanel(GeoKernel3D::ViewerController& viewer, QWidget* parent = nullptr)
        : QWidget(parent), viewer_(viewer)
    {
        auto* form = new QFormLayout(this);
        auto* help = new QLabel("Choose a layer to change its appearance or filter its features. "
                                "Each layer keeps its own settings.");
        help->setWordWrap(true);
        form->addRow(help);
        layer_ = new QComboBox;
        form->addRow("Layer", layer_);
        visible_ = new QCheckBox("Visible");
        form->addRow(visible_);
        opacity_ = new QSlider(Qt::Horizontal);
        opacity_->setRange(0, 100);
        opacityText_ = new QLabel;
        form->addRow("Opacity", opacity_);
        form->addRow(opacityText_);
        color_ = new QPushButton("Choose color…");
        form->addRow(color_);
        auto* source = new QPushButton("Restore source color");
        form->addRow(source);
        theme_ = new QComboBox;
        form->addRow("Color by attribute", theme_);
        field_ = new QComboBox;
        form->addRow("Filter attribute", field_);
        text_ = new QLineEdit;
        text_->setPlaceholderText("Contains text (case-insensitive)");
        form->addRow("Contains", text_);
        values_ = new QComboBox;
        form->addRow("Example values", values_);
        auto* clear = new QPushButton("Clear this filter");
        form->addRow(clear);
        count_ = new QLabel;
        count_->setWordWrap(true);
        form->addRow(count_);
        focus_ = new QPushButton("Zoom to matching features");
        form->addRow(focus_);
        auto* reset = new QPushButton("Reset all layer styles and filters");
        form->addRow(reset);
        auto* note = new QLabel(
            "Filters use literal substring matching and affect drawing and selection. Matching "
            "counts include source records with no drawable geometry. Hidden or transparent layers "
            "may still have matches. Attribute coloring overrides the uniform color. Changes are "
            "session-only and do not modify source files.");
        note->setWordWrap(true);
        form->addRow(note);
        debounce_.setSingleShot(true);
        debounce_.setInterval(250);
        connect(&debounce_, &QTimer::timeout, this, [this] { applyFilter(); });
        connect(layer_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
            if (debounce_.isActive() && scene_ && scene_ == viewer_.scene() && !activeId_.isEmpty())
                viewer_.setFilter(activeId_, {field_->currentData().toString(), text_->text()});
            debounce_.stop();
            sync();
        });
        connect(visible_, &QCheckBox::toggled, this, [this](bool visible) {
            if (syncing_ || !current())
                return;
            auto a = viewer_.layerAppearance(display());
            a.visible = visible;
            viewer_.setLayerAppearance(display(), a);
            counts();
        });
        connect(opacity_, &QSlider::valueChanged, this, [this](int opacity) {
            if (syncing_ || !current())
                return;
            auto a = viewer_.layerAppearance(display());
            a.opacity = opacity / 100.f;
            viewer_.setLayerAppearance(display(), a);
            opacityText_->setText(QString::number(opacity) + "%");
            counts();
        });
        connect(color_, &QPushButton::clicked, this, [this] {
            if (!current())
                return;
            applyFilter();
            auto c = QColorDialog::getColor(viewer_.layerAppearance(display()).color, this,
                                            "Layer color");
            if (c.isValid())
            {
                viewer_.setLayerColor(display(), c);
                sync();
            }
        });
        connect(source, &QPushButton::clicked, this, [this] {
            if (current())
            {
                applyFilter();
                viewer_.setLayerColor(display(), {});
                sync();
            }
        });
        connect(theme_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
            if (syncing_ || !current())
                return;
            viewer_.setTheme(id(), theme_->currentData().toString());
            counts();
        });
        connect(field_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
            if (syncing_)
                return;
            populateValues();
            applyFilter();
        });
        connect(text_, &QLineEdit::textChanged, this, [this] {
            if (!syncing_)
                debounce_.start();
        });
        connect(values_, qOverload<int>(&QComboBox::activated), this, [this](int index) {
            if (index > 0)
                text_->setText(values_->itemData(index).toString());
        });
        connect(clear, &QPushButton::clicked, this, [this] {
            debounce_.stop();
            text_->clear();
            applyFilter();
        });
        connect(focus_, &QPushButton::clicked, this, [this] {
            debounce_.stop();
            applyFilter();
            if (current())
                viewer_.focusLayer(id());
        });
        connect(reset, &QPushButton::clicked, this, [this] {
            debounce_.stop();
            if (!scene_)
                return;
            for (const auto& l : scene_->layers)
            {
                if (l.type != GeoKernel3D::LayerType::Roads &&
                    l.type != GeoKernel3D::LayerType::Buildings)
                    continue;
                viewer_.setFilter(l.id, {});
                viewer_.setLayerColor(l.type == GeoKernel3D::LayerType::Roads
                                          ? GeoKernel3D::DisplayLayer::Roads
                                          : GeoKernel3D::DisplayLayer::Buildings,
                                      {});
                viewer_.setLayerAppearance(l.type == GeoKernel3D::LayerType::Roads
                                               ? GeoKernel3D::DisplayLayer::Roads
                                               : GeoKernel3D::DisplayLayer::Buildings,
                                           {});
            }
            sync();
        });
        setEnabled(false);
    }
    void resetScene()
    {
        debounce_.stop();
        scene_ = viewer_.scene();
        QSignalBlocker b(layer_);
        layer_->clear();
        if (scene_)
            for (const auto& l : scene_->layers)
                if (l.type == GeoKernel3D::LayerType::Roads ||
                    l.type == GeoKernel3D::LayerType::Buildings)
                    layer_->addItem(l.name, l.id);
        setEnabled(layer_->count() > 0);
        sync();
    }

  private:
    QString id() const
    {
        return layer_->currentData().toString();
    }
    const GeoKernel3D::Layer* current() const
    {
        return scene_ && scene_ == viewer_.scene() ? scene_->layer(id()) : nullptr;
    }
    GeoKernel3D::DisplayLayer display() const
    {
        return current()->type == GeoKernel3D::LayerType::Roads
                   ? GeoKernel3D::DisplayLayer::Roads
                   : GeoKernel3D::DisplayLayer::Buildings;
    }
    void sync()
    {
        const auto* l = current();
        if (!l)
            return;
        activeId_ = id();
        syncing_ = true;
        auto appearance = viewer_.layerAppearance(display());
        visible_->setChecked(appearance.visible);
        opacity_->setValue(qRound(appearance.opacity * 100));
        opacityText_->setText(QString::number(opacity_->value()) + "%");
        color_->setText(appearance.color.isValid() ? "Uniform color: " + appearance.color.name()
                                                   : "Choose uniform color…");
        QSet<QString> fields;
        for (const auto& f : l->features)
            for (auto it = f.attributes.cbegin(); it != f.attributes.cend(); ++it)
                fields.insert(it.key());
        auto sorted = fields.values();
        sorted.sort(Qt::CaseInsensitive);
        field_->clear();
        field_->addItem("All attributes / source IDs", QString());
        theme_->clear();
        theme_->addItem("Uniform / source color", QString());
        for (const auto& key : sorted)
        {
            field_->addItem(key, key);
            theme_->addItem(key, key);
        }
        auto filter = viewer_.filter(id());
        field_->setCurrentIndex(std::max(0, field_->findData(filter.field)));
        text_->setText(filter.text);
        theme_->setCurrentIndex(std::max(0, theme_->findData(viewer_.theme(id()))));
        populateValues();
        syncing_ = false;
        counts();
    }
    void populateValues()
    {
        QSignalBlocker b(values_);
        values_->clear();
        values_->addItem("Choose a value or type text");
        const auto* l = current();
        const auto key = field_->currentData().toString();
        if (!l || key.isEmpty())
        {
            values_->setEnabled(false);
            return;
        }
        QSet<QString> values;
        for (const auto& f : l->features)
        {
            auto value = f.attributes.value(key).toString();
            if (!value.isEmpty())
                values.insert(value);
            if (values.size() >= 200)
                break;
        }
        auto sorted = values.values();
        sorted.sort(Qt::CaseInsensitive);
        for (const auto& value : sorted)
            values_->addItem(value, value);
        values_->setEnabled(!sorted.empty());
        values_->setToolTip(
            "Up to 200 distinct example values; filtering always searches all features.");
    }
    void applyFilter()
    {
        debounce_.stop();
        if (!current())
            return;
        viewer_.setFilter(id(), {field_->currentData().toString(), text_->text()});
        counts();
    }
    void counts()
    {
        if (!current())
            return;
        auto summary = viewer_.filterSummary(id());
        if (!summary)
            return;
        count_->setText(QString("Matching records: %1 / %2\nDrawable matching ranges: %3")
                            .arg(summary->matchingRecords)
                            .arg(summary->records)
                            .arg(summary->drawableRanges));
        auto a = viewer_.layerAppearance(display());
        focus_->setEnabled(summary->drawableRanges > 0 && a.visible && a.opacity > 0);
    }
    GeoKernel3D::ViewerController& viewer_;
    std::shared_ptr<const GeoKernel3D::Scene> scene_;
    bool syncing_ = false;
    QString activeId_;
    QTimer debounce_;
    QComboBox *layer_, *theme_, *field_, *values_;
    QCheckBox* visible_;
    QSlider* opacity_;
    QLabel *opacityText_, *count_;
    QPushButton *color_, *focus_;
    QLineEdit* text_;
};
