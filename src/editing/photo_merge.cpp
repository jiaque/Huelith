#include "merge_storage.h"
#include "huelith/editing/image_storage.h"
#include "huelith/editing/photo_merge.h"
#include "huelith/editing/metadata.h"
#include "huelith/editing/project.h"
#include "huelith/editing/raw_develop.h"
#include <QFileInfo>
#include <QJsonArray>
#include <opencv2/calib3d.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/stitching.hpp>
#include <opencv2/stitching/warpers.hpp>
#include <opencv2/stitching/detail/seam_finders.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace huelith::editing {
namespace {
cv::Mat rgba(const QImage& image) {
    return cv::Mat(image.height(), image.width(), CV_32FC4,
                   const_cast<uchar*>(image.constBits()), std::size_t(image.bytesPerLine())).clone();
}
QImage image_from(const cv::Mat& pixels) {
    auto image = allocate_image(pixels.cols, pixels.rows, QImage::Format_RGBA32FPx4_Premultiplied);
    if (image.isNull()) throw EditError(QStringLiteral("合成结果内存不足"));
    image.setColorSpace(working_space());
    for (int y = 0; y < pixels.rows; ++y)
        std::memcpy(image.scanLine(y), pixels.ptr(y), std::size_t(pixels.cols) * 16);
    return image;
}
cv::Mat display_bgr(const QImage& image) {
    const auto displayed = display_image(image).convertToFormat(QImage::Format_RGB888);
    cv::Mat rgb(displayed.height(), displayed.width(), CV_8UC3, const_cast<uchar*>(displayed.constBits()), std::size_t(displayed.bytesPerLine()));
    cv::Mat bgr; cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR); return bgr;
}
void size_guard(cv::Size size) {
    if (size.width < 1 || size.height < 1 || qint64(size.width) * size.height > huelith::editing::maximum_image_pixels)
        throw EditError(QStringLiteral("合成范围超过当前 5.12 亿像素工程上限"));
}
cv::Mat alignment(const cv::Mat& source, const cv::Mat& reference) {
    const double factor = std::min(1., 1200. / std::max(reference.cols, reference.rows));
    cv::Mat a, b;
    cv::resize(source, a, {}, factor, factor); cv::resize(reference, b, {}, factor, factor);
    cv::cvtColor(a, a, cv::COLOR_BGR2GRAY); cv::cvtColor(b, b, cv::COLOR_BGR2GRAY);
    auto clahe = cv::createCLAHE(2.0); clahe->apply(a, a); clahe->apply(b, b);
    auto detector = cv::ORB::create(5000);
    std::vector<cv::KeyPoint> ka, kb; cv::Mat da, db;
    detector->detectAndCompute(a, cv::noArray(), ka, da); detector->detectAndCompute(b, cv::noArray(), kb, db);
    if (da.empty() || db.empty()) throw EditError(QStringLiteral("照片纹理不足，无法自动对齐；可关闭对齐使用已对齐照片"));
    std::vector<std::vector<cv::DMatch>> pairs;
    cv::BFMatcher(cv::NORM_HAMMING).knnMatch(da, db, pairs, 2);
    std::vector<cv::Point2f> pa, pb;
    for (const auto& pair : pairs)
        if (pair.size() == 2 && pair[0].distance < .75F * pair[1].distance) {
            pa.push_back(ka[std::size_t(pair[0].queryIdx)].pt); pb.push_back(kb[std::size_t(pair[0].trainIdx)].pt);
        }
    if (pa.size() < 8) throw EditError(QStringLiteral("照片重叠特征不足，无法可靠对齐"));
    cv::Mat inliers;
    auto affine = cv::estimateAffinePartial2D(pa, pb, inliers, cv::RANSAC, 3., 2000, .995);
    if (affine.empty() || cv::countNonZero(inliers) < 8)
        throw EditError(QStringLiteral("照片对齐失败，请选择同一场景的图组"));
    cv::Mat transform = cv::Mat::eye(3, 3, CV_64F);
    affine.copyTo(transform(cv::Rect(0, 0, 3, 2)));
    transform.at<double>(0, 2) /= factor; transform.at<double>(1, 2) /= factor;
    const double determinant = cv::determinant(transform);
    if (!std::isfinite(determinant) || determinant < .25 || determinant > 4)
        throw EditError(QStringLiteral("对齐变换异常，已中止合成"));
    return transform;
}
cv::Rect covered_crop(const cv::Mat& mask) {
    // Largest all-covered rectangle, including rotated image borders; no black corner heuristic.
    std::vector<int> heights(std::size_t(mask.cols), 0);
    cv::Rect best;
    for (int y = 0; y < mask.rows; ++y) {
        for (int x = 0; x < mask.cols; ++x) heights[std::size_t(x)] = mask.at<uchar>(y,x) ? heights[std::size_t(x)] + 1 : 0;
        std::vector<int> stack;
        for (int x = 0; x <= mask.cols; ++x) {
            const int value = x == mask.cols ? 0 : heights[std::size_t(x)];
            while (!stack.empty() && heights[std::size_t(stack.back())] > value) {
                const int h = heights[std::size_t(stack.back())]; stack.pop_back();
                const int left = stack.empty() ? 0 : stack.back()+1;
                if (qint64(x-left)*h > qint64(best.width)*best.height) best = cv::Rect(left,y-h+1,x-left,h);
            }
            stack.push_back(x);
        }
    }
    if (best.empty()) throw EditError(QStringLiteral("照片没有共同覆盖区域"));
    return best;
}
}
AiResult merge_photos(const PhotoMergeOptions& options, const std::atomic_bool* cancelled, const AiProgress& progress) {
    QJsonArray paths, ev;
    for (const auto& path : options.paths) paths.append(path);
    for (const auto value : options.exposure_ev) ev.append(value);
    auto result=run_ai_job({{"operation", "merge"}, {"mode", int(options.mode)}, {"paths", paths}, {"ev", ev},
        {"reference", options.reference}, {"align", options.align}, {"crop", options.crop},
        {"deghost", options.deghost}, {"projection", options.projection}, {"previewEdge", options.preview_edge}, {"directml", false}}, {}, cancelled, progress);
    if(options.preview_edge==0)
        result.metadata.insert("photoMetadata",read_photo_metadata(options.paths.at(options.reference),cancelled));
    return result;
}
AiResult infer_photo_merge(const QJsonObject& request, const AiProgress& progress) {
    static MergeAllocator allocator;
    cv::Mat::setDefaultAllocator(&allocator);
    cv::setNumThreads(4);
    const auto paths = request["paths"].toArray();
    const auto mode = PhotoMergeMode(request["mode"].toInt());
    const int reference = request["reference"].toInt();
    if (paths.size() < 2 || paths.size() > 16 || reference < 0 || reference >= paths.size())
        throw EditError(QStringLiteral("请选择 2–16 张照片和有效参考图"));
    std::vector<cv::Mat> display, linear;
    qint64 pixel_count{};
    for (const auto& item : paths) {
        const auto path = item.toString();
        const auto extension = QFileInfo(path).suffix().toLower();
        QImage image;
        if (extension == "png" || extension == "jpg" || extension == "jpeg" || extension == "tif" || extension == "tiff") image = import_image(path);
        else { const std::atomic_bool cancelled{false}; image = develop_raw(path, RawOptions{}, cancelled).image; }
        pixel_count += qint64(image.width()) * image.height();
        if (pixel_count > maximum_image_pixels * 16) throw EditError(QStringLiteral("图组总像素超过本次合成内存预算，请减少照片数量"));
        const int edge=request["previewEdge"].toInt();
        if(edge>0 && std::max(image.width(),image.height())>edge)image=image.scaled(edge,edge,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        display.push_back(display_bgr(image)); linear.push_back(rgba(image));
        if (progress) progress(int(linear.size()), int(paths.size()) + 3);
    }
    QJsonObject metadata{{"sourceCount", int(paths.size())}, {"mode", int(mode)}, {"reference", reference}};
    if (mode == PhotoMergeMode::panorama) {
        auto stitcher = cv::Stitcher::create(cv::Stitcher::PANORAMA);
        stitcher->setRegistrationResol(.8);
        stitcher->setPanoConfidenceThresh(.5);
        if (stitcher->estimateTransform(display) != cv::Stitcher::OK || stitcher->component().size() != linear.size())
            throw EditError(QStringLiteral("全景对齐失败：照片必须有足够重叠，且全部属于同一全景"));
        const auto cameras = stitcher->cameras();
        std::vector<double> focal;
        for (const auto& camera : cameras) focal.push_back(camera.focal / stitcher->workScale());
        std::sort(focal.begin(), focal.end());
        cv::Ptr<cv::WarperCreator> creator;
        if (request["projection"].toInt() == 1) creator = cv::makePtr<cv::CylindricalWarper>();
        else if (request["projection"].toInt() == 2) creator = cv::makePtr<cv::PlaneWarper>();
        else creator = cv::makePtr<cv::SphericalWarper>();
        auto warper = creator->create(float(focal[focal.size()/2]));
        std::vector<cv::Point> corners;
        std::vector<cv::Mat> warped, coverage;
        std::vector<cv::UMat> seam_images, seams;
        cv::Rect bounds;
        for (std::size_t i = 0; i < linear.size(); ++i) {
            cv::Mat k; cameras[i].K().convertTo(k, CV_32F);
            k.at<float>(0,0) /= float(stitcher->workScale()); k.at<float>(0,2) /= float(stitcher->workScale());
            k.at<float>(1,1) /= float(stitcher->workScale()); k.at<float>(1,2) /= float(stitcher->workScale());
            const auto roi = warper->warpRoi(linear[i].size(), k, cameras[i].R);
            bounds = i == 0 ? roi : bounds | roi; size_guard(bounds.size());
            cv::Mat pixels, mask, preview;
            const auto corner = warper->warp(linear[i], k, cameras[i].R, cv::INTER_LINEAR, cv::BORDER_CONSTANT, pixels);
            warper->warp(cv::Mat(linear[i].size(), CV_8U, cv::Scalar(255)), k, cameras[i].R, cv::INTER_NEAREST, cv::BORDER_CONSTANT, mask);
            warper->warp(display[i], k, cameras[i].R, cv::INTER_LINEAR, cv::BORDER_REFLECT, preview);
            preview.convertTo(preview, CV_32F);
            warped.push_back(pixels); coverage.push_back(mask); corners.push_back(corner);
            seam_images.push_back(preview.getUMat(cv::ACCESS_READ).clone()); seams.push_back(mask.getUMat(cv::ACCESS_READ).clone());
        }
        if (progress) progress(int(paths.size()) + 1, int(paths.size()) + 3);
        // Exposure gains from overlapping linear luminance, before graph-cut seam choice.
        std::vector<float> gains(linear.size(), 1.F);
        for (std::size_t i = 1; i < warped.size(); ++i) {
            const auto overlap = cv::Rect(corners[i-1],warped[i-1].size()) & cv::Rect(corners[i],warped[i].size());
            double a{}, b{}; int count{};
            for (int y=overlap.y; y<overlap.br().y; y+=4)
                for (int x=overlap.x; x<overlap.br().x; x+=4) {
                    const auto p=warped[i-1].at<cv::Vec4f>(y-corners[i-1].y,x-corners[i-1].x);
                    const auto q=warped[i].at<cv::Vec4f>(y-corners[i].y,x-corners[i].x);
                    if (p[3]>.99F && q[3]>.99F) { a+=(p[0]+p[1]+p[2])*gains[i-1]; b+=q[0]+q[1]+q[2]; ++count; }
                }
            if (count > 50 && b>1e-6) gains[i]=float(std::clamp(a/b,.5,2.));
        }
        const double seam_scale=std::min(1.,1600./std::max(bounds.width,bounds.height));
        std::vector<cv::Point> seam_corners;std::vector<cv::Size> seam_sizes;
        for(std::size_t i=0;i<seams.size();++i) {
            seam_sizes.push_back(seams[i].size());
            seam_corners.emplace_back(cvRound(corners[i].x*seam_scale),cvRound(corners[i].y*seam_scale));
            cv::resize(seam_images[i],seam_images[i],{},seam_scale,seam_scale,cv::INTER_AREA);
            cv::resize(seams[i],seams[i],seam_images[i].size(),0,0,cv::INTER_NEAREST);
        }
        cv::detail::GraphCutSeamFinder finder("COST_COLOR_GRAD"); finder.find(seam_images,seam_corners,seams);
        for(std::size_t i=0;i<seams.size();++i)cv::resize(seams[i],seams[i],seam_sizes[i],0,0,cv::INTER_NEAREST);
        cv::Mat output(bounds.size(),CV_32FC4,cv::Scalar(0)), weights(bounds.size(),CV_32F,cv::Scalar(0));
        cv::Mat covered(bounds.size(),CV_8U,cv::Scalar(0));
        for (std::size_t i=0;i<warped.size();++i) {
            cv::Mat mask = seams[i].getMat(cv::ACCESS_READ).clone(), weight;
            cv::dilate(mask,mask,cv::getStructuringElement(cv::MORPH_ELLIPSE,{9,9}));
            mask.convertTo(weight,CV_32F,1./255); cv::GaussianBlur(weight,weight,{0,0},3);
            for (int y=0;y<warped[i].rows;++y)
                for (int x=0;x<warped[i].cols;++x) {
                    if (!coverage[i].at<uchar>(y,x)) continue;
                    const int ox=x+corners[i].x-bounds.x, oy=y+corners[i].y-bounds.y;
                    const float w=weight.at<float>(y,x);
                    auto pixel=warped[i].at<cv::Vec4f>(y,x);
                    for(int c=0;c<3;++c)pixel[c]*=gains[i];
                    output.at<cv::Vec4f>(oy,ox)+=pixel*w; weights.at<float>(oy,ox)+=w; covered.at<uchar>(oy,ox)=255;
                }
        }
        for (int y=0;y<output.rows;++y) for(int x=0;x<output.cols;++x) {
            const float w=weights.at<float>(y,x);
            if(w>1e-8F) output.at<cv::Vec4f>(y,x)/=w;
            else covered.at<uchar>(y,x)=0;
        }
        if(request["crop"].toBool())output=output(covered_crop(covered)).clone();
        metadata["projection"]=request["projection"];
        if(progress)progress(int(paths.size())+3,int(paths.size())+3);
        return {image_from(output),metadata,{}};
    }
    const auto size=linear[std::size_t(reference)].size();
    std::vector<cv::Mat> aligned, masks;
    for(std::size_t i=0;i<linear.size();++i) {
        if(linear[i].size()!=size)throw EditError(QStringLiteral("HDR / 景深图组必须具有相同像素尺寸"));
        auto transform=cv::Mat::eye(3,3,CV_64F);
        cv::Mat matrix=transform;
        if(int(i)!=reference && request["align"].toBool())matrix=alignment(display[i],display[std::size_t(reference)]);
        cv::Mat pixels,mask;
        cv::warpPerspective(linear[i],pixels,matrix,size,cv::INTER_LINEAR,cv::BORDER_CONSTANT);
        cv::warpPerspective(cv::Mat(size,CV_8U,cv::Scalar(255)),mask,matrix,size,cv::INTER_NEAREST,cv::BORDER_CONSTANT);
        aligned.push_back(pixels);masks.push_back(mask);
    }
    if(progress)progress(int(paths.size())+1,int(paths.size())+3);
    cv::Mat output(size,CV_32FC4,cv::Scalar(0));
    QImage deghost_mask;
    cv::Mat covered(size,CV_8U,cv::Scalar(255));
    for(const auto& mask:masks)cv::bitwise_and(covered,mask,covered);
    if(mode==PhotoMergeMode::hdr) {
        deghost_mask=allocate_image(size.width,size.height,QImage::Format_Grayscale8);deghost_mask.fill(0);
        const auto ev=request["ev"].toArray();
        if(ev.size()!=paths.size())throw EditError(QStringLiteral("HDR 需要每张照片的相对曝光 EV"));
        std::vector<float> factors;
        for(const auto& value:ev) {
            const double offset=value.toDouble(999)-ev[reference].toDouble();
            if(!std::isfinite(offset)||std::abs(offset)>20)throw EditError(QStringLiteral("曝光 EV 无效"));
            factors.push_back(float(std::exp2(offset)));
        }
        const double threshold=request["deghost"].toInt()==1?.4:request["deghost"].toInt()==2?.2:.1;
        int ghosts{};
        for(int y=0;y<size.height;++y)for(int x=0;x<size.width;++x) {
            cv::Vec4f sum{};float weight{};
            const auto ref=aligned[std::size_t(reference)].at<cv::Vec4f>(y,x);
            const float ref_luma=(ref[0]+ref[1]+ref[2])/3;
            bool ghost{};
            for(std::size_t i=0;i<aligned.size();++i) {
                if(!masks[i].at<uchar>(y,x))continue;
                auto p=aligned[i].at<cv::Vec4f>(y,x);
                const float luma=(p[0]+p[1]+p[2])/3;
                const float signal=luma/factors[i];
                if(request["deghost"].toInt()>0 && luma>.015F && luma<.9F && ref_luma>.015F && ref_luma<.9F &&
                    std::abs(signal-ref_luma)>threshold*std::max(.05F,ref_luma))ghost=true;
                const float maximum=std::max({p[0],p[1],p[2]});
                const float w=maximum >= .99F ? .000001F : std::max(.000001F,std::min(luma,1.F-maximum));
                for(int c=0;c<3;++c)sum[c]+=p[c]/factors[i]*w;
                sum[3]+=p[3]*w;weight+=w;
            }
            if(ghost){output.at<cv::Vec4f>(y,x)=ref;++ghosts;deghost_mask.scanLine(y)[x]=255;}
            else if(weight>0)output.at<cv::Vec4f>(y,x)=sum/weight;
        }
        metadata["hdrLinear"]=true;metadata["deghostPixels"]=ghosts;
    } else if(mode==PhotoMergeMode::focus) {
        std::vector<cv::Mat> scores;
        for(const auto& pixels:aligned) {
            cv::Mat gray,laplacian,energy;
            cv::cvtColor(pixels,gray,cv::COLOR_RGBA2GRAY);
            cv::Laplacian(gray,laplacian,CV_32F,3);
            cv::multiply(laplacian,laplacian,energy);cv::GaussianBlur(energy,energy,{0,0},2);
            scores.push_back(energy);
        }
        cv::Mat labels(size,CV_8U,cv::Scalar(reference));
        for(int y=0;y<size.height;++y)for(int x=0;x<size.width;++x) {
            float best=scores[std::size_t(reference)].at<float>(y,x);std::size_t chosen=std::size_t(reference);
            for(std::size_t i=0;i<scores.size();++i)
                if(masks[i].at<uchar>(y,x) && scores[i].at<float>(y,x)>best){best=scores[i].at<float>(y,x);chosen=i;}
            labels.at<uchar>(y,x)=uchar(chosen);
        }
        cv::Mat weights(size,CV_32F,cv::Scalar(0));
        for(std::size_t i=0;i<aligned.size();++i) {
            cv::Mat mask,weight;cv::compare(labels,int(i),mask,cv::CMP_EQ);
            mask.convertTo(weight,CV_32F,1./255);cv::GaussianBlur(weight,weight,{0,0},1.2);
            for(int y=0;y<size.height;++y)for(int x=0;x<size.width;++x) {
                if(!masks[i].at<uchar>(y,x))continue;
                const float w=weight.at<float>(y,x);output.at<cv::Vec4f>(y,x)+=aligned[i].at<cv::Vec4f>(y,x)*w;weights.at<float>(y,x)+=w;
            }
        }
        for(int y=0;y<size.height;++y)for(int x=0;x<size.width;++x)
            if(weights.at<float>(y,x)>0)output.at<cv::Vec4f>(y,x)/=weights.at<float>(y,x);
    } else throw EditError(QStringLiteral("未知合成方式"));
    if(request["crop"].toBool()){const auto crop=covered_crop(covered);output=output(crop).clone();
        if(!deghost_mask.isNull())deghost_mask=deghost_mask.copy(crop.x,crop.y,crop.width,crop.height);}
    if(progress)progress(int(paths.size())+3,int(paths.size())+3);
    return {image_from(output),metadata,deghost_mask};
}
}
