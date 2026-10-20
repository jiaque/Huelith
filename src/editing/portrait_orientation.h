#pragma once
#include "huelith/editing/project.h"
#include <QJsonArray>
#include <QRectF>
#include <QImage>
#include <opencv2/imgproc.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>

namespace huelith::editing {
// Bounded weak-perspective reprojection of the measured visible facial surface.
// The fixed outer ring and positive triangle Jacobians prohibit holes/folds;
// no unseen texture is generated and the source image is sampled only once.
class PortraitOrientation {
public:
    PortraitOrientation(QSize size,const QJsonObject& surface,double yaw,double pitch,const QImage& headRegion,int version=1) {
        if(yaw==0 && pitch==0)return;
        const auto points=surface["points"].toArray();
        if(surface["version"].toInt()!=1 || points.size()!=468 || surface["confidence"].toDouble()<.9)
            throw EditError(QStringLiteral("朝向定位不足，请重新识别；其他面部调整仍可使用"));
        const double aspect=double(size.height()-1)/std::max(1,size.width()-1);
        std::vector<cv::Vec3d> xyz;
        for(const auto& value:points){const auto a=value.toArray();
            if(a.size()!=3)throw EditError("朝向曲面数据损坏");
            cv::Vec3d q(a[0].toDouble(),a[1].toDouble()*aspect,a[2].toDouble());
            for(double v:q.val)if(!std::isfinite(v))throw EditError("朝向曲面坐标无效");
            xyz.push_back(q);
        }
        cv::Vec3d horizontal=xyz[263]-xyz[33];horizontal[2]=0;
        const double eyeSpan=cv::norm(horizontal);
        if(eyeSpan<.003)throw EditError("人脸尺寸不足，无法可靠调整朝向");
        horizontal/=eyeSpan;const cv::Vec3d vertical(-horizontal[1],horizontal[0],0);
        if(headRegion.isNull())throw EditError("头部范围不足，请重新识别后调整朝向");
        const auto mask=headRegion.convertToFormat(QImage::Format_Grayscale8);
        cv::Mat maskView(mask.height(),mask.width(),CV_8UC1,const_cast<uchar*>(mask.constBits()),mask.bytesPerLine());
        std::vector<cv::Point> pixels;cv::findNonZero(maskView>127,pixels);
        if(pixels.empty())throw EditError("未找到完整头部范围");
        const auto box=cv::boundingRect(pixels);
        double minx=1,miny=aspect,maxx=0,maxy=0;
        for(const auto& q:xyz){minx=std::min(minx,q[0]);maxx=std::max(maxx,q[0]);miny=std::min(miny,q[1]);maxy=std::max(maxy,q[1]);}
        const double width=maxx-minx,height=maxy-miny;
        if(width<eyeSpan || height<eyeSpan)throw EditError("朝向曲面范围无效");
        // Preserve the legacy pivot for old recipes. New recipes use a
        // centered surface; version 3 supplies a separate whole-head yaw profile.
        auto pivot=xyz[152];pivot[2]+=.30*width;
        if(version>=2){
            // Center the visible surface rather than rotating it about a remote
            // point behind the chin. That old depth offset dominated the result
            // with a common translation of every facial feature.
            pivot={0,0,0};
            for(const auto& point:xyz)pivot+=point;
            pivot/=double(xyz.size());
        }
        const double cy=std::cos(yaw*.104719755),sy=std::sin(yaw*.104719755);
        const double cp=std::cos(pitch*.104719755),sp=std::sin(pitch*.104719755);
        const double margin=.38*width;
        bounds_={double(box.x)/mask.width()-margin,double(box.y)/mask.height()-margin/aspect,
                 double(box.width)/mask.width()+2*margin,double(box.height)/mask.height()+2*margin/aspect};
        cv::Mat outside;cv::distanceTransform(maskView<128,outside,cv::DIST_L2,cv::DIST_MASK_PRECISE);
        const auto smooth=[](double t){t=std::clamp(t,0.,1.);return t*t*(3-2*t);};
        std::vector<QPointF> source,target;
        const double bandwidth=width*(version>=2?.045:.12);
        for(int row=1;row<40;++row)for(int col=1;col<40;++col){
            const QPointF q(bounds_.x()+bounds_.width()*col/40.,bounds_.y()+bounds_.height()*row/40.);
            const int mx=int(q.x()*mask.width()),my=int(q.y()*mask.height());
            double weight=0;
            if(mx>=0 && my>=0 && mx<mask.width() && my<mask.height())
                weight=1-smooth(outside.at<float>(my,mx)/(margin*mask.width()));
            weight*=1-smooth((q.y()*aspect-xyz[152][1])/(width*.28));
            double nearest=1e9;
            for(const auto& v:xyz){const double dx=q.x()-v[0],dy=q.y()*aspect-v[1];nearest=std::min(nearest,dx*dx+dy*dy);}
            double z=0,sum=0;
            for(const auto& v:xyz){const double dx=q.x()-v[0],dy=q.y()*aspect-v[1];
                const double w=std::exp(-(dx*dx+dy*dy-nearest)/(2*bandwidth*bandwidth));z+=v[2]*w;sum+=w;}
            const cv::Vec3d point(q.x(),q.y()*aspect,z/sum),d=point-pivot;
            const double x=d.dot(horizontal),y=d.dot(vertical),depth=d[2];
            const double rx=cy*x+sy*depth,rz=-sy*x+cy*depth,ry=cp*y-sp*rz;
            const double rotatedDepth=sp*y+cp*rz;
            // Source coordinates are already projected. Use the ratio of old
            // and new camera distances so zero rotation is exactly identity,
            // while the near and far sides change scale in opposite directions.
            const double camera=width*4.;
            const double perspective=version>=2?(camera+depth)/(camera+rotatedDepth):1.;
            auto projected=pivot+(horizontal*rx+vertical*ry)*perspective;
            if(version>=3 && yaw!=0){
                // Yaw needs a skull surface, not nearest-face depth extended
                // vertically into hair. Use a continuous lateral head profile;
                // retain only a bounded facial relief so the nose turns without
                // bending the entire face midline into a crescent.
                const double lateral=std::clamp(x/(width*.65),-1.,1.);
                const double skullDepth=width*(-.32+.18*lateral*lateral);
                const double relief=.15*std::clamp(depth,-width*.2,width*.2);
                const double yawDepth=skullDepth+relief;
                const double turnX=cy*x+sy*yawDepth;
                const double turnZ=-sy*x+cy*yawDepth;
                const double turnScale=(camera+yawDepth)/(camera+turnZ);
                // Compose yaw with the accepted pitch projection. In
                // particular yaw==0 retains version 2 bit-for-bit.
                const double pitchY=cp*y-sp*depth;
                const double pitchZ=sp*y+cp*depth;
                const double pitchScale=(camera+depth)/(camera+pitchZ);
                projected=pivot+(horizontal*x+vertical*pitchY)*pitchScale
                    +horizontal*(turnX*turnScale-x)+vertical*(y*(turnScale-1));
            }
            source.push_back(q);target.push_back(q+QPointF(projected[0]-q.x(),projected[1]/aspect-q.y())*weight);
        }
        const auto local=[&](QPointF p){return cv::Point2f(float((p.x()-bounds_.x())/bounds_.width()*255),float((p.y()-bounds_.y())/bounds_.height()*255));};
        std::vector<cv::Point2f> a,b;
        cv::Subdiv2D subdiv(cv::Rect(-1,-1,258,258));
        for(std::size_t i=0;i<source.size();++i){const auto p=local(source[i]);
            if(p.x<=0 || p.y<=0 || p.x>=255 || p.y>=255)throw EditError("朝向定位超出可用范围");
            // Coincident lip/eyelid samples have different inferred depths.
            // A subpixel sliver cannot represent a visible surface reliably;
            // use a spaced control mesh instead of triangulating those slivers.
            bool duplicate=false;for(const auto& saved:a)if(cv::norm(saved-p)<3.){duplicate=true;break;}
            if(duplicate)continue;
            a.push_back(p);b.push_back(local(target[i]));subdiv.insert(p);
        }
        for(int side=0;side<4;++side)for(int n=0;n<8;++n){const float t=255.f*n/8;
            const cv::Point2f p=side==0?cv::Point2f(t,0):side==1?cv::Point2f(255,t):side==2?cv::Point2f(255-t,255):cv::Point2f(0,255-t);
            a.push_back(p);b.push_back(p);subdiv.insert(p);
        }
        std::vector<cv::Vec6f> triangles;subdiv.getTriangleList(triangles);
        flow_=cv::Mat::zeros(256,256,CV_32FC2);
        const auto cross=[](cv::Point2f u,cv::Point2f v){return double(u.x)*v.y-double(u.y)*v.x;};
        for(const auto& triangle:triangles){std::array<int,3> ids{};bool valid=true;
            for(int c=0;c<3;++c){double best=.02;ids[c]=-1;const cv::Point2f p(triangle[c*2],triangle[c*2+1]);
                for(int i=0;i<int(a.size());++i){const double distance=cv::norm(a[i]-p);if(distance<best){best=distance;ids[c]=i;}}
                valid=valid && ids[c]>=0;
            }
            if(!valid)continue;
            const auto a0=a[ids[0]],a1=a[ids[1]],a2=a[ids[2]],b0=b[ids[0]],b1=b[ids[1]],b2=b[ids[2]];
            const double before=cross(a1-a0,a2-a0),after=cross(b1-b0,b2-b0);
            if(std::abs(before)<.001)continue;
            if(after/before<.12 || after/before>8)
                throw EditError(QStringLiteral("该朝向会露出原片没有的区域，请减小幅度；已保留上一结果"));
            const int x0=std::max(0,int(std::floor(std::min({b0.x,b1.x,b2.x})))),x1=std::min(255,int(std::ceil(std::max({b0.x,b1.x,b2.x}))));
            const int y0=std::max(0,int(std::floor(std::min({b0.y,b1.y,b2.y})))),y1=std::min(255,int(std::ceil(std::max({b0.y,b1.y,b2.y}))));
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){const cv::Point2f q{float(x),float(y)};
                const double u=cross(q-b0,b2-b0)/after,v=cross(b1-b0,q-b0)/after;
                if(u<-.00001 || v<-.00001 || u+v>1.00001)continue;
                const auto delta=a0+(a1-a0)*float(u)+(a2-a0)*float(v)-q;
                flow_.at<cv::Vec2f>(y,x)={float(delta.x*bounds_.width()/255),float(delta.y*bounds_.height()/255)};
            }
        }
    }
    bool empty() const {return flow_.empty();}
    QRectF bounds() const {return bounds_;}
    QPointF displacement(QPointF q) const {
        if(empty() || !bounds_.contains(q))return {};
        const double x=(q.x()-bounds_.x())/bounds_.width()*255,y=(q.y()-bounds_.y())/bounds_.height()*255;
        const int ix=std::clamp(int(x),0,254),iy=std::clamp(int(y),0,254);const float fx=float(x-ix),fy=float(y-iy);
        const auto v=(flow_.at<cv::Vec2f>(iy,ix)*(1-fx)+flow_.at<cv::Vec2f>(iy,ix+1)*fx)*(1-fy)+(flow_.at<cv::Vec2f>(iy+1,ix)*(1-fx)+flow_.at<cv::Vec2f>(iy+1,ix+1)*fx)*fy;
        const double edge=std::min({q.x(),1-q.x(),q.y(),1-q.y()});
        const double t=std::clamp(edge/(std::min(bounds_.width(),bounds_.height())*.04),0.,1.);
        return {v[0]*t*t*(3-2*t),v[1]*t*t*(3-2*t)};
    }
private:
    QRectF bounds_;cv::Mat flow_;
};
}
