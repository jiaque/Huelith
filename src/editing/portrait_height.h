#pragma once
#include "huelith/editing/project.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QPointF>
#include <QRectF>
#include <QImage>
#include <opencv2/core.hpp>
#include <vector>
#include <algorithm>
#include <cmath>

namespace huelith::editing {
// A skeleton-constrained inverse field. Feet stay on the ground; head and arms
// move vertically with the shoulders; screen x coordinates remain unchanged.
class PortraitHeight {
public:
    PortraitHeight(QSize size,const QJsonObject& person,double amount):size_(size) {
        const auto pose=person["settings"].toObject()["pose"].toArray();
        if(pose.size()!=33 || person["bodyAmbiguous"].toBool())throw EditError("身体关联不足，无法可靠调整身高");
        const auto point=[&](int i){const auto p=pose[i].toArray();if(p.size()!=3 || p[2].toDouble()<.25 || !std::isfinite(p[0].toDouble()) || !std::isfinite(p[1].toDouble()) || p[0].toDouble()<=0 || p[0].toDouble()>=1 || p[1].toDouble()<=0 || p[1].toDouble()>=1)throw EditError("全身、手臂或脚部定位不足，不能自动调整身高");return QPointF(p[0].toDouble()*(size.width()-1),p[1].toDouble()*(size.height()-1));};
        const auto shoulders=(point(11)+point(12))*.5,hips=(point(23)+point(24))*.5,feet=(point(27)+point(28))*.5;
        const auto delta=shoulders-feet;scale_=std::hypot(delta.x(),delta.y());if(scale_<80)throw EditError("全身尺寸不足，不能可靠调整身高");
        const auto axis=delta/scale_;
        if(QPointF::dotProduct(shoulders-hips,axis)<scale_*.15 || QPointF::dotProduct(hips-feet,axis)<scale_*.35)throw EditError("当前不是可可靠伸展的站姿，请使用手动增高");
        for(int side=0;side<2;++side){const auto hip=point(23+side),knee=point(25+side),ankle=point(27+side),leg=hip-ankle;const double length=std::hypot(leg.x(),leg.y());
            if(length<scale_*.25 || QPointF::dotProduct(leg,axis)<length*.55 || std::abs((knee-ankle).x()*leg.y()-(knee-ankle).y()*leg.x())/length>length*.35)throw EditError("坐姿、明显屈腿或大幅侧伸姿态不自动增高");}
        // Height changes screen-vertical distance; a leaning pose must not add lateral drift.
        const QPointF gain(0,-std::abs(delta.y())*.05*std::clamp(amount,-1.,1.));
        maximum_displacement_=std::hypot(gain.x(),gain.y());
        const auto hair=QImage::fromData(QByteArray::fromBase64(person["settings"].toObject()["automaticMasks"].toObject()["hair"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
        const auto hairAt=[&](QPointF p){if(hair.isNull())return false;const int x=int(p.x()/(size.width()-1)*hair.width()),y=int(p.y()/(size.height()-1)*hair.height());return x>=0 && y>=0 && x<hair.width() && y<hair.height() && hair.constScanLine(y)[x]>200;};
        std::vector<QPointF> source,change;
        const auto add=[&](QPointF p,QPointF d){if(p.x()<0 || p.y()<0 || p.x()>size.width()-1 || p.y()>size.height()-1)return;
            // Visible hair belongs to the head even when it hangs over the torso.
            if(hairAt(p))d=gain;
            if(p.x()+d.x()<0 || p.y()+d.y()<0 || p.x()+d.x()>size.width()-1 || p.y()+d.y()>size.height()-1)throw EditError("增高后人物会超出照片，请先扩展画布或减小强度");
            for(std::size_t i=0;i<source.size();++i)if(std::hypot((source[i]-p).x(),(source[i]-p).y())<scale_*.002){change[i]=(change[i]+d)*.5;return;}
            source.push_back(p);change.push_back(d);};
        const auto smooth=[](double t){return t*t*(3-2*t);};
        // Torso gain is deliberately smaller than leg gain. Interior rows
        // preserve width instead of expanding a rectangle around the person.
        for(int row=0;row<=6;++row){const double t=row/6.;const auto c=hips+(shoulders-hips)*t;const auto span=(point(24)-point(23))*(1-t)+(point(12)-point(11))*t;
            for(int col=-2;col<=2;++col)add(c+span*(col*.22),gain*(.75+.25*smooth(t)));}
        for(int side=0;side<2;++side){
            for(int segment=0;segment<2;++segment){const auto first=point(23+side+segment*2),last=point(25+side+segment*2),v=last-first;const double length=std::hypot(v.x(),v.y());const QPointF n(-v.y()/length,v.x()/length);
                for(int row=0;row<=8;++row){const double t=row/8.;const auto c=first+v*t;const double weight=segment==0?.75-.375*smooth(t):.375*(1-smooth(t));for(int col=-1;col<=1;++col)add(c+n*(col*scale_*.025),gain*weight);}}
            // Arm segments translate as a unit with their shoulder.
            for(int segment=0;segment<2;++segment){const auto first=point(11+side+segment*2),last=point(13+side+segment*2),v=last-first;const double length=std::hypot(v.x(),v.y());if(length<scale_*.04)throw EditError("手臂投影过短，无法保持手臂形状");const QPointF n(-v.y()/length,v.x()/length);
                for(int row=0;row<=5;++row)for(int col=-1;col<=1;++col)add(first+v*(row/5.)+n*(col*scale_*.022),gain);}
            const auto ankle=point(27+side),heel=point(29+side),toe=point(31+side);QRectF foot(ankle,QSizeF(1,1));foot=foot.united(QRectF(heel,QSizeF(1,1))).united(QRectF(toe,QSizeF(1,1))).adjusted(-scale_*.018,-scale_*.018,scale_*.018,scale_*.018);
            for(int y=0;y<=3;++y)for(int x=0;x<=4;++x)add({foot.left()+x*foot.width()/4,foot.top()+y*foot.height()/3},{});
        }
        const auto face=person["face"].toArray();if(face.size()!=4)throw EditError("缺少头部定位，不能保护头部比例");
        QRectF head(face[0].toDouble()*(size.width()-1),face[1].toDouble()*(size.height()-1),face[2].toDouble()*(size.width()-1),face[3].toDouble()*(size.height()-1));head=head.adjusted(-head.width()*.15,-head.height()*.5,head.width()*.15,head.height()*.1);
        for(int y=0;y<=5;++y)for(int x=0;x<=4;++x)add({head.left()+x*head.width()/4,head.top()+y*head.height()/5},gain);
        for(const auto& p:person["landmarks"].toArray()){const auto q=p.toArray();add({q[0].toDouble()*(size.width()-1),q[1].toDouble()*(size.height()-1)},gain);}
        if(!hair.isNull()){
            const int step=std::max(4,int(scale_*.02));
            for(int y=0;y<size.height();y+=step)for(int x=0;x<size.width();x+=step)
                if(hairAt({double(x),double(y)}))add({double(x),double(y)},gain);
            // Include the silhouette, not only grid points inside the hairstyle.
            const int rowStep=std::max(1,int(double(step)*hair.height()/size.height()));
            for(int y=0;y<hair.height();y+=rowStep)for(int x=0;x<hair.width();++x){
                if(hair.constScanLine(y)[x]<=200)continue;
                if(x==0 || x==hair.width()-1 || hair.constScanLine(y)[x-1]<=200 || hair.constScanLine(y)[x+1]<=200)
                    add({(x+.5)*(size.width()-1)/hair.width(),(y+.5)*(size.height()-1)/hair.height()},gain);
            }
        }
        QRectF bounds;for(const auto& p:source)bounds=bounds.isNull()?QRectF(p,QSizeF(1,1)):bounds.united(QRectF(p,QSizeF(1,1)));bounds=bounds.adjusted(-scale_*.25,-scale_*.25,scale_*.25,scale_*.25);
        // A fixed outer ring keeps the field compact in the surrounding image.
        for(int i=0;i<=12;++i){const double t=i/12.;add({bounds.left()+t*bounds.width(),bounds.top()},{});add({bounds.left()+t*bounds.width(),bounds.bottom()},{});add({bounds.left(),bounds.top()+t*bounds.height()},{});add({bounds.right(),bounds.top()+t*bounds.height()},{});}
        for(std::size_t i=0;i<source.size();++i)centers_.push_back((source[i]+change[i])/scale_);
        const int n=int(centers_.size());cv::Mat matrix(n,n,CV_64F),rhs(n,2,CV_64F);
        for(int i=0;i<n;++i){for(int j=0;j<n;++j){const auto d=centers_[i]-centers_[j];matrix.at<double>(i,j)=basis(std::hypot(d.x(),d.y()))+(i==j?1e-8:0);}
            rhs.at<double>(i,0)=-change[i].x()/scale_;rhs.at<double>(i,1)=-change[i].y()/scale_;}
        if(!cv::solve(matrix,rhs,weights_,cv::DECOMP_CHOLESKY) || !cv::checkRange(weights_))throw EditError("身高约束求解失败，请检查人物定位");
        // Index the exact compact support. A pixel need not visit every head,
        // torso and foot constraint when most of them contribute exactly zero.
        columns_=std::max(1,int(std::ceil(((size.width()-1)/scale_+.7)/cellSize))+1);
        rows_=std::max(1,int(std::ceil(((size.height()-1)/scale_+.7)/cellSize))+1);
        cells_.resize(std::size_t(columns_)*rows_);
        for(int i=0;i<n;++i){const auto c=centers_[i];const int left=std::max(0,int(std::floor(c.x()/cellSize))),right=std::min(columns_-1,int(std::floor((c.x()+.7)/cellSize)));
            const int top=std::max(0,int(std::floor(c.y()/cellSize))),bottom=std::min(rows_-1,int(std::floor((c.y()+.7)/cellSize)));
            for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x)cells_[std::size_t(y)*columns_+x].push_back(i);
        }
        bounds_=bounds.adjusted(-scale_*.35,-scale_*.35,scale_*.35,scale_*.35);bounds_={bounds_.x()/(size.width()-1),bounds_.y()/(size.height()-1),bounds_.width()/(size.width()-1),bounds_.height()/(size.height()-1)};
    }
    QRectF bounds()const{return bounds_;}
    double maximum_displacement()const{return maximum_displacement_;}
    QPointF displacement(QPointF q)const{const QPointF p(q.x()*(size_.width()-1)/scale_,q.y()*(size_.height()-1)/scale_);const int x=int(std::floor((p.x()+.35)/cellSize)),y=int(std::floor((p.y()+.35)/cellSize));if(x<0 || y<0 || x>=columns_ || y>=rows_)return {};QPointF d;
        for(int i:cells_[std::size_t(y)*columns_+x]){const auto v=p-centers_[i];const double distance2=v.x()*v.x()+v.y()*v.y();if(distance2>=.35*.35)continue;const double w=basis(std::sqrt(distance2));d+=QPointF(weights_.at<double>(i,0),weights_.at<double>(i,1))*w;}return {d.x()*scale_/(size_.width()-1),d.y()*scale_/(size_.height()-1)};}
private:
    static double basis(double d){const double r=d/.35;if(r>=1)return 0;const double t=1-r;return t*t*t*t*(1+4*r);}
    static constexpr double cellSize=.175;
    int columns_{},rows_{};std::vector<std::vector<int>> cells_;
    QSize size_;double scale_{},maximum_displacement_{};std::vector<QPointF> centers_;cv::Mat weights_;QRectF bounds_;
};
}
