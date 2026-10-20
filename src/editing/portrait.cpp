// OpenSeeFace model decoding adapted under BSD-2-Clause; see licenses/OpenSeeFace.txt.
#include "huelith/editing/portrait.h"
#include "huelith/editing/project.h"
#include "huelith/editing/semantic.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QPainter>
#include <QPainterPath>
#include <QBuffer>
#include <onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numeric>
#include <memory>

namespace huelith::editing {
namespace {
QJsonArray rectangle(QRectF r) { return {r.x(),r.y(),r.width(),r.height()}; }
QRectF rectangle(const QJsonArray& a) {
    if(a.size()!=4) throw EditError(QStringLiteral("人物坐标损坏"));
    for(const auto& v:a) if(!v.isDouble() || !std::isfinite(v.toDouble()))
        throw EditError(QStringLiteral("人物坐标无效"));
    return {a[0].toDouble(),a[1].toDouble(),a[2].toDouble(),a[3].toDouble()};
}
double iou(QRectF a,QRectF b) {
    const auto r=a.intersected(b);const double n=r.width()*r.height();
    return n/std::max(1e-12,a.width()*a.height()+b.width()*b.height()-n);
}
struct Model {
    Ort::Session session{nullptr};
    Model(Ort::Env& env,const QString& path,const QByteArray& hash) {
        QFile f(path);
        if(!f.open(QIODevice::ReadOnly) || f.size()>25'000'000) throw EditError(QStringLiteral("人像模型缺失：")+path);
        const auto bytes=f.readAll();
        if(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()!=hash)
            throw EditError(QStringLiteral("人像模型校验失败：")+path);
        Ort::SessionOptions options;options.SetIntraOpNumThreads(4);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session=Ort::Session(env,bytes.constData(),std::size_t(bytes.size()),options);
    }
    std::vector<Ort::Value> run(const QImage& image,bool pose=false,bool personDetector=false) {
        const auto rgb=image.convertToFormat(QImage::Format_RGB888);
        cv::Mat pixels(rgb.height(),rgb.width(),CV_8UC3,const_cast<uchar*>(rgb.constBits()),rgb.bytesPerLine()),scaled;
        const int edge=pose?256:224;
        int padX=0,padY=0,activeWidth=edge,activeHeight=edge;
        if(personDetector){const double ratio=double(edge)/std::max(rgb.width(),rgb.height());activeWidth=std::max(1,int(rgb.width()*ratio));activeHeight=std::max(1,int(rgb.height()*ratio));padX=(edge-activeWidth)/2;padY=(edge-activeHeight)/2;cv::Mat resized;cv::resize(pixels,resized,{activeWidth,activeHeight},0,0,cv::INTER_LINEAR);cv::copyMakeBorder(resized,scaled,padY,edge-activeHeight-padY,padX,edge-activeWidth-padX,cv::BORDER_CONSTANT);}
        else cv::resize(pixels,scaled,{edge,edge},0,0,cv::INTER_LINEAR);
        std::vector<float> input(3*edge*edge);
        constexpr float mean[]{.485F,.456F,.406F},stddev[]{.229F,.224F,.225F};
        for(int y=0;y<edge;++y)for(int x=0;x<edge;++x)for(int c=0;c<3;++c)
            input[pose?(std::size_t(y)*edge+x)*3+c:std::size_t(c)*edge*edge+y*edge+x]=personDetector?(x<padX || x>=padX+activeWidth || y<padY || y>=padY+activeHeight?0.F:scaled.at<cv::Vec3b>(y,x)[c]/127.5F-1.F):pose?scaled.at<cv::Vec3b>(y,x)[c]/255.F:(scaled.at<cv::Vec3b>(y,x)[c]/255.F-mean[c])/stddev[c];
        const std::array<int64_t,4> shape=pose?std::array<int64_t,4>{1,256,256,3}:std::array<int64_t,4>{1,3,224,224};
        const auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
        auto tensor=Ort::Value::CreateTensor<float>(memory,input.data(),input.size(),shape.data(),shape.size());
        Ort::AllocatorWithDefaultOptions alloc;
        const auto input_name=session.GetInputNameAllocated(0,alloc);
        const char* input_names[]{input_name.get()};
        std::vector<Ort::AllocatedStringPtr> storage;std::vector<const char*> names;
        for(std::size_t i=0;i<session.GetOutputCount();++i)storage.push_back(session.GetOutputNameAllocated(i,alloc));
        for(const auto& s:storage)names.push_back(s.get());
        return session.Run(Ort::RunOptions{nullptr},input_names,&tensor,1,names.data(),names.size());
    }
};
struct Detection { QRectF box; double confidence; };
QString mask_png(const QImage& image) {QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);image.save(&buffer,"PNG");return QString::fromLatin1(bytes.toBase64());}
QJsonObject semantic_portrait_masks(Model& model,const QImage& image,const PortraitPerson& person) {
    const auto canvas=image.size().scaled(768,768,Qt::KeepAspectRatio);QImage skin(canvas,QImage::Format_Grayscale8),hair(canvas,QImage::Format_Grayscale8);skin.fill(0);hair.fill(0);QImage face_skin=skin.copy(),body_skin=skin.copy();
    const auto face=person.face;std::vector<QRectF> areas;
    if(!person.body_ambiguous)areas.push_back(person.body.adjusted(-.015,-.015,.015,.015));
    areas.push_back(face.adjusted(-face.width()*.6,-face.height()*.8,face.width()*.6,face.height()*.6));
    for(const auto& area:areas){const auto box=QRectF(area.x()*image.width(),area.y()*image.height(),area.width()*image.width(),area.height()*image.height()).toAlignedRect().intersected(image.rect());if(box.isEmpty())continue;
        auto output=model.run(image.copy(box),true);if(output.size()!=1 || output[0].GetTensorTypeAndShapeInfo().GetElementCount()!=256*256*6)throw EditError(QStringLiteral("皮肤范围模型接口无效"));
        const float* logits=output[0].GetTensorData<float>();QImage local_skin(256,256,QImage::Format_Grayscale8),local_hair=local_skin.copy(),local_face=local_skin.copy(),local_body=local_skin.copy();
        for(int y=0;y<256;++y)for(int x=0;x<256;++x){const auto* p=logits+(y*256+x)*6;const float maximum=*std::max_element(p,p+6);double sum=0;std::array<double,6> probability{};for(int c=0;c<6;++c){probability[c]=std::exp(p[c]-maximum);sum+=probability[c];}
            const double skin_probability=std::clamp(((probability[2]+probability[3])/sum-.35)/.5,0.,1.);
            local_skin.scanLine(y)[x]=uchar(skin_probability*skin_probability*(3-2*skin_probability)*255.);const double face_share=probability[3]/std::max(1e-12,probability[2]+probability[3]);local_face.scanLine(y)[x]=uchar(local_skin.constScanLine(y)[x]*face_share);local_body.scanLine(y)[x]=uchar(local_skin.constScanLine(y)[x]*(1-face_share));local_hair.scanLine(y)[x]=uchar(std::clamp(probability[1]/sum*255.,0.,255.));}
        const QRect target(int(double(box.x())/image.width()*canvas.width()),int(double(box.y())/image.height()*canvas.height()),std::max(1,int(double(box.width())/image.width()*canvas.width())),std::max(1,int(double(box.height())/image.height()*canvas.height())));
        local_face=local_face.scaled(target.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);local_body=local_body.scaled(target.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
        local_skin=local_skin.scaled(target.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);local_hair=local_hair.scaled(target.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
        for(int y=0;y<target.height() && target.y()+y<canvas.height();++y)for(int x=0;x<target.width() && target.x()+x<canvas.width();++x){auto& a=skin.scanLine(target.y()+y)[target.x()+x];auto& b=hair.scanLine(target.y()+y)[target.x()+x];a=std::max(a,local_skin.constScanLine(y)[x]);b=std::max(b,local_hair.constScanLine(y)[x]);auto& f=face_skin.scanLine(target.y()+y)[target.x()+x];auto& d=body_skin.scanLine(target.y()+y)[target.x()+x];f=std::max(f,local_face.constScanLine(y)[x]);d=std::max(d,local_body.constScanLine(y)[x]);}
    }
    auto makeup_skin=skin.copy();
    QImage exclusion(canvas,QImage::Format_ARGB32_Premultiplied);exclusion.fill(Qt::transparent);QPainter painter(&exclusion);painter.setRenderHint(QPainter::Antialiasing);painter.setBrush(Qt::white);
    const double width=std::max(2.,face.width()*canvas.width()*.035);painter.setPen(QPen(Qt::white,width,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    if(person.landmarks.size()==66)for(const auto [first,last]:{std::pair{17,21},std::pair{22,26},std::pair{36,41},std::pair{42,47},std::pair{48,59}}){
        QPainterPath path;std::vector<int> indices;
        if(first==48 && person.settings["mouthTopologyVersion"].toInt(1)>=2)indices={58,48,49,50,51,52,62,53,54,55,56,57};else for(int i=first;i<=last;++i)indices.push_back(i);
        for(int index=0;index<int(indices.size());++index){const auto a=person.landmarks[indices[index]].toArray();const QPointF p(a[0].toDouble()*canvas.width(),a[1].toDouble()*canvas.height());if(index==0)path.moveTo(p);else path.lineTo(p);}
        if(first>=36)path.closeSubpath();painter.drawPath(path);
    }painter.end();
    const auto instance=person.body_mask.isNull()?QImage{}:person.body_mask.scaled(canvas,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    for(int y=0;y<canvas.height();++y)for(int x=0;x<canvas.width();++x){const double coverage=instance.isNull()?1.:instance.constScanLine(y)[x]/255.;const double excluded=qAlpha(exclusion.pixel(x,y))/255.;makeup_skin.scanLine(y)[x]=uchar(makeup_skin.constScanLine(y)[x]*coverage);skin.scanLine(y)[x]=uchar(skin.constScanLine(y)[x]*coverage*(1-excluded));hair.scanLine(y)[x]=uchar(hair.constScanLine(y)[x]*coverage);}
    // The six-class model does not distinguish beards from face skin. Protect dark
    // facial hair relative to this person's upper-face lightness, never to an
    // absolute skin-color threshold. The result remains manually correctable.
    const auto colors=image.scaled(canvas,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888);std::vector<double> reference;
    auto light=[&](int x,int y){const auto* p=colors.constScanLine(y)+x*3;return .2126*p[0]+.7152*p[1]+.0722*p[2];};
    for(int y=std::max(0,int(face.y()*canvas.height()));y<std::min(canvas.height(),int((face.y()+face.height()*.52)*canvas.height()));++y)for(int x=std::max(0,int((face.x()+face.width()*.2)*canvas.width()));x<std::min(canvas.width(),int((face.x()+face.width()*.8)*canvas.width()));++x)if(skin.constScanLine(y)[x]>230)reference.push_back(light(x,y));
    if(reference.size()>20){std::nth_element(reference.begin(),reference.begin()+reference.size()/2,reference.end());const double median=reference[reference.size()/2];
        for(int y=std::max(0,int((face.y()+face.height()*.50)*canvas.height()));y<std::min(canvas.height(),int((face.y()+face.height()*1.05)*canvas.height()));++y)for(int x=std::max(0,int(face.x()*canvas.width()));x<std::min(canvas.width(),int((face.x()+face.width())*canvas.width()));++x){const double ratio=light(x,y)/std::max(1.,median);const double weight=std::clamp((ratio-.43)/.30,0.,1.);skin.scanLine(y)[x]=uchar(skin.constScanLine(y)[x]*weight*weight*(3-2*weight));}}
    for(int y=0;y<canvas.height();++y)for(int x=0;x<canvas.width();++x){
        auto& f=face_skin.scanLine(y)[x];auto& b=body_skin.scanLine(y)[x];const double sum=f+b;
        const int coverage=skin.constScanLine(y)[x];const double share=sum>0?std::clamp((f/sum-.15)/.7,0.,1.):0.;
        f=uchar(coverage*share*share*(3-2*share));b=uchar(coverage-f);
    }
    return {{"skin",mask_png(skin)},{"makeupSkin",mask_png(makeup_skin)},{"faceSkin",mask_png(face_skin)},{"bodySkin",mask_png(body_skin)},{"hair",mask_png(hair)}};
}
// MediaPipe person detector supplies the virtual hip center/full-body radius
// expected by the landmark model (OpenCV Zoo reference, Apache-2.0).
struct PoseRegion { QRectF face;QPointF hip,full;double confidence{}; };
std::vector<PoseRegion> pose_regions(Model& detector,const QImage& image){
    auto outputs=detector.run(image,false,true);const float* boxes=nullptr;const float* scores=nullptr;
    for(const auto& output:outputs){const auto count=output.GetTensorTypeAndShapeInfo().GetElementCount();if(count==2254*12)boxes=output.GetTensorData<float>();if(count==2254)scores=output.GetTensorData<float>();}
    if(!boxes || !scores)throw EditError("人体检测模型接口无效");
    const double scale=std::max(image.width(),image.height()),ratio=224./scale;
    const QPointF pad(int(((224-int(image.width()*ratio))/2)/ratio),int(((224-int(image.height()*ratio))/2)/ratio));
    std::vector<PoseRegion> result;int index=0;
    for(const auto [grid,repeats]:{std::pair{28,2},std::pair{14,2},std::pair{7,6}})for(int y=0;y<grid;++y)for(int x=0;x<grid;++x)for(int k=0;k<repeats;++k,++index){
        const double confidence=1./(1.+std::exp(-std::clamp(double(scores[index]),-100.,100.)));if(confidence<.5)continue;
        const auto* p=boxes+index*12;const QPointF anchor((x+.5)/grid,(y+.5)/grid);
        const auto decode=[&](int offset){return (anchor+QPointF(p[offset]/224.,p[offset+1]/224.))*scale-pad;};
        const auto center=decode(0);const QSizeF extent(p[2]/224.*scale,p[3]/224.*scale);
        result.push_back({QRectF(center-QPointF(extent.width()*.5,extent.height()*.5),extent),decode(4),decode(6),confidence});
    }
    return result;
}
QJsonArray body_pose(Model& model,const QImage& image,const PortraitPerson& person,const std::vector<PoseRegion>& regions) {
    const QRectF face(person.face.x()*image.width(),person.face.y()*image.height(),person.face.width()*image.width(),person.face.height()*image.height());
    const PoseRegion* selected=nullptr;double bestScore=0;
    for(const auto& region:regions){const double overlap=iou(face,region.face);if(overlap<.05 || !face.adjusted(-face.width()*.25,-face.height()*.25,face.width()*.25,face.height()*.25).contains(region.face.center()))continue;
        const double score=overlap*region.confidence;if(score>bestScore){bestScore=score;selected=&region;}}
    if(!selected)return {};
    const QPointF center=selected->hip;const auto delta=selected->full-center;
    const double extent=2*std::hypot(delta.x(),delta.y());if(extent<16 || extent>std::max(image.width(),image.height())*10)return {};
    const double angle=std::atan2(delta.x(),-delta.y())*180./3.141592653589793;
    const auto attempt=[&](double cropAngle,double cropScale,bool mirrored=false)->QJsonArray{
    auto transform=cv::getRotationMatrix2D(cv::Point2f(float(center.x()),float(center.y())),cropAngle,256./std::max(1.,extent*cropScale));
    transform.at<double>(0,2)+=128-center.x();transform.at<double>(1,2)+=128-center.y();
    const auto rgb=image.convertToFormat(QImage::Format_RGB888);cv::Mat input(rgb.height(),rgb.width(),CV_8UC3,const_cast<uchar*>(rgb.constBits()),rgb.bytesPerLine()),crop;
    cv::warpAffine(input,crop,transform,{256,256},cv::INTER_LINEAR,cv::BORDER_CONSTANT);
    if(mirrored)cv::flip(crop,crop,1);
    QImage proxy(crop.data,256,256,qsizetype(crop.step),QImage::Format_RGB888);auto outputs=model.run(proxy,true);
    const float* points=nullptr;double score{};
    for(const auto& output:outputs){const auto count=output.GetTensorTypeAndShapeInfo().GetElementCount();if(count==195)points=output.GetTensorData<float>();if(count==1)score=*output.GetTensorData<float>();}
    if(!points || score<.5)return {};
    cv::Mat inverse;cv::invertAffineTransform(transform,inverse);QJsonArray result;
    for(int i=0;i<33;++i){
        int source=i;if(mirrored){if(i>=1 && i<=3)source=i+3;else if(i>=4 && i<=6)source=i-3;else if(i>=7)source=(i%2)?i+1:i-1;}
        const float* p=points+source*5;const double px=mirrored?255.-p[0]:p[0];
        const double x=inverse.at<double>(0,0)*px+inverse.at<double>(0,1)*p[1]+inverse.at<double>(0,2),y=inverse.at<double>(1,0)*px+inverse.at<double>(1,1)*p[1]+inverse.at<double>(1,2);
        const double confidence=1./(1.+std::exp(-std::clamp(double(p[3]),-30.,30.)))/(1.+std::exp(-std::clamp(double(p[4]),-30.,30.)));
        result.append(QJsonArray{x/image.width(),y/image.height(),confidence});}
    const auto nose=result[0].toArray();const auto expanded=person.face.adjusted(-person.face.width(),-person.face.height(),person.face.width(),person.face.height());
    if(!expanded.contains(QPointF(nose[0].toDouble(),nose[1].toDouble())))return {};
    return result;
    };
    return attempt(angle,1.);
}
std::vector<Detection> detect(Model& model,const QImage& image) {
    std::vector<Detection> candidates;
    std::vector<QRect> tiles{image.rect()};
    // Overlapping tiles recover small faces without changing the model's input contract.
    if(std::max(image.width(),image.height())>650) {
        const int w=image.width()*2/3,h=image.height()*2/3;
        for(int y:{0,image.height()-h})for(int x:{0,image.width()-w})tiles.emplace_back(x,y,w,h);
    }
    for(const auto& tile:tiles) {
        auto output=model.run(image.copy(tile));
        if(output.size()!=2 || output[0].GetTensorTypeAndShapeInfo().GetElementCount()!=2*56*56)
            throw EditError(QStringLiteral("人脸检测模型接口无效"));
        const float* p=output[0].GetTensorData<float>();const float* peak=output[1].GetTensorData<float>();
        for(int k=0;k<56*56;++k)if(p[k]>.6F && p[k]>=peak[k]) {
            const double r=p[56*56+k]*112.;
            QRectF box(tile.x()+(k%56*4-r)*tile.width()/224.,tile.y()+(k/56*4-r)*tile.height()/224.,
                       2*r*tile.width()/224.,2*r*tile.height()/224.);
            box=box.intersected(QRectF(image.rect()));
            if(box.width()>=12 && box.height()>=12)candidates.push_back({box,p[k]});
        }
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.confidence>b.confidence;});
    std::vector<Detection> accepted;
    for(const auto& c:candidates) {
        if(std::none_of(accepted.begin(),accepted.end(),[&](const auto& a){return iou(a.box,c.box)>.3;}))accepted.push_back(c);
        if(accepted.size()==64)break;
    }
    return accepted;
}
}
QJsonObject portrait_person_json(const PortraitPerson& p) {
    QJsonObject regions;
    const auto add=[&](const char* name,int first,int last){QJsonArray points;for(int i=first;i<=last && i<p.landmarks.size();++i)points.append(p.landmarks[i]);regions[name]=points;};
    add("jaw",0,16);add("rightBrow",17,21);add("leftBrow",22,26);add("nose",27,35);
    add("rightEye",36,41);add("leftEye",42,47);add("outerLip",48,59);add("innerLip",60,65);
    if(p.landmarks.size()==66 && p.settings["mouthTopologyVersion"].toInt(1)>=2){
        QJsonArray outer,inner;for(int i:{58,48,49,50,51,52,62,53,54,55,56,57})outer.append(p.landmarks[i]);for(int i:{58,59,60,61,62,63,64,65})inner.append(p.landmarks[i]);
        regions["outerLip"]=outer;regions["innerLip"]=inner;
    }
    return {{"id",p.id.toString()},{"face",rectangle(p.face)},{"body",rectangle(p.body)},
        {"landmarks",p.landmarks},{"regions",regions},{"confidence",p.confidence},{"settings",p.settings},
        {"bodyAmbiguous",p.body_ambiguous},{"matchAmbiguous",p.match_ambiguous},{"unmatched",p.unmatched}};
}
PortraitPerson portrait_person_from_json(const QJsonObject& o) {
    PortraitPerson p;p.id=QUuid(o["id"].toString());
    if(p.id.isNull())throw EditError(QStringLiteral("人物标识无效"));
    p.face=rectangle(o["face"].toArray());p.body=rectangle(o["body"].toArray());
    if(p.face.isEmpty() || !QRectF(0,0,1.001,1.001).contains(p.face) || p.body.width()<0 || p.body.height()<0)
        throw EditError(QStringLiteral("人物范围超出图像"));
    p.landmarks=o["landmarks"].toArray();p.confidence=o["confidence"].toDouble();p.settings=o["settings"].toObject();
    if(p.landmarks.size()!=66 && !(p.settings["partialSkin"].toBool() && p.landmarks.isEmpty()))throw EditError(QStringLiteral("人物关键点损坏"));
    for(const auto& value:p.landmarks) {
        const auto a=value.toArray();if(a.size()!=3)throw EditError(QStringLiteral("人物关键点无效"));
        for(const auto& v:a)if(!v.isDouble() || !std::isfinite(v.toDouble()))throw EditError(QStringLiteral("人物关键点无效"));
    }
    p.body_ambiguous=o["bodyAmbiguous"].toBool();p.match_ambiguous=o["matchAmbiguous"].toBool();p.unmatched=o["unmatched"].toBool();return p;
}
AiResult infer_portrait(const QImage& image,const QString& models,const AiProgress& progress) {
    auto rgb=image.format()==QImage::Format_RGBA32FPx4_Premultiplied?display_image(image):image;
    if(rgb.isNull())throw EditError(QStringLiteral("没有可识别的人像照片"));
    rgb=rgb.scaled(1600,1600,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING,"HuelithPortrait");
    Model detector(env,QDir(models).filePath("mnv3_detection_opt.onnx"),"0e8e4806766d85ab067a52c7af0dcb59eb7f9dfe580b44f20a8e6ab712d89809");
    Model landmark(env,QDir(models).filePath("lm_model3_opt.onnx"),"5aa0ab2594acaf2e1d3916a8de592cffeb1f1a7a7f778cad2f37948bff6549d1");
    std::unique_ptr<Model> surface;
    const auto surface_path=QDir(models).filePath("face_landmarks_478.onnx");
    try {
        if(QFile::exists(surface_path))surface=std::make_unique<Model>(env,surface_path,"7d6e82dee82a1dca5fbddb282b3cc74571833a530de317fc22ae325c3358beeb");
    } catch(const std::exception&) {
        // Optional orientation inference must not disable the existing face,
        // body and retouch controls when its model is unavailable or invalid.
        surface.reset();
    }
    Model personDetector(env,QDir(models).filePath("person_detection_mediapipe_2023mar.onnx"),"47fd5599d6fa17608f03e0eb0ae230baa6e597d7e8a2c8199fe00abea55a701f");
    Model pose(env,QDir(models).filePath("pose_estimation_mediapipe_2023mar.onnx"),"9d89c599319a18fb7d2e28451a883476164543182bafca5f09eb2cf767ed2f3f");
    Model segmenter(env,QDir(models).filePath("selfie_multiclass.onnx"),"35ec1ecd9ee7f85073c99c00020b7f6751b69506eeacf683bc8665f6117f85b0");
    const auto poseRegions=pose_regions(personDetector,rgb);
    if(progress)progress(1,3);
    const std::atomic_bool cancelled{false};
    const auto instances=semantic_instances(rgb,QDir(models).filePath("maskrcnn-r50-v2.onnx"),cancelled);
    QImage sceneOcclusion;
    for(const auto& instance:instances){
        if(instance.category==1 || instance.confidence<.6F)continue;
        const auto mask=instance.mask.convertToFormat(QImage::Format_Grayscale8);
        if(sceneOcclusion.isNull()){sceneOcclusion=QImage(mask.size(),QImage::Format_Grayscale8);sceneOcclusion.fill(0);}
        const auto aligned=mask.size()==sceneOcclusion.size()?mask:mask.scaled(sceneOcclusion.size());
        for(int y=0;y<sceneOcclusion.height();++y)for(int x=0;x<sceneOcclusion.width();++x)
            sceneOcclusion.scanLine(y)[x]=std::max(sceneOcclusion.constScanLine(y)[x],aligned.constScanLine(y)[x]);
    }
    const auto sceneOcclusionPng=sceneOcclusion.isNull()?QString{}:mask_png(sceneOcclusion);

    const auto gray=rgb.convertToFormat(QImage::Format_Grayscale8);cv::Mat gray_mat(gray.height(),gray.width(),CV_8UC1,const_cast<uchar*>(gray.constBits()),gray.bytesPerLine()),edges;
    cv::Canny(gray_mat,edges,70,150);std::vector<cv::Vec4i> lines;cv::HoughLinesP(edges,lines,1,CV_PI/180,80,std::max(rgb.width(),rgb.height())*.2,12);
    // Hough's long-line threshold misses nearby furniture edges and shorter
    // straight details. Add locally supported segments before ranking.
    std::vector<cv::Vec4f> segments;cv::createLineSegmentDetector(cv::LSD_REFINE_STD)->detect(gray_mat,segments);
    const double minimumSegment=std::max(24.,std::max(rgb.width(),rgb.height())*.06);
    for(const auto& segment:segments)if(std::hypot(segment[2]-segment[0],segment[3]-segment[1])>=minimumSegment)
        lines.emplace_back(cvRound(segment[0]),cvRound(segment[1]),cvRound(segment[2]),cvRound(segment[3]));
    std::sort(lines.begin(),lines.end(),[](const auto& a,const auto& b){return std::hypot(a[2]-a[0],a[3]-a[1])>std::hypot(b[2]-b[0],b[3]-b[1]);});
    QJsonArray background_lines;for(std::size_t i=0;i<std::min<std::size_t>(128,lines.size());++i){const auto l=lines[i];background_lines.append(QJsonArray{double(l[0])/rgb.width(),double(l[1])/rgb.height(),double(l[2])/rgb.width(),double(l[3])/rgb.height()});}
    std::vector<PortraitPerson> people;
    // Upright images keep the original fast path. Retry orientation only when
    // it produced no confident face, then map every landmark back to source.
    for(const int angle:{0,-90,90,180}){
    if(angle!=0 && !people.empty())break;
    QTransform rotation;rotation.rotate(angle);const auto oriented=angle==0?rgb:rgb.transformed(rotation,Qt::FastTransformation);
    const auto inverse=QImage::trueMatrix(rotation,rgb.width(),rgb.height()).inverted();
    const auto faces=detect(detector,oriented);
    for(const auto& face:faces) {
        const auto originalFace=inverse.mapRect(face.box).intersected(QRectF(rgb.rect()));
        const auto r=face.box.adjusted(-face.box.width()*.1,-face.box.height()*.125,face.box.width()*.1,face.box.height()*.125)
            .toAlignedRect().intersected(oriented.rect());
        auto output=landmark.run(oriented.copy(r));
        if(output[0].GetTensorTypeAndShapeInfo().GetElementCount()!=198*28*28)
            throw EditError(QStringLiteral("关键点模型接口无效"));
        const float* p=output[0].GetTensorData<float>();PortraitPerson person;double confidence{};
        for(int i=0;i<66;++i) {
            const float* heat=p+i*784;const int k=int(std::max_element(heat,heat+784)-heat);
            const auto offset=[](float v){const double a=std::clamp(double(v),1e-7,1-1e-7);return std::log(a/(1-a))/16.;};
            const double y=r.y()+r.height()*223./224.*(k/28/27.+offset(p[(i+66)*784+k]));
            const double x=r.x()+r.width()*223./224.*(k%28/27.+offset(p[(i+132)*784+k]));
            const auto sourcePoint=inverse.map(QPointF(x,y));person.landmarks.append(QJsonArray{sourcePoint.x()/rgb.width(),sourcePoint.y()/rgb.height(),double(heat[k])});confidence+=heat[k];
        }
        person.confidence=confidence/66.;if(person.confidence<.6)continue;
        person.settings["mouthTopologyVersion"]=2;
        person.face={originalFace.x()/rgb.width(),originalFace.y()/rgb.height(),originalFace.width()/rgb.width(),originalFace.height()/rgb.height()};
        if(surface)try { for(const double cropScale:{1.5,1.25,1.0}){
            const double edge=std::max(face.box.width(),face.box.height())*cropScale;
            const QRect crop(int(face.box.center().x()-edge/2),int(face.box.center().y()-edge/2),int(edge),int(edge));
            auto dense=surface->run(oriented.copy(crop),true);
            if(dense.size()>=2 && dense[0].GetTensorTypeAndShapeInfo().GetElementCount()==1434 && dense[1].GetTensorTypeAndShapeInfo().GetElementCount()==1){
                const double presence=1/(1+std::exp(-double(dense[1].GetTensorData<float>()[0])));
                if(presence>.9){
                    QJsonArray points;const auto* xyz=dense[0].GetTensorData<float>();bool valid=true;
                    for(int i=0;i<468;++i){
                        const double x=crop.x()+xyz[i*3]*crop.width()/256.,y=crop.y()+xyz[i*3+1]*crop.height()/256.;
                        const double z=xyz[i*3+2]*crop.width()/256./rgb.width();
                        const auto q=inverse.map(QPointF(x,y));
                        valid=valid && std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
                        points.append(QJsonArray{q.x()/rgb.width(),q.y()/rgb.height(),z});
                    }
                    if(valid){person.settings["faceSurface"]=QJsonObject{{"version",1},{"points",points},{"confidence",presence}};break;}
                }
            }
        }} catch(const std::exception&) {
            person.settings.remove("faceSurface");
        }
        // Associate real instance masks by face coverage, never by list order.
        double best{},second{};int target=-1;
        for(std::size_t j=0;j<instances.size();++j) {
            const auto& inst=instances[j];if(inst.category!=1)continue;
            const auto mask=inst.mask;const auto f=originalFace;double coverage{};int n{};
            for(int yy=1;yy<5;++yy)for(int xx=1;xx<5;++xx) {
                const int x=std::clamp(int((f.x()+f.width()*xx/5.)/rgb.width()*mask.width()),0,mask.width()-1);
                const int y=std::clamp(int((f.y()+f.height()*yy/5.)/rgb.height()*mask.height()),0,mask.height()-1);
                coverage+=mask.constScanLine(y)[x]/255.;++n;
            }
            coverage/=n;
            if(coverage>best){second=best;best=coverage;target=int(j);}else second=std::max(second,coverage);
        }
        person.body_ambiguous=target<0 || best<.45 || best-second<.15;
        if(!person.body_ambiguous) {
            const auto& inst=instances[std::size_t(target)];person.body_mask=inst.mask;
            person.body={double(inst.bounds.x())/inst.mask.width(),double(inst.bounds.y())/inst.mask.height(),
                double(inst.bounds.width())/inst.mask.width(),double(inst.bounds.height())/inst.mask.height()};
            person.settings["pose"]=body_pose(pose,rgb,person,poseRegions);
            person.settings["poseDetectionVersion"]=2;
        }
        person.settings["backgroundLines"]=background_lines;
        auto automaticMasks=semantic_portrait_masks(segmenter,rgb,person);if(!sceneOcclusionPng.isEmpty())automaticMasks["sceneOcclusion"]=sceneOcclusionPng;person.settings["automaticMasks"]=automaticMasks;
        people.push_back(std::move(person));
    }
    }
    if(people.empty()){
        PortraitPerson partial;partial.face={0,0,1,1};partial.body_ambiguous=true;partial.settings["partialSkin"]=true;partial.settings["backgroundLines"]=background_lines;
        const auto masks=semantic_portrait_masks(segmenter,rgb,partial);const auto skin=QImage::fromData(QByteArray::fromBase64(masks["skin"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);double coverage{};for(int y=0;y<skin.height();++y)for(int x=0;x<skin.width();++x)coverage+=skin.constScanLine(y)[x]/255.;
        if(coverage>skin.width()*skin.height()*.15){partial.settings["automaticMasks"]=masks;people.push_back(std::move(partial));}
    }
    // A merged body instance cannot silently belong to two faces.
    for(std::size_t i=0;i<people.size();++i)for(std::size_t j=i+1;j<people.size();++j)
        if(!people[i].body.isEmpty() && iou(people[i].body,people[j].body)>.9) {
            people[i].body_ambiguous=people[j].body_ambiguous=true;people[i].body_mask={};people[j].body_mask={};
        }
    std::sort(people.begin(),people.end(),[](const auto& a,const auto& b){return a.face.center().x()<b.face.center().x();});
    const QSize mask_size=instances.empty()?QSize(1,1):instances.front().mask.size();
    AiResult result;result.image=QImage(mask_size.width(),mask_size.height()*std::max(1,int(people.size())),QImage::Format_Grayscale8);result.image.fill(0);
    QJsonArray records;
    for(std::size_t i=0;i<people.size();++i) {
        records.append(portrait_person_json(people[i]));
        if(!people[i].body_mask.isNull())for(int y=0;y<mask_size.height();++y)
            std::memcpy(result.image.scanLine(int(i)*mask_size.height()+y),people[i].body_mask.constScanLine(y),std::size_t(mask_size.width()));
    }
    result.metadata={{"people",records},{"maskHeight",mask_size.height()},{"version",1},{"model","OpenSeeFace-66"}};
    if(progress)progress(3,3);return result;
}
std::vector<PortraitPerson> analyze_portrait(const QImage& image,const QString& models,const std::atomic_bool& cancelled,const AiProgress& progress) {
    const auto input=image.format()==QImage::Format_RGBA32FPx4_Premultiplied?image:image.convertToFormat(QImage::Format_RGBA8888);
    const auto result=run_ai_job({{"operation","portrait"},{"models",models}},input,&cancelled,progress);
    std::vector<PortraitPerson> people;const int height=result.metadata["maskHeight"].toInt();
    for(const auto& entry:result.metadata["people"].toArray()) {
        auto p=portrait_person_from_json(entry.toObject());
        if(!p.body_ambiguous)p.body_mask=result.image.copy(0,int(people.size())*height,result.image.width(),height);
        people.push_back(std::move(p));
    }
    return people;
}
void match_portrait_people(std::vector<PortraitPerson>& detected,const std::vector<PortraitPerson>& previous) {
    std::vector<int> chosen(detected.size(),-1);
    for(std::size_t i=0;i<detected.size();++i) {
        double best{},second{};int target=-1;
        for(std::size_t j=0;j<previous.size();++j) {
            if(previous[j].unmatched)continue;
            const double score=iou(detected[i].face,previous[j].face);
            if(score>best){second=best;best=score;target=int(j);}else second=std::max(second,score);
        }
        if(best>.55 && best-second>.2)chosen[i]=target;
        else if(!previous.empty())detected[i].match_ambiguous=true;
    }
    for(std::size_t i=0;i<detected.size();++i)if(chosen[i]>=0) {
        if(std::count(chosen.begin(),chosen.end(),chosen[i])!=1){detected[i].match_ambiguous=true;continue;}
        const auto fresh_masks=detected[i].settings["automaticMasks"].toObject();
        const QJsonValue fresh_surface=detected[i].settings.value("faceSurface");
        const auto& old=previous[std::size_t(chosen[i])];detected[i].id=old.id;detected[i].settings=old.settings;
        auto masks=detected[i].settings["automaticMasks"].toObject();for(const auto* key:{"faceSkin","bodySkin","makeupSkin"})if(fresh_masks.contains(key))masks[key]=fresh_masks[key];detected[i].settings["automaticMasks"]=masks;
        if(fresh_surface.isObject())detected[i].settings["faceSurface"]=fresh_surface;
        if(old.settings["landmarksCorrected"].toBool())detected[i].landmarks=old.landmarks;
    }
}
} // namespace huelith::editing
