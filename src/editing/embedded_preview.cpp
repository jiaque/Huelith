#include "huelith/editing/image_storage.h"
#include "huelith/editing/embedded_preview.h"
#include "huelith/editing/metadata.h"
#include <QBuffer>
#include <QImageReader>
#include <QImageIOHandler>
#include <libraw/libraw.h>
#include <algorithm>
#include <cmath>
namespace huelith::editing {
EmbeddedPreview embedded_preview(const Layer& source) {
    if(source.raw_source.isEmpty()||source.raw_source.size()>512LL*1024*1024)
        throw EditError(QStringLiteral("没有可读取的 RAW 内嵌预览；保留当前显影设置"));
    if(source.source_metadata["Camera:HDRPQ"].toInt()==1)
        throw EditError(QStringLiteral("检测到 Canon HDR-PQ 预览，不能作为 sRGB 预览或亮度参考；请继续 RAW 显影"));
    LibRaw decoder;int result=decoder.open_buffer(const_cast<char*>(source.raw_source.constData()),std::size_t(source.raw_source.size()));
    if(result==LIBRAW_SUCCESS)result=decoder.unpack_thumb();
    if(result!=LIBRAW_SUCCESS){
        const auto bytes=extract_embedded_jpeg(source.raw_source);QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);reader.setAutoTransform(true);const auto dimensions=reader.size();
        if(!dimensions.isValid()||qint64(dimensions.width())*dimensions.height()>huelith::editing::maximum_image_pixels)
            throw EditError(QStringLiteral("RAW 解码不可用且没有可读取的内嵌预览；保留当前显影设置"));
        auto image=reader.read();if(image.isNull())throw EditError(QStringLiteral("内嵌 JPEG 解码失败"));
        if(!image.colorSpace().isValid())image.setColorSpace(QColorSpace::SRgb);
        return {to_working(image),8,QStringLiteral("内嵌 JPEG 预览 · 8 位 · %1 × %2 · 非 RAW，动态范围受限").arg(image.width()).arg(image.height())};
    }
    const auto& thumb=decoder.imgdata.thumbnail;
    if(source.source_metadata["Camera:HDRPQ"].toInt()==1||thumb.tformat==LIBRAW_THUMBNAIL_H265)
        throw EditError(QStringLiteral("检测到 Canon HDR-PQ / H.265 预览，不能当作 sRGB 预览或亮度参考；请继续 RAW 显影或使用支持 PQ 的解码器"));
    if(!thumb.thumb||!thumb.tlength||thumb.tlength>64*1024*1024)throw EditError(QStringLiteral("内嵌预览为空或超限"));
    QImage image;int bits=8;bool oriented{};
    if(thumb.tformat==LIBRAW_THUMBNAIL_JPEG){
        QByteArray bytes(thumb.thumb,qsizetype(thumb.tlength));QBuffer buffer(&bytes);buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);reader.setAutoTransform(true);oriented=reader.transformation()!=QImageIOHandler::TransformationNone;
        if(qint64(reader.size().width())*reader.size().height()>huelith::editing::maximum_image_pixels)throw EditError(QStringLiteral("内嵌预览尺寸超限"));image=reader.read();
    }else if((thumb.tformat==LIBRAW_THUMBNAIL_BITMAP||thumb.tformat==LIBRAW_THUMBNAIL_BITMAP16)&&thumb.tcolors==3){
        bits=thumb.tformat==LIBRAW_THUMBNAIL_BITMAP16?16:8;
        if(qint64(thumb.twidth)*thumb.theight>huelith::editing::maximum_image_pixels||qint64(thumb.twidth)*thumb.theight*3*(bits/8)>thumb.tlength)
            throw EditError(QStringLiteral("内嵌预览数据损坏"));
        image=QImage(thumb.twidth,thumb.theight,QImage::Format_RGB888);
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width()*3;++x)
            image.scanLine(y)[x]=bits==8?reinterpret_cast<const uchar*>(thumb.thumb)[qsizetype(y)*image.width()*3+x]:
                uchar(reinterpret_cast<const uint16_t*>(thumb.thumb)[qsizetype(y)*image.width()*3+x]>>8);
        bits=8;
    }
    if(image.isNull())throw EditError(QStringLiteral("内嵌预览编码暂不支持；没有将其当作 RAW 解码成功"));
    if(!oriented){const int flip=decoder.imgdata.sizes.flip;
        if(flip==3)image=image.transformed(QTransform().rotate(180));
        else if(flip==5)image=image.transformed(QTransform().rotate(-90));
        else if(flip==6)image=image.transformed(QTransform().rotate(90));}
    if(!image.colorSpace().isValid())image.setColorSpace(QColorSpace::SRgb);
    return {to_working(image),bits,QStringLiteral("内嵌相机预览 · %1 位 RGB · %2 × %3 · 非 RAW，动态范围受限").arg(bits).arg(image.width()).arg(image.height())};
}
double preview_exposure_reference(const QImage& developed,const QImage& preview) {
    const auto median=[](const QImage& input){
        const auto image=to_working(input).scaled(192,192,Qt::KeepAspectRatio,Qt::SmoothTransformation);std::vector<double> values;
        for(int y=0;y<image.height();++y){const auto* row=reinterpret_cast<const float*>(image.constScanLine(y));for(int x=0;x<image.width();++x){
            const auto* p=row+x*4;const double luma=.2627*p[0]+.6780*p[1]+.0593*p[2];if(p[3]>.5&&std::isfinite(luma)&&luma>1e-7)values.push_back(luma);}}
        if(values.size()<16)throw EditError(QStringLiteral("预览中缺少足够的有效中间调；保留当前显影设置"));
        const auto mid=values.begin()+values.size()/2;std::nth_element(values.begin(),mid,values.end());return *mid;
    };
    return std::clamp(std::log2(median(preview)/median(developed)),-3.,3.);
}
}
