#pragma once
#include "huelith/editing/project.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QLineF>
#include <QRectF>
#include <QImage>
#include <opencv2/core.hpp>
#include <algorithm>
#include <cmath>
#include <vector>
namespace huelith::editing {
// Shoulder-width targets, with an immobile neck and tapered arm transport.
class PortraitShoulderWidth {
public:
    PortraitShoulderWidth(QSize size,const QJsonObject& person,double leftAmount,double rightAmount,double leftElevation=0,double rightElevation=0):size_(size){
        const auto settings=person["settings"].toObject();const auto pose=settings["pose"].toArray();
        if(pose.size()!=33 || person["bodyAmbiguous"].toBool())throw EditError("肩部定位不足，请修正人物定位");
        const auto point=[&](int i){
            const auto p=pose[i].toArray();
            if(p.size()<3 || p[2].toDouble()<.6){
                const auto side=i%2?QStringLiteral("人物左侧"):QStringLiteral("人物右侧");
                const auto joint=i<13?QStringLiteral("肩部"):i<15?QStringLiteral("肘部"):i<17?QStringLiteral("手腕"):QStringLiteral("髋部");
                throw EditError(side+joint+QStringLiteral("定位不足；可在定位修正中拖动对应关键点，或仅调整可靠一侧；被遮挡部位请使用手动液化"));
            }
            return QPointF(p[0].toDouble()*(size.width()-1),p[1].toDouble()*(size.height()-1));
        };
        const auto left=point(11),right=point(12),center=(left+right)*.5;
        scale_=QLineF(left,right).length();if(scale_<20)throw EditError("肩部投影过短，请修正定位");
        const auto axis=(right-left)/scale_;const QPointF vertical(-axis.y(),axis.x());
        const QPointF widthShifts[2]={axis*(-scale_*.06*leftAmount),axis*(scale_*.06*rightAmount)};
        const QPointF elevationShifts[2]={{0,scale_*.06*leftElevation},{0,scale_*.06*rightElevation}};
        const QPointF shifts[2]={widthShifts[0]+elevationShifts[0],widthShifts[1]+elevationShifts[1]};
        maximum_=std::max(QLineF({},shifts[0]).length(),QLineF({},shifts[1]).length());
        const auto hair=QImage::fromData(QByteArray::fromBase64(settings["automaticMasks"].toObject()["hair"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
        const auto hairAt=[&](QPointF p){if(hair.isNull())return false;const int x=int(p.x()/(size.width()-1)*hair.width()),y=int(p.y()/(size.height()-1)*hair.height());return x>=0 && y>=0 && x<hair.width() && y<hair.height() && hair.constScanLine(y)[x]>200;};
        std::vector<QPointF> source,delta;
        const auto add=[&](QPointF p,QPointF d){
            if(p.x()<0 || p.y()<0 || p.x()>size.width()-1 || p.y()>size.height()-1)return;
            if(hairAt(p))d={};
            if(p.x()+d.x()<0 || p.y()+d.y()<0 || p.x()+d.x()>size.width()-1 || p.y()+d.y()>size.height()-1)throw EditError("肩部调整会超出照片，请减小强度或扩展画布");
            for(std::size_t i=0;i<source.size();++i)if(QLineF(source[i],p).length()<scale_*.002){if(QLineF(delta[i],d).length()>scale_*.003)throw EditError("肩颈与手臂约束重叠，请修正定位");return;}
            source.push_back(p);delta.push_back(d);
        };
        const auto smooth=[](double t){return t*t*(3-2*t);};
        // Keep the neck root and central chest unchanged.
        for(int y=-1;y<=2;++y)for(int x=-1;x<=1;++x)add(center+axis*(scale_*.12*x)+vertical*(scale_*.10*y),{});
        for(const auto& value:person["landmarks"].toArray()){const auto p=value.toArray();add({p[0].toDouble()*(size.width()-1),p[1].toDouble()*(size.height()-1)},{});}
        // The projected shoulder midpoint is not the neck root in a turned
        // torso. Protect visible neck skin below the actual chin as well.
        const auto landmarks=person["landmarks"].toArray(),face=person["face"].toArray();
        if(landmarks.size()==66 && face.size()==4){
            const auto lm=[&](int i){const auto p=landmarks[i].toArray();return QPointF(p[0].toDouble()*(size.width()-1),p[1].toDouble()*(size.height()-1));};
            QPointF eyes;for(int i=36;i<48;++i)eyes+=lm(i);eyes/=12.;
            auto down=lm(8)-eyes;const double length=QLineF({},down).length();
            const auto skin=QImage::fromData(QByteArray::fromBase64(settings["automaticMasks"].toObject()["skin"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
            if(length>1 && !skin.isNull()){down/=length;const QPointF across(-down.y(),down.x());const double fw=face[2].toDouble()*(size.width()-1),fh=face[3].toDouble()*(size.height()-1);
                for(int row=0;row<=18;++row)for(int col=-16;col<=16;++col){const auto p=lm(8)+down*(fh*.04*row)+across*(fw*.03*col);const int x=int(p.x()/(size.width()-1)*skin.width()),y=int(p.y()/(size.height()-1)*skin.height());
                    if(x>=0 && y>=0 && x<skin.width() && y<skin.height() && skin.constScanLine(y)[x]>180)add(p,{});
                }
                // Interior grid points alone leave the neck silhouette free
                // between the last sample and the collar. Constrain that
                // measured boundary too, rather than increasing grid density.
                for(int y=1;y<skin.height()-1;y+=2)for(int x=1;x<skin.width()-1;++x){
                    if(skin.constScanLine(y)[x]<=180)continue;
                    if(skin.constScanLine(y)[x-1]>180 && skin.constScanLine(y)[x+1]>180 && skin.constScanLine(y-1)[x]>180 && skin.constScanLine(y+1)[x]>180)continue;
                    const QPointF p((x+.5)*(size.width()-1)/skin.width(),(y+.5)*(size.height()-1)/skin.height());const auto offset=p-lm(8);
                    const double along=QPointF::dotProduct(offset,down),side=QPointF::dotProduct(offset,across);
                    if(along>=0 && along<=fh*.72 && std::abs(side)<=fw*.48)add(p,{});
                }
            }
        }
        for(int side=0;side<2;++side){
            const auto shoulder=point(11+side);
            // Shoulder cap and its clothing translate together, not inflate.
            for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x)add(shoulder+axis*(scale_*.0275*x)+vertical*(scale_*.0275*y),shifts[side]);
            if(QPointF::dotProduct(shifts[side],shifts[side])<1e-12){
                // A hidden, unedited arm must not disable the visible side.
                // Keep reliable opposite-side anchors without inventing joints.
                for(int i:{13+side,15+side,23+side})if(pose[i].toArray().size()>=3 && pose[i].toArray()[2].toDouble()>=.6)add(point(i),{});
                for(int segment=0;segment<2;++segment){const int a=11+side+segment*2,b=a+2;
                    if(pose[a].toArray()[2].toDouble()<.6 || pose[b].toArray()[2].toDouble()<.6)continue;
                    const auto first=point(a),last=point(b),v=last-first;const double length=QLineF(first,last).length();if(length<1)continue;const QPointF normal(-v.y()/length,v.x()/length);
                    for(int row=1;row<=12;++row)for(int col=-2;col<=2;++col)add(first+v*(row/12.)+normal*(scale_*.0175*col),{});
                }
                continue;
            }
            const auto elbow=point(13+side),wrist=point(15+side),hip=point(23+side);
            for(int segment=0;segment<2;++segment){const auto first=segment==0?shoulder:elbow,last=segment==0?elbow:wrist,v=last-first;const double length=QLineF(first,last).length();if(length<scale_*.08)throw EditError("手臂投影过短，请修正定位");const QPointF normal(-v.y()/length,v.x()/length);
                for(int row=1;row<=12;++row){const double t=row/12.;const double weight=segment==0?1-.5*smooth(t):.5*(1-smooth(t));
                    for(int col=-2;col<=2;++col)add(first+v*t+normal*(scale_*.0175*col),widthShifts[side]*weight+elevationShifts[side]*(segment==0?1-smooth(t):0));
                }
            }
            for(int row=1;row<=4;++row){const double t=row/4.;add(shoulder+(hip-shoulder)*t,shifts[side]*(1-smooth(t)));}
            add(hip,{});
        }
        if(!hair.isNull()){
            const int step=std::max(5,int(scale_*.035));
            for(int y=0;y<size.height();y+=step)for(int x=0;x<size.width();x+=step)if(hairAt({double(x),double(y)}))add({double(x),double(y)},{});
            const int rowStep=std::max(1,int(double(step)*hair.height()/size.height()));
            for(int y=0;y<hair.height();y+=rowStep)for(int x=0;x<hair.width();++x){
                if(hair.constScanLine(y)[x]<=200)continue;
                if(x==0 || x==hair.width()-1 || hair.constScanLine(y)[x-1]<=200 || hair.constScanLine(y)[x+1]<=200)
                    add({(x+.5)*(size.width()-1)/hair.width(),(y+.5)*(size.height()-1)/hair.height()},{});
            }
        }
        QRectF bounds;for(const auto& p:source)bounds=bounds.isNull()?QRectF(p,QSizeF(1,1)):bounds.united(QRectF(p,QSizeF(1,1)));
        bounds=bounds.adjusted(-scale_,-scale_,scale_,scale_);
        for(int i=0;i<=10;++i){const double t=i/10.;add({bounds.left()+bounds.width()*t,bounds.top()},{});add({bounds.left()+bounds.width()*t,bounds.bottom()},{});add({bounds.left(),bounds.top()+bounds.height()*t},{});add({bounds.right(),bounds.top()+bounds.height()*t},{});}
        for(std::size_t i=0;i<source.size();++i)centers_.push_back((source[i]+delta[i])/scale_);
        const int n=int(centers_.size());cv::Mat matrix(n,n,CV_64F),rhs(n,2,CV_64F);
        for(int i=0;i<n;++i){for(int j=0;j<n;++j)matrix.at<double>(i,j)=basis(QLineF(centers_[i],centers_[j]).length())+(i==j?1e-8:0);rhs.at<double>(i,0)=-delta[i].x()/scale_;rhs.at<double>(i,1)=-delta[i].y()/scale_;}
        if(!cv::solve(matrix,rhs,weights_,cv::DECOMP_CHOLESKY) || !cv::checkRange(weights_))throw EditError("肩宽约束求解失败，请检查定位");
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
