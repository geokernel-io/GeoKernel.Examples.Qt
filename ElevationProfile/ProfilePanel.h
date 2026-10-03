#pragma once
#include <GeoKernel.Viewer3D/ViewerController.h>
#include <GeoKernel.Viewer3D/TerrainProfile.h>
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

class ProfileChart : public QWidget {
public:
    GeoKernel3D::TerrainProfile profile;
    explicit ProfileChart(QWidget* parent=nullptr):QWidget(parent){setMinimumSize(350,240);}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),palette().base());
        QRectF plot(65,25,width()-85,height()-80);
        double lo=1e100,hi=-1e100;
        for(const auto& s:profile.samples)if(s.height){lo=std::min(lo,*s.height);hi=std::max(hi,*s.height);}
        p.setPen(palette().text().color());
        if(lo>hi||profile.horizontal<=0){p.drawText(rect(),Qt::AlignCenter,"Click two or more terrain points.");return;}
        const double pad=std::max(0.5,(hi-lo)*0.1);lo-=pad;hi+=pad;
        p.drawText(QRectF(5,0,width()-10,22),Qt::AlignCenter,"Source height (m; datum unverified)");
        for(int i=0;i<=4;++i){double t=i/4.;double y=plot.bottom()-t*plot.height();
            p.setPen(QColor(210,210,210));p.drawLine(QPointF(plot.left(),y),QPointF(plot.right(),y));
            p.setPen(palette().text().color());p.drawText(QRectF(0,y-10,60,20),Qt::AlignRight|Qt::AlignVCenter,QString::number(lo+t*(hi-lo),'f',1));
            p.drawText(QRectF(plot.left()+t*plot.width()-30,plot.bottom()+5,60,20),Qt::AlignCenter,QString::number(t*profile.horizontal,'f',0));}
        p.drawText(QRectF(0,height()-25,width(),20),Qt::AlignCenter,"Horizontal distance (m)");
        QPainterPath path;bool connected=false;
        for(const auto& s:profile.samples){if(!s.height){connected=false;continue;}
            QPointF point(plot.left()+s.distance/profile.horizontal*plot.width(),plot.bottom()-(*s.height-lo)/(hi-lo)*plot.height());
            if(connected)path.lineTo(point);else path.moveTo(point);connected=true;}
        p.setPen(QPen(QColor(25,120,180),2));p.drawPath(path);
    }
};

