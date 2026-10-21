#include "huelith/editing/portrait_effects.h"
#include "huelith/editing/project.h"
#include <QFile>
#include <QCryptographicHash>
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace huelith::editing {
// Runs only in the disposable AI worker. The learned masks, not bounding boxes,
// define paintable nail surfaces; source-space masks survive later deformation.
QJsonArray portrait_detect_nails(const QImage& image,const QString& model){
    QFile file(model);if(!file.open(QIODevice::ReadOnly) || file.size()>64000000)throw EditError(QStringLiteral("甲面模型缺失或文件无效，请使用完整安装包"));
    const auto bytes=file.readAll();if(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()!="87b58ee4a89eaf87185d7bf3e3daae5f98df37e0502194c5d1db657345045264")throw EditError(QStringLiteral("甲面模型校验失败"));
    constexpr int edge=640,plane=edge*edge;auto rgb=(image.format()==QImage::Format_RGBA32FPx4_Premultiplied?display_image(image):image).scaled(edge,edge,Qt::KeepAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA8888);
    if(rgb.isNull())throw EditError(QStringLiteral("没有可识别的甲面图像"));const int left=(edge-rgb.width())/2,top=(edge-rgb.height())/2;
    std::vector<float> input(3*plane,114.F/255.F);for(int y=0;y<rgb.height();++y)for(int x=0;x<rgb.width();++x){const auto* p=rgb.constScanLine(y)+x*4;for(int c=0;c<3;++c)input[c*plane+(top+y)*edge+left+x]=(p[c]*p[3]/255.F+114.F*(1-p[3]/255.F))/255.F;}
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING,"HuelithNails");Ort::SessionOptions options;options.SetIntraOpNumThreads(4);options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);Ort::Session session(env,bytes.constData(),bytes.size(),options);
    auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);const std::array<int64_t,4> shape{1,3,edge,edge};auto tensor=Ort::Value::CreateTensor<float>(memory,input.data(),input.size(),shape.data(),shape.size());const char* ins[]{"images"};const char* outs[]{"output0","output1"};auto outputs=session.Run(Ort::RunOptions{},ins,&tensor,1,outs,2);
    if(outputs[0].GetTensorTypeAndShapeInfo().GetShape()!=std::vector<int64_t>{1,37,8400} || outputs[1].GetTensorTypeAndShapeInfo().GetShape()!=std::vector<int64_t>{1,32,160,160})throw EditError(QStringLiteral("甲面模型输出无效"));
    const auto* detections=outputs[0].GetTensorData<float>();const auto* proto=outputs[1].GetTensorData<float>();struct Candidate{int index;float score;QRectF box;};std::vector<Candidate> candidates,kept;
    for(int i=0;i<8400;++i){const float score=detections[4*8400+i];if(!std::isfinite(score)||score<.4F)continue;const float cx=detections[i],cy=detections[8400+i],w=detections[2*8400+i],h=detections[3*8400+i];if(!std::isfinite(cx+cy+w+h)||w<2||h<2||w>edge||h>edge)continue;candidates.push_back({i,score,QRectF(cx-w/2,cy-h/2,w,h)});}
    std::sort(candidates.begin(),candidates.end(),[](auto a,auto b){return a.score>b.score;});for(const auto& c:candidates){bool overlap=false;for(const auto& k:kept){const auto r=c.box.intersected(k.box);const double a=r.width()*r.height();if(a/(c.box.width()*c.box.height()+k.box.width()*k.box.height()-a)>.45){overlap=true;break;}}if(!overlap)kept.push_back(c);if(kept.size()==20)break;}
    QJsonArray nails;const auto linear=image.format()==QImage::Format_RGBA32FPx4_Premultiplied?image:to_working(image);
    for(const auto& c:kept){QImage small(160,160,QImage::Format_Grayscale8);small.fill(0);for(int y=0;y<160;++y)for(int x=0;x<160;++x){if(!c.box.contains(QPointF(x*4,y*4)))continue;double logit=0;for(int ch=0;ch<32;++ch)logit+=detections[(5+ch)*8400+c.index]*proto[ch*25600+y*160+x];small.scanLine(y)[x]=uchar(std::clamp(qRound(255/(1+std::exp(-std::clamp(logit,-30.,30.)))),0,255));}
        auto mask=small.scaled(edge,edge,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).copy(left,top,rgb.width(),rgb.height());QRect bounds;int count=0;std::vector<double> lights;
        for(int y=0;y<mask.height();++y)for(int x=0;x<mask.width();++x){auto& v=mask.scanLine(y)[x];v=v>127?255:0;if(!v)continue;++count;bounds=bounds.united(QRect(x,y,1,1));const int sx=std::clamp(int((x+.5)*linear.width()/mask.width()),0,linear.width()-1),sy=std::clamp(int((y+.5)*linear.height()/mask.height()),0,linear.height()-1);const auto* p=reinterpret_cast<const float*>(linear.constScanLine(sy))+sx*4;if(p[3]>.01F)lights.push_back((.2627*p[0]+.678*p[1]+.0593*p[2])/p[3]);}
        if(count<5||lights.empty())continue;std::nth_element(lights.begin(),lights.begin()+lights.size()/2,lights.end());const double x=double(bounds.x())/mask.width(),y=double(bounds.y())/mask.height(),w=double(bounds.width())/mask.width(),h=double(bounds.height())/mask.height();
        // Keep the learned edge confidence instead of turning every boundary pixel opaque.
        auto confidence=small.scaled(edge,edge,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).copy(left,top,rgb.width(),rgb.height());
        for(int yy=0;yy<mask.height();++yy)for(int xx=0;xx<mask.width();++xx)
            mask.scanLine(yy)[xx]=uchar(std::clamp((int(confidence.constScanLine(yy)[xx])-127)*2,0,255));
        nails.append(QJsonObject{{"origin","automatic"},{"materialVersion",2},{"confidence",c.score},{"enabled",true},{"mask",portrait_mask_png(mask)},{"outline",QJsonArray{QJsonArray{x,y},QJsonArray{x+w,y},QJsonArray{x+w,y+h},QJsonArray{x,y+h}}},{"color",QJsonArray{.24,.09,.12}},{"referenceLuminance",lights[lights.size()/2]}});
    }return nails;
}
}
