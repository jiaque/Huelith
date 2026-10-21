#include "portrait_contour.h"
#include "huelith/editing/project.h"
#include "huelith/editing/portrait_landmarks.h"
#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>

namespace huelith::editing {
namespace {
constexpr double support = .65;
double basis(double distance,double radius=support) {
    const double r = distance / radius;
    if (r >= 1) return 0;
    const double t = 1-r;
    return t*t*t*t*(1+4*r); // Compact C2 radial basis, zero slope at support edge.
}
double length(QPointF p) { return std::hypot(p.x(),p.y()); }
QPointF landmark(const QJsonArray& points, int index) {
    const auto a=points[index].toArray();
    return {a[0].toDouble(),a[1].toDouble()};
}
}
QPointF PortraitContour::local(QPointF p) const {
    const QPointF pixel(p.x()*(size_.width()-1),p.y()*(size_.height()-1));
    const auto delta=(pixel-origin_)/scale_;
    return {QPointF::dotProduct(delta,axis_),QPointF::dotProduct(delta,vertical_)};
}
PortraitContour::PortraitContour(QSize size,const QJsonArray& points,
                                 double width,double cheek,double jaw,double chin,
                                 std::array<PortraitEyeTarget,2> eyes,const QImage& obstacles,PortraitNoseTarget nose,PortraitMouthTarget mouth,std::array<PortraitBrowTarget,2> brows,PortraitProportionTarget proportions,PortraitOutlineTarget outline,PortraitHeadTarget headTarget,std::array<double,2> malar,double cheekFill,std::array<double,2> lift,PortraitForeheadLift foreheadLift,PortraitLocalSkinLift skinLift,std::array<double,2> midfaceLift):size_(size) {
    if(!headTarget.manual.isEmpty()){
        support_=std::clamp(headTarget.manual["transition"].toDouble(.65),.65,1.5);
        // A hand-drawn head can reach the frame edge. Whole-head motion needs
        // a wider stationary frame transition than a local facial adjustment;
        // squeezing that inverse displacement into .08 face widths folds it.
        edge_fade_=.35;
    }
    double active=std::abs(width)+std::abs(cheek)+std::abs(jaw)+std::abs(chin);
    for(const auto& eye:eyes)active+=std::abs(eye.size)+std::abs(eye.spacing)+std::abs(eye.height)+std::abs(eye.tilt)+std::abs(eye.length)+std::abs(eye.innerCorner)+std::abs(eye.outerCorner)+std::abs(eye.aperture)+std::abs(eye.upperLift)+std::abs(eye.lowerLid)+std::abs(eye.outerLift);
    active+=std::abs(nose.width)+std::abs(nose.length)+std::abs(nose.size)+std::abs(nose.rootWidth)+std::abs(nose.bridgeWidth)+std::abs(nose.tipWidth);
    active+=std::abs(mouth.size)+std::abs(mouth.thickness)+std::abs(mouth.smile[0])+std::abs(mouth.smile[1]);
    active+=std::abs(mouth.width)+std::abs(mouth.height)+std::abs(mouth.offset)+std::abs(mouth.tilt)+std::abs(mouth.upper)+std::abs(mouth.lower)+std::abs(mouth.cupid);
    for(const auto& brow:brows)active+=std::abs(brow.height)+std::abs(brow.tilt)+std::abs(brow.length)+std::abs(brow.spacing)+std::abs(brow.headSpacing)+std::abs(brow.archHeight)+std::abs(brow.archPosition)+std::abs(brow.thickness);
    active+=std::abs(proportions.midface)+std::abs(proportions.philtrum)+std::abs(proportions.lowerface);
    active+=std::abs(outline.chinWidth)+std::abs(outline.smooth)+std::abs(outline.jawAngle)+std::abs(outline.temple[0])+std::abs(outline.temple[1]);
    active+=std::abs(headTarget.size)+std::abs(headTarget.roll);
    active+=std::abs(headTarget.forehead)+std::abs(headTarget.crown)+std::abs(headTarget.hairline);
    active+=std::abs(headTarget.hairVolume);
    if(points.size()>=66)for(int side=0;side<2;++side)for(int j=0;j<=8;++j){
        const auto point=points[side==0?16-j:j].toArray();
        if(point.size()<2 || (point.size()>2 && point[2].toDouble()<.3)){lift[side]=0;break;}
    }
    active+=std::abs(malar[0])+std::abs(malar[1])+std::abs(cheekFill)+std::abs(lift[0])+std::abs(lift[1])+std::abs(foreheadLift.amount)+std::abs(skinLift.amount[0])+std::abs(skinLift.amount[1])+std::abs(skinLift.amount[2])+std::abs(skinLift.amount[3])+std::abs(midfaceLift[0])+std::abs(midfaceLift[1]);
    if (active<1e-10) return;
    if (points.size()<66 || size.width()<2 || size.height()<2) return;
    auto pixel=[&](int i){auto p=landmark(points,i);return QPointF(p.x()*(size.width()-1),p.y()*(size.height()-1));};
    QPointF leftEye,rightEye;
    for(int i=36;i<42;++i) leftEye+=pixel(i)/6.;
    for(int i=42;i<48;++i) rightEye+=pixel(i)/6.;
    const auto eyeDelta=rightEye-leftEye;
    const double eyeDistance=length(eyeDelta);
    if(eyeDistance<2) return;
    axis_=eyeDelta/eyeDistance;vertical_={-axis_.y(),axis_.x()};
    origin_=(leftEye+rightEye)*.5;
    scale_=length(pixel(16)-pixel(0));
    if(scale_<eyeDistance || scale_<4) return;
    if(foreheadLift.amount>0 && !foreheadLift.skin.isNull()){
        bool reliable=true;double browTop=1;for(int i=17;i<=26;++i){const auto a=points[i].toArray();if(a.size()>2 && a[2].toDouble()<.3)reliable=false;browTop=std::min(browTop,local(landmark(points,i)).y());}
        if(reliable){
            constexpr int n=192;foreheadBounds_=QRectF(-.45,browTop-.62,.90,.60);cv::Mat supportMask(n,n,CV_8U,cv::Scalar(0));
            const auto skin=foreheadLift.skin.convertToFormat(QImage::Format_Grayscale8);
            const auto readMask=[](const QImage& mask,QPointF p){if(mask.isNull() || p.x()<0 || p.y()<0 || p.x()>1 || p.y()>1)return 0;return int(mask.constScanLine(std::min(mask.height()-1,int(p.y()*mask.height())))[std::min(mask.width()-1,int(p.x()*mask.width()))]);};
            for(int y=1;y<n-1;++y)for(int x=1;x<n-1;++x){const QPointF p(foreheadBounds_.left()+x*foreheadBounds_.width()/(n-1),foreheadBounds_.top()+y*foreheadBounds_.height()/(n-1));const auto location=origin_+(axis_*p.x()+vertical_*p.y())*scale_;const QPointF normalized(location.x()/(size.width()-1),location.y()/(size.height()-1));
                if(readMask(skin,normalized)>220 && (obstacles.isNull() || readMask(obstacles,normalized)<24))supportMask.at<uchar>(y,x)=255;
            }
            cv::Mat distances;cv::distanceTransform(supportMask,distances,cv::DIST_L2,cv::DIST_MASK_PRECISE);foreheadWeights_.create(n,n,CV_32F);
            for(int y=0;y<n;++y)for(int x=0;x<n;++x){const double t=std::clamp(distances.at<float>(y,x)*.60/(n-1)/.06,0.,1.);foreheadWeights_.at<float>(y,x)=float(t*t*(3-2*t));}
            foreheadAmount_=cv::norm(foreheadWeights_,cv::NORM_INF)>.05?std::clamp(foreheadLift.amount,0.,1.):0;
        }
    }
    for(int side=0;side<4;++side)if(skinLift.amount[side]>0 && !skinLift.skin.isNull()){
        const bool oral=side>=2;const int outer=oral?(side%2==0?62:58):(side==0?45:36);const auto anchor=points[outer].toArray();if(anchor.size()>2 && anchor[2].toDouble()<.3)continue;
        const auto corner=local(landmark(points,outer));constexpr int n=128;
        auto& box=skinLiftBounds_[side];box=QRectF(corner.x()+(side%2==0?.020:-.180),corner.y()-(oral?.04:.10),.16,oral?.24:.22);
        const auto skin=skinLift.skin.convertToFormat(QImage::Format_Grayscale8);cv::Mat mask(n,n,CV_8U,cv::Scalar(0));
        const auto read=[](const QImage& image,QPointF q){if(image.isNull() || q.x()<0 || q.y()<0 || q.x()>=1 || q.y()>=1)return 0;return int(image.constScanLine(int(q.y()*image.height()))[int(q.x()*image.width())]);};
        for(int y=1;y<n-1;++y)for(int x=1;x<n-1;++x){const QPointF q(box.left()+x*box.width()/(n-1),box.top()+y*box.height()/(n-1));bool protectedFeature=false;
            for(int i=17;i<(oral?66:48);++i)if((oral || i<27 || i>=36) && length(q-local(landmark(points,i)))<.028){protectedFeature=true;break;}if(protectedFeature)continue;
            const auto location=origin_+(axis_*q.x()+vertical_*q.y())*scale_;const QPointF normalized(location.x()/(size.width()-1),location.y()/(size.height()-1));
            if(read(skin,normalized)>220 && (obstacles.isNull() || read(obstacles,normalized)<24))mask.at<uchar>(y,x)=255;
        }
        cv::Mat distance;cv::distanceTransform(mask,distance,cv::DIST_L2,cv::DIST_MASK_PRECISE);skinLiftWeights_[side].create(n,n,CV_32F);
        for(int y=0;y<n;++y)for(int x=0;x<n;++x){const double t=std::clamp(distance.at<float>(y,x)*box.width()/(n-1)/.035,0.,1.);skinLiftWeights_[side].at<float>(y,x)=float(t*t*(3-2*t));}
        skinLiftAmount_[side]=cv::norm(skinLiftWeights_[side],cv::NORM_INF)>.05?std::clamp(skinLift.amount[side],0.,1.):0;
    }
    // Local skin flows already incorporate the entire obstacle mask. They do
    // not need a dense landmark system populated with every mask pixel. This
    // also makes a completely blocked region a cheap, exact identity result.
    const double localActivity=std::abs(foreheadLift.amount)+std::abs(skinLift.amount[0])+std::abs(skinLift.amount[1])+std::abs(skinLift.amount[2])+std::abs(skinLift.amount[3]);
    if(active-localActivity<1e-10){
        const auto include=[&](const QRectF& box){QPolygonF polygon;for(auto p:{box.topLeft(),box.topRight(),box.bottomRight(),box.bottomLeft()}){const auto location=origin_+(axis_*p.x()+vertical_*p.y())*scale_;polygon<<QPointF(location.x()/(size.width()-1),location.y()/(size.height()-1));}bounds_=bounds_.isNull()?polygon.boundingRect():bounds_.united(polygon.boundingRect());};
        if(foreheadAmount_>0)include(foreheadBounds_);for(int side=0;side<4;++side)if(skinLiftAmount_[side]>0)include(skinLiftBounds_[side]);return;
    }
    const double chinHeight=QPointF::dotProduct(pixel(8)-pixel(portrait_landmarks::lower_lip),vertical_)/scale_;
    const double rootY=local(landmark(points,27)).y(),baseY=local(landmark(points,33)).y();
    const double chinY=local(landmark(points,8)).y(),lipY=local(landmark(points,50)).y();
    const double midDelta=std::max(.01,baseY-rootY)*.15*proportions.midface;
    const double lowerDelta=std::max(.01,chinY-baseY)*.15*proportions.lowerface;
    const double philtrumDelta=std::max(.01,lipY-baseY)*.25*proportions.philtrum;
    std::vector<QPointF> sources,targets;
    for(int i=0;i<points.size();++i){
        const auto p=local(landmark(points,i));QPointF move;
        if(i>0 && i<16){
            const double side=i<8?-1.:i>8?1.:0.;
            const int j=std::min(i,16-i);
            // Separate contour objectives: whole cheek arc, cheekbone arc,
            // mandibular arc and vertical chin length. Inner features stay put.
            const double cheekWeight=j<=5?std::pow(std::sin(3.141592653589793*j/6.),4):0.;
            const double jawWeight=j>=3 && j<=7?std::pow(std::sin(3.141592653589793*(j-2)/6.),2):0.;
            const double chinWeight=i>=5 && i<=11?std::pow(std::cos((i-8)*3.141592653589793/8.),2):0.;
            move.setX(side*(.055*cheek*cheekWeight+.065*jaw*jawWeight));
            // Only the subzygomatic arc between the cheekbone and jaw corner
            // moves. Ear roots, zygomatic endpoints, jaw corners and chin stay
            // fixed. An inward indentation receives more of the limited fill.
            if((j==2 || j==3) && cheekFill!=0){
                const auto upper=local(landmark(points,i<8?1:15));
                const auto lower=local(landmark(points,i<8?4:12));
                const double t=std::clamp((p.y()-upper.y())/std::max(.01,lower.y()-upper.y()),0.,1.);
                const double chordX=upper.x()+(lower.x()-upper.x())*t;
                const double deficit=std::max(0.,side*(chordX-p.x()));
                const double distance=std::min(.025,.010+deficit*.6);
                move.rx()+=side*distance*std::clamp(cheekFill,-1.,1.);
            }
            // Lift follows the lateral contour, tapering to zero at each ear
            // root and at the chin tip. It does not translate the inner face.
            const double liftAmount=std::clamp(lift[i<8?1:0],0.,1.);
            const double liftWeight=std::pow(std::sin(3.141592653589793*j/8.),2);
            const double chinSideWeight=i>=6 && i<=10?std::pow(std::cos((i-8)*3.141592653589793/6.),2):0.;
            move.rx()+=side*.035*outline.chinWidth*chinSideWeight;
            move.setY(.30*std::max(.05,chinHeight)*chin*chinWeight);
            move.ry()+=midDelta*std::clamp((p.y()-rootY)/std::max(.01,baseY-rootY),0.,1.)
                +lowerDelta*std::clamp((p.y()-baseY)/std::max(.01,chinY-baseY),0.,1.);
            move+=QPointF(side*.010,-.035)*liftAmount*liftWeight;
        }
        sources.push_back(p);targets.push_back(p+move);
    }
    if(outline.jawAngle!=0){
        for(int side=0;side<2;++side){
            const int corner=side==0?4:12;const int direction=side==0?1:-1;
            const double angle=direction*.15*outline.jawAngle;
            for(int step=1;step<=2;++step){
                const int i=corner+direction*step;const auto d=sources[i]-sources[corner];
                const QPointF rotated(d.x()*std::cos(angle)-d.y()*std::sin(angle),d.x()*std::sin(angle)+d.y()*std::cos(angle));
                targets[i]+=(rotated-d)*(step==1?1.:.45);
            }
        }
    }
    if(outline.smooth>0){
        // Minimize contour bending with positional fidelity. Ear roots, jaw
        // corners and chin tip stay exact; never mirror the two face halves.
        const std::array<int,12> free{1,2,3,5,6,7,9,10,11,13,14,15};
        cv::Mat d=cv::Mat::zeros(15,17,CV_64F),positions(17,2,CV_64F);
        for(int i=0;i<17;++i){positions.at<double>(i,0)=sources[i].x();positions.at<double>(i,1)=sources[i].y();}
        for(int i=1;i<16;++i){
            const double a=std::max(.005,length(sources[i]-sources[i-1])),b=std::max(.005,length(sources[i+1]-sources[i]));
            d.at<double>(i-1,i-1)=1/a;d.at<double>(i-1,i)=-1/a-1/b;d.at<double>(i-1,i+1)=1/b;
        }
        cv::Mat df(15,12,CV_64F);for(int i=0;i<12;++i)d.col(free[i]).copyTo(df.col(i));
        const cv::Mat h=cv::Mat::eye(12,12,CV_64F)+.002*df.t()*df;
        const cv::Mat rhs=-.002*df.t()*d*positions;cv::Mat delta;
        if(!cv::solve(h,rhs,delta,cv::DECOMP_CHOLESKY))throw EditError("轮廓平滑求解失败");
        double maximum=0.;for(int i=0;i<12;++i)maximum=std::max(maximum,std::hypot(delta.at<double>(i,0),delta.at<double>(i,1)));
        const double gain=outline.smooth*std::min(1.,.0125/std::max(1e-9,maximum));
        for(int i=0;i<12;++i)targets[free[i]]+=QPointF(delta.at<double>(i,0),delta.at<double>(i,1))*gain;
    }
    const double nasalWidth=length(sources[35]-sources[31]);
    const double nasalLength=std::max(.01,sources[33].y()-sources[27].y());
    // Translate each alar/nostril side together, instead of squashing the whole
    // nose. Length moves the tip/base as an island with a graded bridge segment.
    for(int i=28;i<=35;++i){
        const double bridgeWeight=i<30?(i-27)/3.:1.;
        const double side=i==31 || i==32?-1.:i==34 || i==35?1.:0.;
        targets[i]+=QPointF(side*nasalWidth*(.15*nose.width+.10*nose.size),(nasalLength*(.12*nose.length+.10*nose.size)+midDelta)*bridgeWeight);
    }
    if(nose.rootWidth!=0 || nose.bridgeWidth!=0 || nose.tipWidth!=0){
        // Width changes transport narrow strips beside the nasal axis. The
        // centreline, alar contours and nostril anchors remain independent.
        for(int i=27;i<=30;++i){
            const double amount=i==27?nose.rootWidth:i==28?.35*nose.rootWidth+.6*nose.bridgeWidth:i==29?nose.bridgeWidth:nose.tipWidth;
            const double halfWidth=nasalWidth*(i==30?.20:.25);
            for(double side:{-1.,1.})for(double band:{1.,2.}){
                const QPointF offset(side*halfWidth*band,0);
                const auto p=sources[i]+offset;
                sources.push_back(p);
                targets.push_back(targets[i]+offset+QPointF(band==1.?side*halfWidth*.20*amount:0.,0));
            }
        }
    }
    for(int i=31;i<35;++i){
        sources.push_back((sources[i]+sources[i+1])*.5);
        targets.push_back((targets[i]+targets[i+1])*.5);
    }
    const auto mouthCenter=(sources[58]+sources[62])*.5;
    const double mouthProportionShift=midDelta+philtrumDelta+
        lowerDelta*std::clamp((mouthCenter.y()-baseY)/std::max(.01,chinY-baseY),0.,1.);
    const double mouthWidth=length(sources[62]-sources[58]);
    const double upperInner=(sources[59].y()+sources[60].y()+sources[61].y())/3.;
    const double lowerInner=(sources[63].y()+sources[64].y()+sources[65].y())/3.;
    for(int i:portrait_landmarks::outer_lip){
        targets[i]+=(sources[i]-mouthCenter)*(.12*mouth.size);
        targets[i].rx()+=(sources[i].x()-mouthCenter.x())*.15*mouth.width;
        if(i>=48 && i<=52)targets[i].ry()+=(sources[i].y()-upperInner)*.30*(mouth.thickness+mouth.upper);
        if(i>=53 && i<=57)targets[i].ry()+=(sources[i].y()-lowerInner)*.30*(mouth.thickness+mouth.lower);
        // Cupid's bow changes only the three upper central contour points.
        // Its range comes from available upper-lip thickness, not face size.
        if(i>=49 && i<=51)targets[i].ry()+=(i==50?1.:-1.)*
            std::abs(sources[i].y()-upperInner)*.20*mouth.cupid;
        for(int side=0;side<2;++side){const int corner=side==0?62:58;const double direction=side==0?1.:-1.;
            const double falloff=std::max(0.,1-std::abs(sources[i].x()-sources[corner].x())/std::max(.001,mouthWidth*.38));
            targets[i]+=QPointF(direction*.025,-.065)*(mouthWidth*mouth.smile[side]*falloff*falloff);
        }
    }
    // Anchor the oral cavity itself. Lip thickness must not stretch the teeth.
    QPointF cavity;for(int i:{59,60,61,63,64,65})cavity+=sources[i]/6.;
    const auto mouthIsland=[&](QPointF p){
        const auto d=p-mouthCenter;const double angle=.12*mouth.tilt;
        return mouthCenter+QPointF(d.x()*std::cos(angle)-d.y()*std::sin(angle),d.x()*std::sin(angle)+d.y()*std::cos(angle))
            +QPointF(mouthWidth*.10*mouth.offset,-mouthWidth*.10*mouth.height+mouthProportionShift);
    };
    for(int i=48;i<=65;++i)targets[i]=mouthIsland(targets[i]);
    sources.push_back(cavity);targets.push_back(mouthIsland(cavity));
    // Additional cavity anchors preserve teeth as a rigid island when moving
    // or tilting the mouth; thickness/width edits do not scale that island.
    for(int i:{59,60,61,63,64,65}){
        const auto p=(sources[i]+cavity)*.5;
        sources.push_back(p);targets.push_back(mouthIsland(p));
    }
    std::vector<QPointF> obstaclePoints;
    if(!obstacles.isNull()){
        const int step=std::max(2,int(scale_/size.width()*obstacles.width()*.0125));
        for(int y=0;y<obstacles.height();y+=step)for(int x=0;x<obstacles.width();x+=step){
            QPointF sum;int count=0;
            for(int j=y;j<std::min(y+step,obstacles.height());++j)for(int i=x;i<std::min(x+step,obstacles.width());++i)
                if(obstacles.constScanLine(j)[i]>127){sum+=QPointF((i+.5)/obstacles.width(),(j+.5)/obstacles.height());++count;}
            if(count)obstaclePoints.push_back(local(sum/count));
        }
    }
    for(int side=0;side<2;++side){
        const auto& eye=eyes[side];const int first=side==0?42:36;
        QPointF center;for(int i=first;i<first+6;++i)center+=sources[i]/6.;
        const double direction=side==0?1.:-1.;
        double angle=-direction*.15*eye.tilt;const double eyeScale=1+.12*eye.size;
        QPointF shift(direction*.035*eye.spacing,-.035*eye.height);
        // A fixed frame leaves finite room for an eyelid to move. Bound motion
        // by measured clearance, not an arbitrary face-wide gain. This is a
        // fixed per-person range, so the slider remains continuous and monotone.
        double clearance=1.,rotationLimit=1.;
        for(int i=first;i<first+6;++i)for(auto obstacle:obstaclePoints){
            const double distance=length(obstacle-sources[i]);
            clearance=std::min(clearance,distance);
            rotationLimit=std::min(rotationLimit,.35*distance/std::max(.001,length(sources[i]-center)));
        }
        if(!obstaclePoints.empty()){
            shift*=std::min(1.,clearance*.35/.05);
            angle=-direction*std::min(.15,rotationLimit)*eye.tilt;
        }
        const auto eyeAxis=sources[first+3]-sources[first];const double span=length(eyeAxis);
        const auto eyeTangent=eyeAxis/std::max(.001,span);
        const QPointF eyeNormal(-eyeTangent.y(),eyeTangent.x());
        const double opening=std::abs(QPointF::dotProduct((sources[first+4]+sources[first+5]-sources[first+1]-sources[first+2])*.5,eyeNormal));
        if((eye.aperture!=0 || eye.upperLift!=0 || eye.lowerLid!=0) && opening<span*.08)throw EditError("眼睛闭合或眼睑定位不足，不能可靠调整眼高，请修正定位或换用手动液化");
        const auto target=[&](QPointF p){
            const double u=QPointF::dotProduct(p-center,eyeTangent);
            const double t=std::clamp((std::abs(u)/std::max(.001,span)-.16)/.34,0.,1.);
            const double gain=std::min(1.,clearance*.35/std::max(.001,span*.06));
            // Extend the two ends along the eye axis. The central eye region
            // stays unchanged, and no vertical scale changes the eye opening.
            const bool inner=side==0?u<0:u>0;
            const double extension=eye.length+(inner?eye.innerCorner:eye.outerCorner);
            p+=eyeTangent*((u<0?-1.:1.)*span*.06*extension*t*t*(3-2*t)*gain);
            const double v=QPointF::dotProduct(p-center,eyeNormal);
            const double band=std::clamp((std::abs(v)/std::max(.001,opening)-.20)/.30,0.,1.);
            const double cornerFade=std::clamp((.5-std::abs(u)/std::max(.001,span))/.20,0.,1.);
            p+=eyeNormal*(v*(.20*eye.aperture+.25*(v<0?eye.upperLift:eye.lowerLid))*band*band*(3-2*band)*cornerFade*gain);
            if(!inner)p-=eyeNormal*(span*.06*eye.outerLift*t*t*(3-2*t)*gain);
            const auto d=p-center;return center+shift+
                QPointF(d.x()*std::cos(angle)-d.y()*std::sin(angle),d.x()*std::sin(angle)+d.y()*std::cos(angle))*eyeScale;
        };
        for(int i=first;i<first+6;++i)targets[i]=target(sources[i]);
        // Interior anchors make the visible eye an affine island rather than
        // pushing six isolated eyelid points and distorting the iris between.
        sources.push_back(center);targets.push_back(target(center));
        for(int i=first;i<first+6;++i){
            const auto inside=(sources[i]+center)*.5;
            const auto edge=(sources[i]+sources[first+(i-first+1)%6])*.5;
            sources.push_back(inside);targets.push_back(target(inside));
            sources.push_back(edge);targets.push_back(target(edge));
        }
    }
    for(int side=0;side<2;++side){
        const auto& brow=brows[side];const int first=side==0?22:17;
        const int head=side==0?22:21,tail=side==0?26:17;const double direction=side==0?1.:-1.;
        const auto origin=sources[head],axis=sources[tail]-origin;
        const double browLength=length(axis);if(browLength<.01)continue;
        const auto tangent=axis/browLength;const QPointF normal(-tangent.y(),tangent.x());
        browPivot_[side]=origin;browAxis_[side]=tangent;browLength_[side]=browLength;browAngle_[side]=-direction*.20*brow.tilt;
        const double angle=0;
        const auto unconstrainedTarget=[&](QPointF p,double t){
            const auto d=p-origin;
            const auto rotated=QPointF(d.x()*std::cos(angle)-d.y()*std::sin(angle),d.x()*std::sin(angle)+d.y()*std::cos(angle));
            const double smooth=t*t*(3-2*t),peak=std::pow(std::sin(3.141592653589793*t),2),headWeight=std::pow(std::max(0.,1-t/.6),2);
            return origin+rotated+QPointF(direction*.022*(brow.spacing+brow.headSpacing*headWeight),-.035*brow.height-browLength*.18*brow.archHeight*peak)
                +tangent*(browLength*(.18*brow.length*smooth+.14*brow.archPosition*peak));
        };
        // Preserve a transition band between the moving brow and a fixed frame.
        // One gain for the whole curve keeps translations and rotations coherent.
        double motionGain=1.;
        for(int i=first;i<first+5;++i){
            // Use the full single-control envelope rather than the current value:
            // increasing the slider must not flatten into a value-dependent clamp.
            const double motion=std::max(.035,browLength*.18);
            for(auto obstacle:obstaclePoints)
                motionGain=std::min(motionGain,.2*std::max(0.,length(obstacle-sources[i])-.006)/motion);
        }
        const auto target=[&](QPointF p,double t){return p+(unconstrainedTarget(p,t)-p)*motionGain;};
        for(int i=first;i<first+5;++i){
            const double t=std::abs(i-head)/4.;targets[i]=target(sources[i],t);
            // Transport a narrow strip with the curve. Position/length changes
            // must not turn into radial eyebrow inflation or move the eyelid.
            if(brow.thickness!=0){
                const auto segment=sources[std::min(first+4,i+1)]-sources[std::max(first,i-1)];
                const QPointF localNormal(-segment.y()/length(segment),segment.x()/length(segment));
                // The brow centreline and length stay fixed; transport the
                // material on either side along its own curve normal.
                for(double offset:{-.035,-.015,.015,.035}){
                    const auto p=sources[i]+localNormal*offset;
                    const double envelope=std::abs(offset)<.02?1.:.25;
                    sources.push_back(p);targets.push_back(target(p,t)+localNormal*(offset*.4*brow.thickness*envelope*motionGain));
                }
            }else for(double offset:{-.006,.006}){const auto p=sources[i]+normal*offset;sources.push_back(p);targets.push_back(target(p,t));}
            if(i<first+4){const auto p=(sources[i]+sources[i+1])*.5;const double mid=std::abs(i+.5-head)/4.;sources.push_back(p);targets.push_back(target(p,mid));}
        }
    }
    for(auto anchor:obstaclePoints){sources.push_back(anchor);targets.push_back(anchor);}
    if(headTarget.active()){
        if(headTarget.region.isNull())throw EditError("头部范围尚未识别，请重新识别人物后调整头部大小");
        const auto anchor=headTarget.manual["anchor"].toArray();
        const auto pivot=anchor.size()==2?local(QPointF(anchor[0].toDouble(),anchor[1].toDouble())):sources[8]+QPointF(0,.03);const double factor=1+.08*headTarget.size;
        const double headAngle=headTarget.roll*.065;
        const auto headAffine=[&](QPointF p){const auto d=(p-pivot)*factor;
            const auto moved=pivot+QPointF(d.x()*std::cos(headAngle)-d.y()*std::sin(headAngle),d.x()*std::sin(headAngle)+d.y()*std::cos(headAngle));
            double weight=1;
            if(!headTarget.manual.isEmpty()){
                const auto smooth=[](double t){t=std::clamp(t,0.,1.);return t*t*(3-2*t);};
                weight*=1-smooth((p.y()-pivot.y())/.22)*(1-smooth((std::abs(p.x()-pivot.x())-.22)/.22));
                for(int i:{11,12})if(headTarget.pose.size()==33){const auto a=headTarget.pose[i].toArray();if(a.size()<3 || a[2].toDouble()<.5)continue;const auto shoulder=local({a[0].toDouble(),a[1].toDouble()});if(shoulder.y()<pivot.y()+.08)continue;weight*=smooth(length(p-shoulder)/.22);}
            }
            return p+(moved-p)*weight;};
        const bool scalpActive=headTarget.forehead!=0 || headTarget.crown!=0 || headTarget.hairline!=0 || headTarget.hairVolume!=0;
        std::vector<QPointF> hairline;double baseline=0,top=0;
        if(scalpActive){
            if(!headTarget.scalp["valid"].toBool())throw EditError(headTarget.scalp["reason"].toString(QStringLiteral("发际线定位不足，请重新识别")));
            if(headTarget.scalp["cropped"].toBool())throw EditError("头部外沿被裁切，不能可靠自动调整额头或颅顶");
            const auto read=[&](const QJsonValue& v){const auto a=v.toArray();return local(QPointF(a[0].toDouble(),a[1].toDouble()));};
            for(const auto& v:headTarget.scalp["hairline"].toArray())hairline.push_back(read(v));
            if(hairline.size()<2)throw EditError("发际线定位不足");
            std::sort(hairline.begin(),hairline.end(),[](auto a,auto b){return a.x()<b.x();});
            baseline=read(headTarget.scalp["baseline"]).y();top=read(headTarget.scalp["top"]).y();
        }
        cv::Mat hairCoverage;
        if(headTarget.hairVolume!=0 && !headTarget.hairRegion.isNull()){
            const auto& image=headTarget.hairRegion;
            cv::Mat input(image.height(),image.width(),CV_8UC1,const_cast<uchar*>(image.constBits()),image.bytesPerLine());
            const int radius=std::max(1,int(scale_/size.width()*image.width()*.18));
            cv::dilate(input,hairCoverage,cv::getStructuringElement(cv::MORPH_ELLIPSE,{radius*2+1,radius*2+1}));
            cv::GaussianBlur(hairCoverage,hairCoverage,{0,0},std::max(.6,radius*.3));
        }
        if(headTarget.hairVolume!=0 && !hairCoverage.empty()){
            // Integrate the inverse stationary velocity directly. Refitting an
            // integrated flow with RBF can introduce overshoot and foldovers.
            constexpr int n=256;
            volumeBounds_=QRectF(-1.,top-.4,2.,pivot.y()+.3-(top-.4));
            const double sx=volumeBounds_.width()/(n-1),sy=volumeBounds_.height()/(n-1);
            cv::Mat velocity(n,n,CV_32FC2),mapX(n,n,CV_32F),mapY(n,n,CV_32F);
            std::vector<cv::Point2f> face;
            for(int i=0;i<17;++i)face.emplace_back(float(sources[i].x()),float(sources[i].y()));
            for(auto i=hairline.rbegin();i!=hairline.rend();++i)face.emplace_back(float(i->x()),float(i->y()));
            std::vector<QPointF> fixed;
            for(auto p:headTarget.background)fixed.push_back(local(p));
            const auto smooth=[](double t){t=std::clamp(t,0.,1.);return t*t*(3-2*t);};
            for(int y=0;y<n;++y)for(int x=0;x<n;++x){
                const QPointF p(volumeBounds_.left()+x*sx,volumeBounds_.top()+y*sy);
                const double distance=-cv::pointPolygonTest(face,cv::Point2f(float(p.x()),float(p.y())),true);
                const auto scalpPixel=origin_+(axis_*p.x()+vertical_*p.y())*scale_;
                const double hx=scalpPixel.x()/(size_.width()-1)*(hairCoverage.cols-1),hy=scalpPixel.y()/(size_.height()-1)*(hairCoverage.rows-1);
                double probability=0.;
                if(hx>=0 && hy>=0 && hx<hairCoverage.cols-1 && hy<hairCoverage.rows-1){
                    const int ix=int(hx),iy=int(hy);const double tx=hx-ix,ty=hy-iy;
                    probability=((1-ty)*((1-tx)*hairCoverage.at<uchar>(iy,ix)+tx*hairCoverage.at<uchar>(iy,ix+1))
                        +ty*((1-tx)*hairCoverage.at<uchar>(iy+1,ix)+tx*hairCoverage.at<uchar>(iy+1,ix+1)))/255.;
                }
                double protection=1.;
                for(auto f:fixed)protection=std::min(protection,smooth((length(p-f)-.04)/.12));
                const double edge=std::min({x*sx,(n-1-x)*sx,y*sy,(n-1-y)*sy});
                const double imageEdge=std::min({scalpPixel.x(),scalpPixel.y(),size_.width()-1-scalpPixel.x(),size_.height()-1-scalpPixel.y()});
                const double weight=smooth(imageEdge/(.15*scale_))*smooth((probability-.35)/.5)*smooth((distance-.012)/.18)
                    *smooth((pivot.y()+.12-p.y())/.18)*smooth(edge/.15)*protection;
                const auto v=(p-QPointF(0,baseline))*(.18*headTarget.hairVolume*weight);
                velocity.at<cv::Vec2f>(y,x)={float(v.x()/sx),float(v.y()/sy)};
                mapX.at<float>(y,x)=float(x);mapY.at<float>(y,x)=float(y);
            }
            cv::Mat sampled;
            for(int iteration=0;iteration<32;++iteration){
                cv::remap(velocity,sampled,mapX,mapY,cv::INTER_LINEAR,cv::BORDER_CONSTANT);
                for(int y=0;y<n;++y)for(int x=0;x<n;++x){
                    const auto v=sampled.at<cv::Vec2f>(y,x);
                    mapX.at<float>(y,x)-=v[0]/32;mapY.at<float>(y,x)-=v[1]/32;
                }
            }
            volumeInverse_.create(n,n,CV_32FC2);
            for(int y=0;y<n;++y)for(int x=0;x<n;++x)
                volumeInverse_.at<cv::Vec2f>(y,x)={float((mapX.at<float>(y,x)-x)*sx),float((mapY.at<float>(y,x)-y)*sy)};
        }
        const auto transform=[&](QPointF p){
            if(scalpActive){
                double lineY=hairline.front().y();
                for(std::size_t i=1;i<hairline.size();++i){
                    const auto a=hairline[i-1],b=hairline[i];const double t=std::clamp((p.x()-a.x())/std::max(.001,b.x()-a.x()),0.,1.);
                    lineY=a.y()+(b.y()-a.y())*t;if(p.x()<=b.x())break;
                }
                const auto smooth=[](double t){t=std::clamp(t,0.,1.);return t*t*(3-2*t);};
                const double foreheadHeight=std::max(.02,baseline-lineY),hairHeight=std::max(.02,lineY-top);
                const double fromBrow=smooth((baseline-p.y())/foreheadHeight),fromTop=smooth((p.y()-top)/hairHeight);
                const double crownWeight=smooth((lineY-p.y())/hairHeight);
                p.ry()+=-.20*foreheadHeight*headTarget.forehead*fromBrow
                    -.18*hairHeight*headTarget.crown*crownWeight
                    +.15*foreheadHeight*headTarget.hairline*fromBrow*fromTop;

            }
            return headAffine(p);
        };
        for(int i=0;i<16;++i){sources.push_back((sources[i]+sources[i+1])*.5);targets.push_back((targets[i]+targets[i+1])*.5);}
        // Scalp edits cannot perturb face targets, even when combined with a
        // contour edit. Only the independent whole-head scale transports them.
        for(auto& p:targets)p=headAffine(p);
        for(auto p:hairline){sources.push_back(p);targets.push_back(transform(p));}
        const int step=std::max(2,int(scale_/size.width()*headTarget.region.width()*.09));
        for(int y=0;y<headTarget.region.height();y+=step)for(int x=0;x<headTarget.region.width();x+=step){
            if(headTarget.region.constScanLine(y)[x]<128)continue;
            const auto p=local(QPointF((x+.5)/headTarget.region.width(),(y+.5)/headTarget.region.height()));
            // Feature constraints already carry other edits. Sampling the outer
            // head and scalp stabilizes hair without pinning those edits away.
            bool nearFeature=false;for(int i=0;i<66;++i)if(length(p-sources[i])<.07){nearFeature=true;break;}
            if(!nearFeature){sources.push_back(p);targets.push_back(transform(p));}
        }
        cv::Mat headMask(headTarget.region.height(),headTarget.region.width(),CV_8UC1,const_cast<uchar*>(headTarget.region.constBits()),headTarget.region.bytesPerLine());
        std::vector<std::vector<cv::Point>> boundaries;cv::findContours(headMask.clone(),boundaries,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_NONE);
        for(const auto& boundary:boundaries){
            const auto stride=std::max<std::size_t>(1,boundary.size()/64);
            for(std::size_t i=0;i<boundary.size();i+=stride){
                const auto p=local(QPointF((boundary[i].x+.5)/headTarget.region.width(),(boundary[i].y+.5)/headTarget.region.height()));
                bool nearFeature=false;for(int j=0;j<66;++j)if(length(p-sources[j])<.07){nearFeature=true;break;}
                if(!nearFeature){sources.push_back(p);targets.push_back(transform(p));}
            }
        }
        sources.push_back(pivot);targets.push_back(pivot);
        if(headTarget.pose.size()==33)for(int i:{11,12}){
            const auto a=headTarget.pose[i].toArray();if(a.size()<3 || a[2].toDouble()<.5)continue;
            const auto p=local(QPointF(a[0].toDouble(),a[1].toDouble()));
            // A pose model may report confident "shoulders" inside a cropped
            // face. Such points cannot be fixed anchors for the moving head.
            if(p.y()<pivot.y()+.08)continue;
            sources.push_back(p);targets.push_back(p);
        }
        for(auto normalized:headTarget.background){const auto p=local(normalized);sources.push_back(p);targets.push_back(p);}
    }
    // Malar targets sit inside the cheek, rather than scaling a circular
    // neighborhood. Existing eyelid, alar, lip and facial outline targets remain
    // authoritative. Interpolate their other edits before adding local lift.
    for(int side=0;side<2;++side){
        const double amount=std::clamp(malar[side],0.,1.),middle=std::clamp(midfaceLift[side],0.,1.);if(amount==0 && middle==0)continue;
        const int lower=side==0?46:41,alar=side==0?35:31,edge=side==0?13:3,lip=side==0?62:58;
        bool reliable=true;for(int i:{lower,alar,edge,lip}){const auto a=points[i].toArray();if(a.size()>2 && a[2].toDouble()<.3)reliable=false;}if(!reliable)continue;
        const auto blend=[&](const std::vector<QPointF>& p,double a,double b,double c,double d){return p[lower]*a+p[alar]*b+p[edge]*c+p[lip]*d;};
        QPointF shift((side==0?1.:-1.)*.010*amount,-.012*amount-.020*middle);if(length(shift)>.030)shift*=.030/length(shift);
        for(const auto& weights:std::array<std::array<double,4>,3>{{{.38,.25,.27,.10},{.28,.18,.38,.16},{.24,.30,.25,.21}}}){
            const auto from=blend(sources,weights[0],weights[1],weights[2],weights[3]);
            const auto to=blend(targets,weights[0],weights[1],weights[2],weights[3]);
            bool near=false;for(const auto& p:sources)if(length(p-from)<.025){near=true;break;}
            if(!near){sources.push_back(from);targets.push_back(to+shift);}
        }
    }
    for(int side=0;side<2;++side){
        const double amount=std::clamp(midfaceLift[side],0.,1.);if(amount==0)continue;
        const int lower=side==0?46:41,alar=side==0?35:31,edge=side==0?13:3,lip=side==0?62:58;
        bool reliable=true;for(int i:{lower,alar,edge,lip}){const auto a=points[i].toArray();if(a.size()>2 && a[2].toDouble()<.3)reliable=false;}if(!reliable)continue;
        for(const auto& weights:std::array<std::array<double,4>,3>{{{.12,.18,.40,.30},{.08,.25,.32,.35},{.08,.12,.46,.34}}}){
            const auto blend=[&](const std::vector<QPointF>& p){return p[lower]*weights[0]+p[alar]*weights[1]+p[edge]*weights[2]+p[lip]*weights[3];};const auto from=blend(sources),to=blend(targets);
            bool near=false;for(const auto& p:sources)if(length(p-from)<.025){near=true;break;}if(!near){sources.push_back(from);targets.push_back(to+QPointF(0,-.018*amount));}
        }
    }
    if(midfaceLift[0]!=0 || midfaceLift[1]!=0){
        // Pin an area of the nose and lips, not only their outline vertices.
        // Their own explicit controls still define these interpolated targets.
        for(const auto& island:std::array<std::vector<int>,2>{{{27,28,29,30,31,32,33,34,35},{48,49,50,51,52,62,53,54,55,56,57,58}}}){
            QPointF sourceCenter,targetCenter;for(int i:island){sourceCenter+=sources[i]/double(island.size());targetCenter+=targets[i]/double(island.size());}
            for(int i:island)for(double t:{.25,.5,.75}){const auto from=sourceCenter+(sources[i]-sourceCenter)*t,to=targetCenter+(targets[i]-targetCenter)*t;bool near=false;for(const auto& p:sources)if(length(p-from)<.006){near=true;break;}if(!near){sources.push_back(from);targets.push_back(to);}}
        }
    }
    if(lift[0]!=0 || lift[1]!=0){
        // Fixed collars outside the jaw separate the moving skin contour from
        // the neck and clothing. Sample segments as well as detector vertices.
        for(int i=3;i<13;++i){const auto a=sources[i],b=sources[i+1],tangent=b-a;const double span=length(tangent);if(span<.001)continue;
            QPointF normal(-tangent.y()/span,tangent.x()/span);if(QPointF::dotProduct(normal,(a+b)*.5)<0)normal=-normal;
            for(int step=0;step<3;++step)for(double offset:{.06,.12}){const auto p=a+(b-a)*(step/3.)+normal*offset;bool near=false;for(const auto& q:sources)if(length(p-q)<.012){near=true;break;}if(!near){sources.push_back(p);targets.push_back(p);}}
        }
        // Protect a region, not only isolated jaw-offset curves: without this
        // stationary lattice a compact interpolant can bulge between curves.
        for(int row=0;row<=8;++row)for(int column=0;column<=12;++column){
            const QPointF p(sources[8].x()-.48+column*.08,sources[8].y()+.06+row*.08);
            bool near=false;for(const auto& q:sources)if(length(p-q)<.018){near=true;break;}
            if(!near){sources.push_back(p);targets.push_back(p);}
        }
    }
    if(width!=0){
        // Full facial width is a continuous lateral contour edit, not a jaw
        // bulge. Include temples and forehead sides; central features retain
        // their own geometry. Scalp above the hairline tapers back to identity.
        const auto smooth=[](double t){t=std::clamp(t,0.,1.);return t*t*(3-2*t);};
        double hairlineY=-.40;
        const auto line=headTarget.scalp["hairline"].toArray();
        if(!line.isEmpty()){hairlineY=0;for(const auto& v:line){const auto a=v.toArray();hairlineY+=local(QPointF(a[0].toDouble(),a[1].toDouble())).y()/line.size();}}
        const double upper=hairlineY-.18;
        const auto move=[&](QPointF p){
            const double lateral=smooth((std::abs(p.x())-.20)/.28)*(1-smooth((std::abs(p.x())-.58)/.30));
            const double vertical=smooth((p.y()-upper)/.18)*(1-smooth((p.y()-chinY+.06)/.20));
            double protection=1.;for(auto o:obstaclePoints)protection=std::min(protection,smooth(length(p-o)/.07));
            return QPointF(.16*width*p.x()*lateral*vertical*protection,0);
        };
        // Apply the same field to existing head/feature targets so simultaneous
        // head or scalp edits cannot pin the upper-face width back to zero.
        for(std::size_t i=0;i<targets.size();++i)targets[i]+=move(sources[i]);
        // Active head/scalp edits already supply a dense transported lattice.
        // Adding identity-based forehead anchors would fight that transform.
        if(!headTarget.active())for(int row=0;row<=8;++row)for(double side:{-1.,1.})for(double x:{.32,.44,.54,.68}){
            const QPointF p(side*x,upper+(0.14-upper)*row/8.);
            bool near=false;for(auto q:sources)if(length(p-q)<.035){near=true;break;}
            if(!near){sources.push_back(p);targets.push_back(p+move(p));}
        }
    }
    if(outline.temple[0]!=0 || outline.temple[1]!=0){
        // Fill the lateral hollow between brow tail and upper cheek. Transport
        // its boundary and neighboring hair together; eyelids, brows and lower
        // jaw remain stationary. Work in the roll-normalized anatomical frame.
        const auto smooth=[](double t){t=std::clamp(t,0.,1.);return t*t*(3-2*t);};
        const auto move=[&](QPointF p){
            const int side=p.x()>0?0:1;const double sign=side==0?1.:-1.;
            const auto brow=sources[side==0?26:17];
            const double cy=brow.y()-.035;
            double weight=smooth((std::abs(p.x())-std::abs(brow.x())-.025)/.08)*
                (1-smooth((std::abs(p.x())-.53)/.20))*
                (1-smooth(std::abs(p.y()-cy)/.20));
            for(auto o:obstaclePoints)weight*=smooth(length(p-o)/.055);
            return QPointF(sign*.025*std::clamp(outline.temple[side],0.,1.)*weight,0);
        };
        for(std::size_t i=0;i<targets.size();++i)targets[i]+=move(sources[i]);
        for(int side=0;side<2;++side)for(int row=-3;row<=3;++row)for(double x:{.35,.45,.55,.65}){
            const auto brow=sources[side==0?26:17];const QPointF p((side==0?1.:-1.)*x,brow.y()-.035+row*.065);
            bool near=false;for(auto q:sources)if(length(q-p)<.03){near=true;break;}
            if(!near){sources.push_back(p);targets.push_back(p+move(p));}
        }
    }
    // Stationary outer ring limits influence on hair, neck and background.
    // It is outside the editable contour, so shrinking still has a smooth band.
    double minX=0,maxX=0,minY=0,maxY=0;
    for(auto p:sources){minX=std::min(minX,p.x());maxX=std::max(maxX,p.x());minY=std::min(minY,p.y());maxY=std::max(maxY,p.y());}
    minX-=.35;maxX+=.35;minY-=.35;maxY+=.35;
    for(int i=0;i<12;++i){const double t=i/12.;
        for(auto p:{QPointF(minX+(maxX-minX)*t,minY),QPointF(maxX,minY+(maxY-minY)*t),
                    QPointF(maxX-(maxX-minX)*t,maxY),QPointF(minX,maxY-(maxY-minY)*t)}){
            sources.push_back(p);targets.push_back(p);
        }
    }
    // Fit target -> source directly; negating a forward displacement is not
    // an inverse warp. Duplicate detector points are consolidated before solve.
    std::vector<QPointF> offsets;
    for(std::size_t i=0;i<targets.size();++i){
        bool duplicate=false;
        for(auto p:centers_)if(length(p-targets[i])<1e-5){duplicate=true;break;}
        if(!duplicate){centers_.push_back(targets[i]);offsets.push_back(sources[i]-targets[i]);}
    }
    const int count=int(centers_.size());
    cv::Mat matrix(count,count,CV_64F),rhs(count,2,CV_64F);
    for(int i=0;i<count;++i){
        rhs.at<double>(i,0)=offsets[i].x();rhs.at<double>(i,1)=offsets[i].y();
        for(int j=0;j<count;++j)matrix.at<double>(i,j)=basis(length(centers_[i]-centers_[j]),support_)+(i==j?1e-9:0);
    }
    if(!cv::solve(matrix,rhs,coefficients_,cv::DECOMP_CHOLESKY) || !cv::checkRange(coefficients_))
        throw EditError(QStringLiteral("面部轮廓约束求解失败，请修正人物定位"));
    for(auto center:centers_){
        const auto centerPixel=origin_+(axis_*center.x()+vertical_*center.y())*scale_;
        const double radius=support_*scale_;
        const QRectF box((centerPixel.x()-radius)/(size.width()-1),(centerPixel.y()-radius)/(size.height()-1),
                         2*radius/(size.width()-1),2*radius/(size.height()-1));
        bounds_=bounds_.isNull()?box:bounds_.united(box);
    }
}
QPointF PortraitContour::displacement(QPointF normalized) const {
    if(empty())return {};
    const auto p=local(normalized);QPointF d;
    for(int i=0;i<int(centers_.size());++i){const auto delta=p-centers_[i];
        const double squared=QPointF::dotProduct(delta,delta);if(squared>=support_*support_)continue;
        const double weight=basis(std::sqrt(squared),support_);
        d+=QPointF(coefficients_.at<double>(i,0),coefficients_.at<double>(i,1))*weight;}
    if(!volumeInverse_.empty()){
        const auto q=p+d;
        const double x=(q.x()-volumeBounds_.left())/volumeBounds_.width()*(volumeInverse_.cols-1);
        const double y=(q.y()-volumeBounds_.top())/volumeBounds_.height()*(volumeInverse_.rows-1);
        if(x>=0 && y>=0 && x<volumeInverse_.cols-1 && y<volumeInverse_.rows-1){
            const int ix=int(x),iy=int(y);const float tx=float(x-ix),ty=float(y-iy);
            const auto v=(1-ty)*((1-tx)*volumeInverse_.at<cv::Vec2f>(iy,ix)+tx*volumeInverse_.at<cv::Vec2f>(iy,ix+1))
                +ty*((1-tx)*volumeInverse_.at<cv::Vec2f>(iy+1,ix)+tx*volumeInverse_.at<cv::Vec2f>(iy+1,ix+1));
            d+=QPointF(v[0],v[1]);
        }
    }
    if(foreheadAmount_>0 && !foreheadWeights_.empty()){
        const auto weight=[&](QPointF q){const double x=(q.x()-foreheadBounds_.left())/foreheadBounds_.width()*(foreheadWeights_.cols-1),y=(q.y()-foreheadBounds_.top())/foreheadBounds_.height()*(foreheadWeights_.rows-1);if(x<0 || y<0 || x>=foreheadWeights_.cols-1 || y>=foreheadWeights_.rows-1)return 0.;const int ix=int(x),iy=int(y);const double tx=x-ix,ty=y-iy;return (1-ty)*((1-tx)*foreheadWeights_.at<float>(iy,ix)+tx*foreheadWeights_.at<float>(iy,ix+1))+ty*((1-tx)*foreheadWeights_.at<float>(iy+1,ix)+tx*foreheadWeights_.at<float>(iy+1,ix+1));};
        // Integrate the inverse stationary velocity. A bounded support that
        // vanishes smoothly at skin/hair/brow borders keeps those borders fixed.
        const double step=.020*foreheadAmount_/8;
        for(int i=0;i<8;++i){const auto q=p+d;d.ry()+=step*weight(q+QPointF(0,step*weight(q)*.5));}
    }
    for(int side=0;side<4;++side)if(skinLiftAmount_[side]>0 && !skinLiftWeights_[side].empty()){
        const auto& field=skinLiftWeights_[side];const auto& box=skinLiftBounds_[side];
        const auto weight=[&](QPointF q){const double x=(q.x()-box.left())/box.width()*(field.cols-1),y=(q.y()-box.top())/box.height()*(field.rows-1);if(x<0 || y<0 || x>=field.cols-1 || y>=field.rows-1)return 0.;const int ix=int(x),iy=int(y);const double tx=x-ix,ty=y-iy;return (1-ty)*((1-tx)*field.at<float>(iy,ix)+tx*field.at<float>(iy,ix+1))+ty*((1-tx)*field.at<float>(iy+1,ix)+tx*field.at<float>(iy+1,ix+1));};
        const QPointF step=QPointF((side%2==0?-1.:1.)*(side>=2?.003:.006),side>=2?.010:.012)*(skinLiftAmount_[side]/8);
        for(int i=0;i<8;++i){const auto q=p+d;d+=step*weight(q+step*(weight(q)*.5));}
    }
    // Integrate a smooth rotation field around the brow head. This avoids
    // forcing a narrow rotated strip through stationary RBF constraints.
    for(int side=0;side<2;++side)if(browAngle_[side]!=0){
        const auto axis=browAxis_[side];const QPointF normal(-axis.y(),axis.x());const double width=browLength_[side];
        const auto velocity=[&](QPointF q){const auto r=q-browPivot_[side];const double u=(QPointF::dotProduct(r,axis)/width-.5)/.95,v=QPointF::dotProduct(r,normal)/(width*.42);const double radius=std::hypot(u,v),t=std::clamp((1-radius)/.45,0.,1.);return QPointF(r.y(),-r.x())*(browAngle_[side]*t*t*(3-2*t));};
        for(int step=0;step<16;++step){const auto q=p+d;d+=velocity(q+velocity(q)/32.)/16.;}
    }
    const double edge=std::min({normalized.x()*(size_.width()-1),(1-normalized.x())*(size_.width()-1),normalized.y()*(size_.height()-1),(1-normalized.y())*(size_.height()-1)});
    const double t=std::clamp(edge/(scale_*edge_fade_),0.,1.);
    const auto pixel=(axis_*d.x()+vertical_*d.y())*(scale_*t*t*(3-2*t));
    return {pixel.x()/(size_.width()-1),pixel.y()/(size_.height()-1)};
}
}