class ProfilePanel : public QWidget {
public:
    explicit ProfilePanel(GeoKernel3D::ViewerController& viewer,QWidget* parent=nullptr):QWidget(parent),viewer_(viewer){
        auto* layout=new QVBoxLayout(this);
        auto* help=new QLabel("Enable Add points and click along a route on the terrain. Dragging still navigates. Finish to stop adding points.");help->setWordWrap(true);layout->addWidget(help);
        capture_=new QCheckBox("Add points on click");capture_->setChecked(true);capture_->setEnabled(false);layout->addWidget(capture_);
        auto* finish=new QPushButton("Finish route");layout->addWidget(finish);
        undo_=new QPushButton("Undo last point");layout->addWidget(undo_);
        auto* clear=new QPushButton("Clear route");layout->addWidget(clear);
        layout->addWidget(new QLabel("Requested sample spacing"));
        spacing_=new QDoubleSpinBox;spacing_->setRange(1,100);spacing_->setValue(10);spacing_->setSuffix(" m");layout->addWidget(spacing_);
        chart_=new ProfileChart;layout->addWidget(chart_,1);
        result_=new QLabel;result_->setWordWrap(true);result_->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(result_);
        auto* note=new QLabel("Sampled from the loaded DEM mesh (512 samples per side). Maximum 4096 profile samples. Gaps are not joined. Heights and ascent/descent are approximate; finer sampling cannot add DEM detail. Exaggeration affects only the 3D view.");note->setWordWrap(true);layout->addWidget(note);
        connect(capture_,&QCheckBox::toggled,this,[this](bool on){viewer_.setTerrainQueryEnabled(on&&bool(ref_));});
        connect(finish,&QPushButton::clicked,this,[this]{capture_->setChecked(false);});
        connect(undo_,&QPushButton::clicked,this,[this]{if(!points_.empty())points_.pop_back();recalculate();});
        connect(clear,&QPushButton::clicked,this,[this]{points_.clear();recalculate();});
        connect(spacing_,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{recalculate();});
        timer_.setInterval(15);
        connect(&timer_,&QTimer::timeout,this,[this]{
            if(ref_!=viewer_.reference()){timer_.stop();return;}
            try{GeoKernel3D::advanceTerrainProfile(profile_,*ref_,[this](float x,float z){return viewer_.terrainRenderHeight(ref_,x,z);},32);
                chart_->profile=profile_;chart_->update();
                result_->setText(QString("%1 route points | %2 / %3 samples\nHorizontal length: %4 m\nAscent: %5 m | Descent: %6 m\nMaximum interval: %7 m | Missing: %8")
                    .arg(points_.size()).arg(profile_.processed).arg(profile_.samples.size()).arg(profile_.horizontal,0,'f',1).arg(profile_.ascent,0,'f',1).arg(profile_.descent,0,'f',1).arg(profile_.maxInterval,0,'f',1).arg(profile_.missing));
                if(profile_.processed==profile_.samples.size())timer_.stop();
            }catch(const std::exception& e){timer_.stop();result_->setText(QString::fromUtf8(e.what()));}
        });
        viewer_.addTerrainQueryClickedHandler([this](QPointF screen){const auto expected=ref_;
            QTimer::singleShot(0,this,[this,screen,expected]{
                if(!capture_->isChecked()||!expected||expected!=ref_||expected!=viewer_.reference())return;
                if(points_.size()>=64){result_->setText("Maximum 64 route points. Undo or clear to continue.");return;}
                try{const auto hit=viewer_.terrainPointAt(screen);if(!hit){result_->setText("Click inside the loaded terrain.");return;}
                    const auto point=ref_->fromRender({hit->x(),hit->y(),hit->z()},viewer_.heightScale());
                    if(!points_.empty()&&std::hypot(point.x-points_.back().x,point.y-points_.back().y)<.01)return;
                    points_.push_back(point);recalculate();
                }catch(const std::exception& e){result_->setText(QString::fromUtf8(e.what()));}
            });
        });
        recalculate();
    }
    void resetScene(){timer_.stop();ref_=viewer_.reference();points_.clear();capture_->setEnabled(bool(ref_));viewer_.setTerrainQueryEnabled(capture_->isChecked()&&bool(ref_));recalculate();}
private:
    void recalculate(){
        timer_.stop();profile_={};chart_->profile={};chart_->update();undo_->setEnabled(!points_.empty());
        if(!ref_){result_->setText("Load terrain to draw a profile.");return;}
        if(!viewer_.setMeasurementLine(ref_,points_)){result_->setText("Route overlay rejected. Reload the scene.");return;}
        result_->setText(QString("%1 route points. Add at least two points.").arg(points_.size()));
        if(points_.size()<2)return;
        try{std::vector<GeoKernel3D::MeasurementPoint> route;
            for(const auto& p:points_)route.push_back({ref_->fromEnu(p),0,false});
            profile_=GeoKernel3D::planTerrainProfile(*ref_,route,spacing_->value(),4096);timer_.start();
        }catch(const std::exception& e){result_->setText(QString::fromUtf8(e.what()));}
    }
    GeoKernel3D::ViewerController& viewer_;
    std::shared_ptr<const GeoKernel3D::SceneReference> ref_;
    std::vector<GeoKernel3D::Vec3d> points_;
    GeoKernel3D::TerrainProfile profile_;
    QTimer timer_;QCheckBox* capture_;QPushButton* undo_;QDoubleSpinBox* spacing_;ProfileChart* chart_;QLabel* result_;
};
