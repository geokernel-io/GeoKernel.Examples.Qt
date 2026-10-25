#pragma once
#include "Viewer/GisViewer.h"
#include <QPainter>
#include <QResizeEvent>
#include <QTimer>
// Render both map layers to the same extent/viewport. The swipe only changes
// clipping of cached images; dragging it never reruns inference or map rendering.
class ComparisonCanvas : public QWidget
{
  public:
    QImage before, after;
    int percentage = 50;
    bool active = false;
    explicit ComparisonCanvas(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(228, 228, 228));
        if (!before.isNull())
            p.drawImage(rect(), before);
        if (!active || after.isNull())
            return;
        const int split = width() * (100 - percentage) / 100;
        if (percentage > 0)
        {
            p.save();
            p.setClipRect(QRect(split, 0, width() - split, height()));
            p.drawImage(rect(), after);
            p.restore();
        }
        p.setPen(QPen(Qt::white, 2));
        p.drawLine(split, 0, split, height());
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(10, 20, 30, 200));
        p.drawRoundedRect(QRect(8, 8, 135, 26), 4, 4);
        p.drawRoundedRect(QRect(width() - 163, 8, 155, 26), 4, 4);
        p.setPen(Qt::white);
        p.drawText(QRect(8, 8, 135, 26), Qt::AlignCenter, "Original /10m");
        p.drawText(QRect(width() - 163, 8, 155, 26), Qt::AlignCenter, "AI output /2.5m grid");
    }
};
class ComparisonView : public QWidget
{
    ComparisonCanvas *canvas;
    QTimer refreshTimer;
    bool ready = false;

  public:
    GeoKernel::Viewer::GisViewer *original;
    GeoKernel::Viewer::GisViewer *enhanced;
    explicit ComparisonView(QWidget *parent = nullptr) : QWidget(parent)
    {
        original = new GeoKernel::Viewer::GisViewer(this);
        enhanced = new GeoKernel::Viewer::GisViewer(this);
        enhanced->hide();
        canvas = new ComparisonCanvas(this);
        refreshTimer.setSingleShot(true);
        refreshTimer.setInterval(20);
        QObject::connect(&refreshTimer, &QTimer::timeout, this, [this] { refreshImages(); });
        QObject::connect(original, &GeoKernel::Viewer::GisViewer::viewChanged, this,
                         [this]
                         {
                             if (ready)
                                 enhanced->setViewExtent(original->viewExtent());
                             if (!refreshTimer.isActive())
                                 refreshTimer.start();
                         });
    }
    void setComparison(int value)
    {
        canvas->percentage = std::clamp(value, 0, 100);
        canvas->update();
    }
    void setResultReady(bool value)
    {
        ready = value;
        canvas->active = value;
        if (ready)
            enhanced->setViewExtent(original->viewExtent());
        refreshTimer.start();
        canvas->update();
    }
    int splitPosition() const
    {
        return width() * (100 - canvas->percentage) / 100;
    }
    bool hasResult() const
    {
        return ready;
    }
    void refreshImages()
    {
        if (width() < 1 || height() < 1 || !original->mapLayerAt(0))
            return;
        const auto extent = original->viewExtent();
        canvas->before = original->renderToImage(size(), extent);
        if (ready && enhanced->mapLayerAt(0))
            canvas->after = enhanced->renderToImage(size(), extent);
        else
            canvas->after = QImage();
        canvas->update();
    }

  protected:
    void resizeEvent(QResizeEvent *) override
    {
        original->setGeometry(rect());
        enhanced->setGeometry(rect());
        canvas->setGeometry(rect());
        canvas->raise();
        if (ready)
            enhanced->setViewExtent(original->viewExtent());
        refreshTimer.start();
    }
};
