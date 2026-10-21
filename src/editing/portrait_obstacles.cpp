#include "huelith/editing/portrait_effects.h"
#include "huelith/editing/project.h"
#include <QJsonArray>
#include <QPainter>
#include <opencv2/imgproc.hpp>

namespace huelith::editing {
QJsonObject portrait_scalp_geometry(const QJsonObject& person) {
    const auto masks=person["settings"].toObject()["automaticMasks"].toObject();
    const auto decode=[&](const char* key){return QImage::fromData(QByteArray::fromBase64(masks[key].toString().toLatin1())).convertToFormat(QImage::Format_Grayscale8);};
    const auto hair=decode("hair"),skin=decode("skin");const auto points=person["landmarks"].toArray();
    if(hair.isNull() || skin.isNull() || points.size()<66)return {{"valid",false},{"reason",QStringLiteral("需要先完成头发与皮肤识别")}};
    const auto canvas=hair.size();const auto skinMask=skin.scaled(canvas,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    auto point=[&](int i){const auto a=points[i].toArray();return cv::Point2f(float(a[0].toDouble()*canvas.width()),float(a[1].toDouble()*canvas.height()));};
    auto axis=(point(42)+point(45)-point(36)-point(39))*.5F;const float eyeSpan=float(cv::norm(axis));
    if(eyeSpan<2)return {{"valid",false},{"reason",QStringLiteral("眼部定位不足，不能建立头部方向")}};
    axis*=1/eyeSpan;const cv::Point2f down(-axis.y,axis.x);
    const auto origin=(point(39)+point(42))*.5F;const float width=float(cv::norm(point(16)-point(0)));
    if(width<16)return {{"valid",false},{"reason",QStringLiteral("头部太小，无法可靠区分发际线")}};
    float baseline=0;for(int i=17;i<27;++i)baseline=std::min(baseline,(point(i)-origin).dot(down)/width);baseline-=.02F;
    auto pixel=[&](double x,double y){return origin+axis*float(x*width)+down*float(y*width);};
    auto sample=[&](const QImage& mask,cv::Point2f p){const int x=cvRound(p.x),y=cvRound(p.y);return x>=0 && y>=0 && x<mask.width() && y<mask.height()?int(mask.constScanLine(y)[x]):0;};
    auto normalized=[&](cv::Point2f p){return QJsonArray{double(p.x)/canvas.width(),double(p.y)/canvas.height()};};
    QJsonArray line;double minimumX=1,maximumX=-1;
    for(int i=0;i<13;++i){const double x=-.30+i*.05;double last=-2;
        for(double y=-1.1;y<baseline-.04;y+=1./width){
            if(sample(hair,pixel(x,y))>100 && sample(skinMask,pixel(x,y+.05))>100)last=y;
        }
        if(last>-1.1){line.append(normalized(pixel(x,last)));minimumX=std::min(minimumX,x);maximumX=std::max(maximumX,x);}
    }
    const bool boundaryDetected=line.size()>=3 && maximumX-minimumX>=.15;
    double top=0;int count=0;bool clipped=false;
    for(int y=0;y<hair.height();++y)for(int x=0;x<hair.width();++x){
        if(hair.constScanLine(y)[x]<160)continue;const auto d=cv::Point2f(float(x),float(y))-origin;
        const double u=d.dot(axis)/width,v=d.dot(down)/width;
        if(std::abs(u)>.65 || v>baseline || v<-1.4)continue;
        top=std::min(top,v);++count;clipped|=x<2 || y<2 || x>=hair.width()-2 || y>=hair.height()-2;
    }
    if(count<width*width*.02)return {{"valid",false},{"reason",QStringLiteral("没有足够可见头发，不能自动调整发际线或颅顶")}};
    if(!boundaryDetected){
        // Crown/volume only need the visible hair extent. Keep the facial
        // boundary stable even when fine blond hairs have a weak skin seam.
        line={};
        const double seam=baseline+(top-baseline)*.3;
        for(int i=0;i<13;++i)line.append(normalized(pixel(-.30+i*.05,seam)));
    }
    return {{"version",3},{"hairlineDetected",boundaryDetected},{"valid",true},{"hairline",line},{"baseline",normalized(pixel(0,baseline))},{"top",normalized(pixel(0,top))},{"cropped",clipped}};
}
QPainterPath portrait_head_outline(const QJsonArray& points,QSize size){
    QPainterPath path;if(points.size()<3)return path;
    const auto at=[&](int i){const auto a=points[(i+points.size())%points.size()].toArray();return QPointF(a[0].toDouble()*size.width(),a[1].toDouble()*size.height());};
    path.moveTo(at(0));for(int i=0;i<points.size();++i)path.cubicTo(at(i)+(at(i+1)-at(i-1))/6,at(i+1)-(at(i+2)-at(i))/6,at(i+1));path.closeSubpath();return path;
}
QImage portrait_head_region(const QJsonObject& person) {
    const auto settings=person["settings"].toObject();
    if(settings["headManualEnabled"].toBool()){
        const auto points=settings["headManual"].toObject()["outline"].toArray();if(points.size()<3)return {};
        const QSize size(768,768);QImage mask(size,QImage::Format_Grayscale8);mask.fill(0);QPainter painter(&mask);painter.setRenderHint(QPainter::Antialiasing);painter.fillPath(portrait_head_outline(points,size),Qt::white);return mask;
    }

    const auto masks=person["settings"].toObject()["automaticMasks"].toObject();
    const auto decode=[&](const char* key){return QImage::fromData(QByteArray::fromBase64(masks[key].toString().toLatin1())).convertToFormat(QImage::Format_Grayscale8);};
    const auto hair=decode("hair"),skin=decode("skin");const auto points=person["landmarks"].toArray();
    if(hair.isNull() || skin.isNull() || points.size()<66)return {};
    const auto canvas=hair.size();const auto skinMask=skin.scaled(canvas,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    auto point=[&](int i){const auto a=points[i].toArray();return cv::Point2f(float(a[0].toDouble()*canvas.width()),float(a[1].toDouble()*canvas.height()));};
    const auto chin=point(8),eye=(point(39)+point(42))*.5F;auto axis=point(16)-point(0);
    const float width=float(cv::norm(axis));if(width<10)return {};axis*=1/width;const cv::Point2f down(-axis.y,axis.x);
    std::vector<cv::Point> region;
    for(int i=0;i<66;++i)region.emplace_back(point(i));
    for(int y=0;y<canvas.height();++y)for(int x=0;x<canvas.width();++x){
        const cv::Point2f delta=cv::Point2f(float(x),float(y))-eye;
        const float horizontal=delta.dot(axis),vertical=delta.dot(down);
        if(std::abs(horizontal)>width*.72F || vertical<-width*1.4F || (cv::Point2f(float(x),float(y))-chin).dot(down)>width*.03F)continue;
        if(hair.constScanLine(y)[x]>160 || skinMask.constScanLine(y)[x]>190)region.emplace_back(x,y);
    }
    // The segmentation supplies the actual scalp extent, including bald skin.
    // Fill feature holes; an affine head island must include eyes and spectacles.
    std::vector<cv::Point> hull;cv::convexHull(region,hull);
    QImage result(canvas,QImage::Format_Grayscale8);result.fill(0);
    cv::Mat mask(canvas.height(),canvas.width(),CV_8UC1,result.bits(),result.bytesPerLine());cv::fillConvexPoly(mask,hull,cv::Scalar(255));
    const auto body=QImage::fromData(QByteArray::fromBase64(person["bodyMask"].toString().toLatin1())).convertToFormat(QImage::Format_Grayscale8).scaled(canvas,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    if(!body.isNull())for(int y=0;y<canvas.height();++y)for(int x=0;x<canvas.width();++x)if(body.constScanLine(y)[x]<32)result.scanLine(y)[x]=0;
    return result;
}
QImage portrait_frame_protection(const QImage& source,const QJsonObject& person) {
    const auto points=person["landmarks"].toArray();
    const QSize canvas=source.size().scaled(768,768,Qt::KeepAspectRatio);
    QImage result(canvas,QImage::Format_Grayscale8);result.fill(0);
    if(points.size()<66)return result;
    const auto image=display_image(source.scaled(canvas,Qt::IgnoreAspectRatio,Qt::SmoothTransformation)).convertToFormat(QImage::Format_RGB888);
    auto point=[&](int i){const auto p=points[i].toArray();return cv::Point2f(float(p[0].toDouble()*canvas.width()),float(p[1].toDouble()*canvas.height()));};
    const float faceWidth=float(cv::norm(point(16)-point(0))),eyeWidth=float(cv::norm(point(45)-point(42)));
    if(eyeWidth<8)return result;
    float top=point(17).y,bottom=point(36).y;
    for(int i=17;i<27;++i)top=std::min(top,point(i).y);
    for(int i=36;i<48;++i)bottom=std::max(bottom,point(i).y);
    const cv::Rect box=cv::Rect(int(std::min(point(0).x,point(16).x)),int(top),int(faceWidth),int(bottom+eyeWidth*.7f-top)) & cv::Rect(0,0,canvas.width(),canvas.height());
    if(box.empty())return result;
    cv::Mat rgb(canvas.height(),canvas.width(),CV_8UC3,const_cast<uchar*>(image.constBits()),image.bytesPerLine());
    cv::Mat gray,low,values,binary,labels,stats,centers;
    cv::cvtColor(rgb(box),gray,cv::COLOR_RGB2GRAY);gray.convertTo(values,CV_32F);
    cv::GaussianBlur(values,low,{},std::max(1.,faceWidth*.012));
    cv::compare(low-values,12,binary,cv::CMP_GT);
    cv::morphologyEx(binary,binary,cv::MORPH_CLOSE,cv::getStructuringElement(cv::MORPH_RECT,{3,3}));
    const int count=cv::connectedComponentsWithStats(binary,labels,stats,centers);
    cv::Mat chosen=cv::Mat::zeros(binary.size(),CV_8U);
    // Require one rim/bridge component spanning BOTH eyes and extending below
    // the eyelids. Brows, eyelashes and a single shadow must not qualify.
    for(int i=1;i<count;++i){
        const int x=stats.at<int>(i,cv::CC_STAT_LEFT),y=stats.at<int>(i,cv::CC_STAT_TOP);
        const int w=stats.at<int>(i,cv::CC_STAT_WIDTH),h=stats.at<int>(i,cv::CC_STAT_HEIGHT);
        if(w<faceWidth*.65 || h<eyeWidth*.38 || stats.at<int>(i,cv::CC_STAT_AREA)>w*h*.65)continue;
        const auto a=point(39),b=point(42);
        if(x+box.x>std::min(a.x,b.x) || x+w+box.x<std::max(a.x,b.x) || y+box.y>std::min(a.y,b.y) || y+h+box.y<bottom+eyeWidth*.15)continue;
        chosen.setTo(255,labels==i);
    }
    // The connected rim may touch lashes: do not freeze the visible eye itself.
    cv::Mat eyes=cv::Mat::zeros(binary.size(),CV_8U);
    for(int first:{36,42}){std::vector<cv::Point> polygon;for(int i=first;i<first+6;++i){const auto p=point(i);polygon.emplace_back(cvRound(p.x)-box.x,cvRound(p.y)-box.y);}cv::fillConvexPoly(eyes,polygon,255);}
    cv::dilate(eyes,eyes,cv::getStructuringElement(cv::MORPH_ELLIPSE,{3,3}));chosen.setTo(0,eyes);
    for(int y=0;y<box.height;++y)std::copy_n(chosen.ptr<uchar>(y),box.width,result.scanLine(y+box.y)+box.x);
    return result;
}
}
