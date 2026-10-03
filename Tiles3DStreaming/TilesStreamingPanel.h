#pragma once
#include <GeoKernel.Viewer3D/ViewerController.h>
#include <GeoKernel.Viewer3D/ModelPlacement.h>
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QTimer>
#include <QSignalBlocker>
#include <algorithm>
#include <exception>
#include <stdexcept>

class TilesStreamingPanel final : public QWidget {
    GeoKernel3D::ViewerController& viewer_;
    QString source_, actionError_;
    QSpinBox* budget_;
    QCheckBox* visible_;
    QPushButton *start_, *stop_, *focus_;
    QLabel *status_, *errors_;

    template<class F> void perform(F action) {
        try { action(); actionError_.clear(); }
        catch(const std::exception& e) { actionError_=QString::fromUtf8(e.what()); }
        refresh();
    }
    void start() {
        perform([&]{
            if(source_.isEmpty())return;
            viewer_.startTileset(source_,true,uint64_t(budget_->value())*1024*1024);
            const QSignalBlocker block(visible_);visible_->setChecked(true);
        });
    }
    void refresh() {
        const auto s=viewer_.tilesetState();
        const auto m=viewer_.modelState();
        const auto d=viewer_.renderDiagnostics();
        start_->setEnabled(!source_.isEmpty());
        stop_->setEnabled(s.active);
        focus_->setEnabled(m.attached && visible_->isChecked());
        status_->setText(QString("State: %1\nLoaded tiles: %2\nDeferred tiles: %3\nDetail budget limited: %4\nCPU cache: %5 MiB\nCache hits: %6\nDrawn vertices: %7\nGPU upload pending: %8")
            .arg(s.active?(s.loading?"Updating (previous tiles visible)":"Active"):"Stopped")
            .arg(s.tiles).arg(s.deferredTiles).arg(s.selectionLimited?"Yes":"No")
            .arg(double(s.cacheBytes)/(1024*1024),0,'f',1).arg(s.cacheHits)
            .arg(m.drawnVertices).arg(m.uploadPending?"Yes":"No"));
        QStringList errors{actionError_,s.error,m.error,d.renderError,d.imageryStreamError};
        errors.removeAll(QString());errors.removeDuplicates();errors_->setText(errors.join('\n'));
    }
public:
    explicit TilesStreamingPanel(GeoKernel3D::ViewerController& viewer,QWidget* parent=nullptr)
        :QWidget(parent),viewer_(viewer) {
        auto* layout=new QVBoxLayout(this);
        auto* explanation=new QLabel("Camera movement selects tile detail automatically. Data streams from the downloaded local archive.");
        explanation->setWordWrap(true);layout->addWidget(explanation);
        layout->addWidget(new QLabel("CPU geometry batch budget (MiB)"));
        budget_=new QSpinBox;budget_->setRange(64,128);budget_->setSingleStep(32);budget_->setValue(96);layout->addWidget(budget_);
        start_=new QPushButton("Start / apply budget");layout->addWidget(start_);
        stop_=new QPushButton("Stop streaming (keep current tiles)");layout->addWidget(stop_);
        visible_=new QCheckBox("Show buildings");visible_->setChecked(true);layout->addWidget(visible_);
        focus_=new QPushButton("Focus loaded tiles");layout->addWidget(focus_);
        status_=new QLabel;status_->setWordWrap(true);status_->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(status_);
        errors_=new QLabel;errors_->setWordWrap(true);errors_->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(errors_);
        auto* note=new QLabel("Hiding clears the loaded model; showing restarts streaming. Stop may wait for in-flight work. Cache counters are not total RAM/VRAM usage.");
        note->setWordWrap(true);layout->addWidget(note);layout->addStretch();
        connect(start_,&QPushButton::clicked,this,[this]{start();});
        connect(stop_,&QPushButton::clicked,this,[this]{perform([this]{viewer_.stopTileset();});});
        connect(visible_,&QCheckBox::toggled,this,[this](bool on){
            if(on)start();else perform([this]{viewer_.setModel(nullptr);});
        });
        connect(focus_,&QPushButton::clicked,this,[this]{perform([this]{
            const auto snapshot=viewer_.modelSelection();const auto model=snapshot.model;
            if(!model||!model->reference||model->vertices.empty())return;
            GeoKernel3D::Vec3d low{1e100,1e100,1e100},high{-1e100,-1e100,-1e100};
            for(const auto& v:model->vertices){
                const auto p=model->reference->fromRender({v.x,v.y,v.z});
                low={std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};
                high={std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};
            }
            if(!viewer_.focusModelBounds(model,low,high))throw std::runtime_error("Loaded tiles cannot be focused in the current scene.");
        });});
        auto* timer=new QTimer(this);timer->setInterval(500);
        connect(timer,&QTimer::timeout,this,[this]{refresh();});timer->start();refresh();
    }
    void setSource(const QString& source){source_=source;start();}
};
