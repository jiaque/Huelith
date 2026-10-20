#include "huelith/editing/raw_camera.h"
#include "huelith/editing/dng.h"
#include "huelith/editing/image_storage.h"
#include "huelith/editing/raw_recipe.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtEndian>
#include <libraw/libraw.h>
#include <tiffio.h>
#include <array>
#include <cmath>
#include <numeric>
#include <memory>

namespace huelith::editing {
namespace {
using Tiff = std::unique_ptr<TIFF, decltype(&TIFFClose)>;
TIFF* device_tiff(QIODevice& device, const char* mode) {
    return TIFFClientOpen("Huelith DNG",mode,&device,
        [](thandle_t h,void* p,tmsize_t n)->tmsize_t{return static_cast<QIODevice*>(h)->read(static_cast<char*>(p),n);},
        [](thandle_t h,void* p,tmsize_t n)->tmsize_t{return static_cast<QIODevice*>(h)->write(static_cast<const char*>(p),n);},
        [](thandle_t h,toff_t offset,int origin)->toff_t{
            auto* file=static_cast<QIODevice*>(h);const qint64 position=qint64(offset)+(origin==SEEK_CUR?file->pos():origin==SEEK_END?file->size():0);
            return position>=0&&file->seek(position)?toff_t(position):toff_t(-1);
        },[](thandle_t){return 0;},[](thandle_t h)->toff_t{return toff_t(static_cast<QIODevice*>(h)->size());},
        [](thandle_t,void**,toff_t*){return 0;},[](thandle_t,void*,toff_t){});
}
void check(int value){if(value!=1)throw EditError(QStringLiteral("DNG 标签或像素写入失败"));}
void cancel(const std::atomic_bool* flag){if(flag&&flag->load())throw EditError(QStringLiteral("保存 DNG 已取消"));}
void unpack(LibRaw& decoder,const QByteArray& bytes) {
    if(bytes.isEmpty()||bytes.size()>512LL*1024*1024)throw EditError(QStringLiteral("RAW 源数据大小无效"));
    int result=decoder.open_buffer(const_cast<char*>(bytes.constData()),std::size_t(bytes.size()));
    if(result==LIBRAW_SUCCESS)result=decoder.unpack();
    if(result!=LIBRAW_SUCCESS)throw EditError(QString::fromUtf8(libraw_strerror(result)));
    const auto& data=decoder.imgdata;
    if(!data.rawdata.raw_image||!data.idata.filters||data.idata.colors!=3||
        qint64(data.sizes.raw_width)*data.sizes.raw_height>maximum_image_pixels||data.sizes.raw_pitch<data.sizes.raw_width*2u||
        data.color.cblack[4]>64||data.color.cblack[5]>64)
        throw EditError(QStringLiteral("DNG 转存目前支持 RGB Bayer / X-Trans 传感器数据；此布局请保存工程或导出 TIFF"));
}
QByteArray sensor_digest(const LibRaw& decoder) {
    QCryptographicHash hash(QCryptographicHash::Sha256);const auto& data=decoder.imgdata;
    for(int y=0;y<data.sizes.raw_height;++y)
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(data.rawdata.raw_image)+qsizetype(y)*data.sizes.raw_pitch,qsizetype(data.sizes.raw_width)*2));
    return hash.result().toHex();
}
QByteArray recipe_packet(const QJsonObject& recipe) {
    RawGroups groups;groups.fill(true);
    (void)read_raw_preset(QJsonDocument(raw_preset(QStringLiteral("DNG"),recipe,groups)).toJson(QJsonDocument::Compact));
    const auto json=QJsonDocument(recipe).toJson(QJsonDocument::Compact);
    if(json.size()>32*1024*1024)throw EditError(QStringLiteral("DNG 显影设置超过 32 MiB"));
    QByteArray packet;QXmlStreamWriter xml(&packet);
    xml.writeStartElement("x:xmpmeta");xml.writeNamespace("adobe:ns:meta/","x");
    xml.writeStartElement("rdf:RDF");xml.writeNamespace("http://www.w3.org/1999/02/22-rdf-syntax-ns#","rdf");
    xml.writeStartElement("rdf:Description");xml.writeAttribute("rdf:about","");xml.writeNamespace("urn:huelith:raw:1.0","huelith");
    xml.writeTextElement("huelith:Version","1");xml.writeTextElement("huelith:Recipe",QString::fromLatin1(qCompress(json,6).toBase64()));
    xml.writeEndElement();xml.writeEndElement();xml.writeEndElement();return packet;
}
}
QJsonObject raw_sensor_info(const QByteArray& bytes) {
    LibRaw decoder;unpack(decoder,bytes);const auto& data=decoder.imgdata;
    QJsonArray black,gains,matrix;
    for(int i=0;i<8;++i)black.append(double(data.color.cblack[i]));
    for(int i=0;i<4;++i)gains.append(double(data.color.cam_mul[i]));
    for(int y=0;y<3;++y)for(int x=0;x<3;++x)matrix.append(double(data.color.cam_xyz[y][x]));
    return {{"sensorSha256",QString::fromLatin1(sensor_digest(decoder))},{"rawWidth",data.sizes.raw_width},
        {"black",double(data.color.black)},{"cblack",black},{"gains",gains},{"matrix",matrix},
        {"rawHeight",data.sizes.raw_height},{"width",data.sizes.width},{"height",data.sizes.height},
        {"camera",QString::fromUtf8(data.idata.model)},{"white",double(data.color.maximum)}};
}
void save_raw_dng(const Layer& source,const QString& path,const std::atomic_bool* cancelled) {
    cancel(cancelled);
    const auto output=QFileInfo(path).absoluteFilePath();
    if(!path.endsWith(".dng",Qt::CaseInsensitive))throw EditError(QStringLiteral("请选择 .dng 文件名"));
    if(!source.linked_path.isEmpty() &&
        (output.compare(QFileInfo(source.linked_path).absoluteFilePath(),Qt::CaseInsensitive)==0 ||
         (QFileInfo::exists(output)&&QFileInfo(output).canonicalFilePath().compare(QFileInfo(source.linked_path).canonicalFilePath(),Qt::CaseInsensitive)==0)))
        throw EditError(QStringLiteral("DNG 必须保存为副本，不能覆盖 RAW 原片"));
    LibRaw decoder;unpack(decoder,source.raw_source);cancel(cancelled);
    normalize_raw_neutral(decoder.imgdata.color);
    normalize_raw_matrix(decoder.imgdata.color);
    const auto& data=decoder.imgdata;
    const auto packet=recipe_packet(source.raw_recipe);
    QSaveFile file(output);file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly))throw EditError(file.errorString());
    Tiff handle(device_tiff(file,"wl"),TIFFClose);if(!handle)throw EditError(QStringLiteral("无法创建 DNG"));
    auto* tiff=handle.get();
    // Standard EXIF fields are retained independently from our versioned development recipe.
    uint64_t exif_offset{};
    if(data.other.shutter>0 || data.other.aperture>0 || data.other.focal_len>0 || data.other.iso_speed>0 || data.lens.Lens[0]) {
    TIFFCreateEXIFDirectory(tiff);
    if(data.other.shutter>0)check(TIFFSetField(tiff,EXIFTAG_EXPOSURETIME,double(data.other.shutter)));
    if(data.other.aperture>0)check(TIFFSetField(tiff,EXIFTAG_FNUMBER,double(data.other.aperture)));
    if(data.other.focal_len>0)check(TIFFSetField(tiff,EXIFTAG_FOCALLENGTH,double(data.other.focal_len)));
    uint16_t iso=uint16_t(std::clamp(double(data.other.iso_speed),0.,65535.));
    if(iso)check(TIFFSetField(tiff,EXIFTAG_ISOSPEEDRATINGS,1,&iso));
    if(data.lens.Lens[0])check(TIFFSetField(tiff,EXIFTAG_LENSMODEL,data.lens.Lens));
    const auto date=source.source_metadata["ExifIFD:DateTimeOriginal"].toString().toUtf8();
    if(date.size()>=19)check(TIFFSetField(tiff,EXIFTAG_DATETIMEORIGINAL,date.constData()));
    check(TIFFWriteCustomDirectory(tiff,&exif_offset));
    }
    TIFFCreateDirectory(tiff);
    if(exif_offset)check(TIFFSetField(tiff,TIFFTAG_EXIFIFD,exif_offset));
    check(TIFFSetField(tiff,TIFFTAG_IMAGEWIDTH,uint32_t(data.sizes.raw_width)));
    check(TIFFSetField(tiff,TIFFTAG_IMAGELENGTH,uint32_t(data.sizes.raw_height)));
    check(TIFFSetField(tiff,TIFFTAG_BITSPERSAMPLE,16));check(TIFFSetField(tiff,TIFFTAG_SAMPLESPERPIXEL,1));
    check(TIFFSetField(tiff,TIFFTAG_SAMPLEFORMAT,SAMPLEFORMAT_UINT));check(TIFFSetField(tiff,TIFFTAG_PLANARCONFIG,PLANARCONFIG_CONTIG));
    check(TIFFSetField(tiff,TIFFTAG_COMPRESSION,COMPRESSION_NONE));check(TIFFSetField(tiff,TIFFTAG_PHOTOMETRIC,PHOTOMETRIC_CFA));
    check(TIFFSetField(tiff,TIFFTAG_ROWSPERSTRIP,32));check(TIFFSetField(tiff,TIFFTAG_SUBFILETYPE,0));
    const std::array<uint16_t,8> orientations{1,2,4,3,5,8,6,7};
    check(TIFFSetField(tiff,TIFFTAG_ORIENTATION,orientations[std::size_t(std::clamp(data.sizes.flip,0,7))]));
    const std::array<uint8_t,4> version{1,4,0,0},backward{1,1,0,0};
    check(TIFFSetField(tiff,TIFFTAG_DNGVERSION,version.data()));check(TIFFSetField(tiff,TIFFTAG_DNGBACKWARDVERSION,backward.data()));
    const auto model=(QString::fromUtf8(data.idata.make)+" "+QString::fromUtf8(data.idata.model)).toUtf8();
    check(TIFFSetField(tiff,TIFFTAG_UNIQUECAMERAMODEL,model.constData()));
    check(TIFFSetField(tiff,TIFFTAG_MAKE,data.idata.make));check(TIFFSetField(tiff,TIFFTAG_MODEL,data.idata.model));
    check(TIFFSetField(tiff,TIFFTAG_SOFTWARE,"Huelith"));
    const auto copyright=source.source_metadata["IFD0:Copyright"].toString().toUtf8();
    const auto artist=source.source_metadata["IFD0:Artist"].toString().toUtf8();
    if(!copyright.isEmpty())check(TIFFSetField(tiff,TIFFTAG_COPYRIGHT,copyright.constData()));
    if(!artist.isEmpty())check(TIFFSetField(tiff,TIFFTAG_ARTIST,artist.constData()));
    const int period=data.idata.filters==9?6:2;
    const auto channel=[&](int y,int x){return period==6?int(data.idata.xtrans_abs[y%6][x%6]):decoder.COLOR(y,x);};
    const std::array<uint16_t,2> repeat{uint16_t(period),uint16_t(period)};std::vector<uint8_t> pattern(period*period);
    for(int y=0;y<period;++y)for(int x=0;x<period;++x){const char letter=data.idata.cdesc[channel(y,x)];
        if(letter!='R'&&letter!='G'&&letter!='B')throw EditError(QStringLiteral("RAW CFA 色彩排列不受支持"));
        pattern[std::size_t(y)*period+x]=uint8_t(letter=='R'?0:letter=='G'?1:2);}
    const std::array<uint8_t,3> colors{0,1,2};
    check(TIFFSetField(tiff,TIFFTAG_CFAREPEATPATTERNDIM,repeat.data()));check(TIFFSetField(tiff,TIFFTAG_CFAPATTERN,int(pattern.size()),pattern.data()));
    check(TIFFSetField(tiff,TIFFTAG_CFAPLANECOLOR,3,colors.data()));check(TIFFSetField(tiff,TIFFTAG_CFALAYOUT,1));
    const int rows=data.color.cblack[4]?std::lcm(period,int(data.color.cblack[4])):period;
    const int columns=data.color.cblack[5]?std::lcm(period,int(data.color.cblack[5])):period;
    if(rows>128||columns>128)throw EditError(QStringLiteral("RAW 黑电平模式过大"));
    std::vector<float> black(std::size_t(rows)*columns);
    for(int y=0;y<rows;++y)for(int x=0;x<columns;++x){
        double value=data.color.black+data.color.cblack[channel(y,x)];
        if(data.color.cblack[4]&&data.color.cblack[5])value+=data.color.cblack[6+(y%data.color.cblack[4])*data.color.cblack[5]+x%data.color.cblack[5]];
        black[std::size_t(y)*columns+x]=float(value);
    }
    const std::array<uint16_t,2> black_repeat{uint16_t(rows),uint16_t(columns)};
    check(TIFFSetField(tiff,TIFFTAG_BLACKLEVELREPEATDIM,black_repeat.data()));check(TIFFSetField(tiff,TIFFTAG_BLACKLEVEL,int(black.size()),black.data()));
    const uint32_t white=data.color.maximum;check(TIFFSetField(tiff,TIFFTAG_WHITELEVEL,1,&white));
    std::array<float,9> matrix{};std::array<float,3> neutral{};
    for(int y=0;y<3;++y){
        if(!std::isfinite(data.color.cam_mul[y])||data.color.cam_mul[y]<=0)throw EditError(QStringLiteral("RAW 白平衡元数据无效"));
        neutral[y]=data.color.cam_mul[1]/data.color.cam_mul[y];
        for(int x=0;x<3;++x)matrix[std::size_t(y)*3+x]=data.color.cam_xyz[y][x];
    }
    check(TIFFSetField(tiff,TIFFTAG_COLORMATRIX1,9,matrix.data()));check(TIFFSetField(tiff,TIFFTAG_CALIBRATIONILLUMINANT1,21));
    check(TIFFSetField(tiff,TIFFTAG_ASSHOTNEUTRAL,3,neutral.data()));
    const std::array<uint32_t,4> active{data.sizes.top_margin,data.sizes.left_margin,
        uint32_t(data.sizes.top_margin)+data.sizes.height,uint32_t(data.sizes.left_margin)+data.sizes.width};
    const std::array<float,2> origin{0,0},size{float(data.sizes.width),float(data.sizes.height)},scale{1,1};
    check(TIFFSetField(tiff,TIFFTAG_ACTIVEAREA,active.data()));check(TIFFSetField(tiff,TIFFTAG_DEFAULTCROPORIGIN,origin.data()));
    check(TIFFSetField(tiff,TIFFTAG_DEFAULTCROPSIZE,size.data()));check(TIFFSetField(tiff,TIFFTAG_DEFAULTSCALE,scale.data()));
    check(TIFFSetField(tiff,TIFFTAG_XMLPACKET,uint32_t(packet.size()),packet.constData()));
    for(int y=0;y<data.sizes.raw_height;++y){cancel(cancelled);
        auto* row=reinterpret_cast<char*>(data.rawdata.raw_image)+qsizetype(y)*data.sizes.raw_pitch;
        if(TIFFWriteScanline(tiff,row,uint32_t(y),0)<0)throw EditError(QStringLiteral("DNG 像素写入失败"));
    }
    check(TIFFWriteDirectory(tiff));handle.reset();cancel(cancelled);
    if(!file.commit())throw EditError(QStringLiteral("DNG 原子保存失败：%1").arg(file.errorString()));
}
QJsonObject dng_recipe(const QByteArray& bytes) {
    if(!bytes.startsWith("II")&&!bytes.startsWith("MM"))return {};
    QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);
    Tiff handle(device_tiff(buffer,"r"),TIFFClose);if(!handle)return {};
    void* version{};if(!TIFFGetField(handle.get(),TIFFTAG_DNGVERSION,&version))return {};
    uint32_t length{};void* payload{};
    if(!TIFFGetField(handle.get(),TIFFTAG_XMLPACKET,&length,&payload))return {};
    if(length>48*1024*1024)throw EditError(QStringLiteral("DNG 设置包过大"));
    QXmlStreamReader reader(QByteArray(static_cast<const char*>(payload),length));
    QByteArray encoded;QString revision;
    while(!reader.atEnd()){
        reader.readNext();if(reader.isDTD())throw EditError(QStringLiteral("DNG 设置不支持 DTD"));
        if(reader.isStartElement()&&reader.namespaceUri()==QStringLiteral("urn:huelith:raw:1.0")){
            if(reader.name()==QStringLiteral("Version"))revision=reader.readElementText();
            else if(reader.name()==QStringLiteral("Recipe"))encoded=reader.readElementText().toLatin1();
        }
    }
    if(encoded.isEmpty())return {};
    if(reader.hasError()||revision!="1")throw EditError(QStringLiteral("DNG 显影设置损坏或版本不支持"));
    const auto compressed=QByteArray::fromBase64(encoded,QByteArray::AbortOnBase64DecodingErrors);
    if(compressed.size()<4||qFromBigEndian<uint32_t>(compressed.constData())>32*1024*1024)throw EditError(QStringLiteral("DNG 显影设置大小无效"));
    QJsonParseError error;const auto document=QJsonDocument::fromJson(qUncompress(compressed),&error);
    if(error.error!=QJsonParseError::NoError||!document.isObject())throw EditError(QStringLiteral("DNG 显影设置无法读取"));
    (void)recipe_packet(document.object());return document.object();
}
}
