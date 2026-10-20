#pragma once
#include "portrait_neck.h"
#include <opencv2/core.hpp>
#include <QLineF>
#include <vector>

namespace huelith::editing {
// Extend the visible neck between a fixed clavicle and a translated head.
// Width remains a separate field; the head is never scaled to create length.
class PortraitNeckLength {
public:
    PortraitNeckLength(QSize size,const QJsonObject& person,double amount):size_(size){
        PortraitNeck visible(size,person,0);
        const auto settings=person["settings"].toObject();const auto pose=settings["pose"].toArray(),lm=person["landmarks"].toArray(),face=person["face"].toArray();
        const auto pixel=[&](const QJsonArray& points,int i){const auto q=points[i].toArray();return QPointF(q[0].toDouble()*(size.width()-1),q[1].toDouble()*(size.height()-1));};
        const auto left=pixel(pose,11),right=pixel(pose,12),base=visible.base(),chin=visible.chin(),axis=chin-base;
        scale_=face[2].toDouble()*(size.width()-1);const auto gain=axis*(.12*std::clamp(amount,-1.,1.));maximum_=std::hypot(gain.x(),gain.y());
        const auto hair=QImage::fromData(QByteArray::fromBase64(settings["automaticMasks"].toObject()["hair"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
        const auto hairAt=[&](QPointF p){if(hair.isNull())return false;const int x=int(p.x()/(size.width()-1)*hair.width()),y=int(p.y()/(size.height()-1)*hair.height());return x>=0 && y>=0 && x<hair.width() && y<hair.height() && hair.constScanLine(y)[x]>200;};
        std::vector<QPointF> source,delta;
        const auto add=[&](QPointF p,QPointF d){
            if(p.x()<0 || p.y()<0 || p.x()>size.width()-1 || p.y()>size.height()-1)return;
            const double hairJoin=scale_*.035;
            bool movingHair=hairAt(p);
            for(int yy=-1;yy<=1 && !movingHair;++yy)for(int xx=-1;xx<=1 && !movingHair;++xx)movingHair=hairAt(p+QPointF(xx*hairJoin,yy*hairJoin));
            if(movingHair)d=gain;
            if(p.x()+d.x()<0 || p.y()+d.y()<0 || p.x()+d.x()>size.width()-1 || p.y()+d.y()>size.height()-1)throw EditError("调整颈长后头部会超出照片，请减小强度或扩展画布");
            // Do not merge a fixed clavicle anchor into a nearby moving head
            // anchor merely because both displacements are small in face units.
            for(std::size_t i=0;i<source.size();++i)if(QLineF(source[i],p).length()<scale_*.0015){if(QLineF(delta[i],d).length()>scale_*.003)throw EditError("头部与锁骨约束重叠，请修正定位");return;}
            source.push_back(p);delta.push_back(d);
        };
        QRectF head(face[0].toDouble()*(size.width()-1),face[1].toDouble()*(size.height()-1),scale_,face[3].toDouble()*(size.height()-1));
        // Hair is constrained from its actual mask below. Expanding a face
        // rectangle above the canvas would move background samples and falsely
        // reject a head whose real outline still fits inside the photograph.
        for(int y=0;y<=6;++y)for(int x=0;x<=6;++x)add({head.left()+head.width()*x/6,head.top()+head.height()*y/6},gain);
        for(int i=0;i<lm.size();++i)add(pixel(lm,i),gain);
        const auto shoulderAxis=right-left;const double shoulderLength=std::hypot(shoulderAxis.x(),shoulderAxis.y());const auto normal=shoulderAxis/shoulderLength;
        for(int row=0;row<=6;++row){const double t=row/6.;const double weight=t*t*(3-2*t);
            for(int col=-2;col<=2;++col)add(base+axis*t+normal*(scale_*.12*col),gain*weight);
        }
        // In a turned torso the straight line between arm-joint landmarks can
        // cross the visible neck. It is not a rigid clavicle contour. Keep the
        // two actual shoulders and the skin/clothing junction fixed instead.
        add(left,{});add(right,{});add(base,{});
        for(int side=0;side<2;++side)for(int index:{13,15,23})if(pose[index+side].toArray()[2].toDouble()>=.6)add(pixel(pose,index+side),{});
        if(!hair.isNull()){const int step=std::max(4,int(scale_*.12));
            for(int y=0;y<size.height();y+=step)for(int x=0;x<size.width();x+=step)if(hairAt({double(x),double(y)}))add({double(x),double(y)},gain);
            const int tipStep=std::max(3,int(scale_*.025));
            for(int y=std::max(0,int(chin.y()-scale_*.35));y<size.height();y+=tipStep)
                for(int x=0;x<size.width();x+=tipStep)if(hairAt({double(x),double(y)}))add({double(x),double(y)},gain);
            // Sparse interior points miss short hair tips adjacent to the
            // fixed neck. Constrain the real silhouette as well to prevent
            // the interpolant overshooting between moving and fixed anchors.
            for(int y=1;y<hair.height()-1;y+=2)for(int x=1;x<hair.width()-1;++x){
                if(hair.constScanLine(y)[x]<=200)continue;
                if(hair.constScanLine(y)[x-1]>200 && hair.constScanLine(y)[x+1]>200 && hair.constScanLine(y-1)[x]>200 && hair.constScanLine(y+1)[x]>200)continue;
                add({double(x)*(size.width()-1)/hair.width(),double(y)*(size.height()-1)/hair.height()},gain);
            }
        }
        QRectF bounds;for(const auto& p:source)bounds=bounds.isNull()?QRectF(p,QSizeF(1,1)):bounds.united(QRectF(p,QSizeF(1,1)));
        bounds=bounds.adjusted(-scale_,-scale_,scale_,scale_);
        for(int i=0;i<=10;++i){const double t=i/10.;add({bounds.left()+bounds.width()*t,bounds.top()},{});add({bounds.left()+bounds.width()*t,bounds.bottom()},{});add({bounds.left(),bounds.top()+bounds.height()*t},{});add({bounds.right(),bounds.top()+bounds.height()*t},{});}
        for(std::size_t i=0;i<source.size();++i)centers_.push_back((source[i]+delta[i])/scale_);
        const int n=int(centers_.size());cv::Mat matrix(n,n,CV_64F),rhs(n,2,CV_64F);
        for(int i=0;i<n;++i){for(int j=0;j<n;++j)matrix.at<double>(i,j)=basis(QLineF(centers_[i],centers_[j]).length())+(i==j?1e-8:0);rhs.at<double>(i,0)=-delta[i].x()/scale_;rhs.at<double>(i,1)=-delta[i].y()/scale_;}
        if(!cv::solve(matrix,rhs,weights_,cv::DECOMP_CHOLESKY) || !cv::checkRange(weights_))throw EditError("颈长约束求解失败，请检查定位");
        bounds=bounds.adjusted(-scale_*2,-scale_*2,scale_*2,scale_*2);bounds_={bounds.x()/(size.width()-1),bounds.y()/(size.height()-1),bounds.width()/(size.width()-1),bounds.height()/(size.height()-1)};
    }
    QRectF bounds()const{return bounds_;}
    double maximum_displacement()const{return maximum_;}
    QPointF displacement(QPointF q)const{const QPointF p(q.x()*(size_.width()-1)/scale_,q.y()*(size_.height()-1)/scale_);QPointF d;
        for(int i=0;i<int(centers_.size());++i){const auto v=p-centers_[i];const double r2=QPointF::dotProduct(v,v);if(r2>=4)continue;const double w=basis(std::sqrt(r2));d+=QPointF(weights_.at<double>(i,0),weights_.at<double>(i,1))*w;}
        return {d.x()*scale_/(size_.width()-1),d.y()*scale_/(size_.height()-1)};
    }
private:
    static double basis(double d){const double r=d/2;if(r>=1)return 0;const double t=1-r;return t*t*t*t*(1+4*r);}
    QSize size_;double scale_{},maximum_{};QRectF bounds_;std::vector<QPointF> centers_;cv::Mat weights_;
};
}
