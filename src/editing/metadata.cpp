#include "huelith/editing/image_storage.h"
#include "huelith/editing/metadata.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>

namespace huelith::editing {
namespace {
QByteArray execute(QStringList arguments,const std::atomic_bool* cancelled) {
    const auto exe=qEnvironmentVariable("HUELITH_EXIFTOOL_PATH",
        QCoreApplication::applicationDirPath()+"/tools/exiftool/exiftool.exe");
    if(!QFileInfo::exists(exe))throw EditError(QStringLiteral("内置元数据组件缺失，请使用完整安装包"));
    QProcess process;process.setProgram(exe);
    arguments.prepend("");arguments.prepend("-config");
    process.setArguments(arguments);process.start();
    if(!process.waitForStarted(5000))throw EditError(process.errorString());
    QByteArray output,error;QElapsedTimer elapsed;elapsed.start();
    do {
        process.waitForFinished(25);output+=process.readAllStandardOutput();error+=process.readAllStandardError();
        if((cancelled&&cancelled->load())||elapsed.elapsed()>30000||output.size()>16*1024*1024||error.size()>65536){
            process.kill();process.waitForFinished();throw EditError(QStringLiteral("元数据操作已取消或超过资源限额"));
        }
    }while(process.state()!=QProcess::NotRunning);
    if(process.exitStatus()!=QProcess::NormalExit||process.exitCode()!=0)
        throw EditError(QStringLiteral("元数据处理失败：%1").arg(QString::fromUtf8(error).left(2000)));
    return output;
}
bool rights(const QString& name) {
    return name.contains(QRegularExpression("Copyright|Rights|UsageTerms|Artist|Author|Creator|Owner",QRegularExpression::CaseInsensitiveOption));
}
bool safe_tag(const QString& key) {
    const auto group=key.section(':',0,0),name=key.section(':',1);
    if(name=="XResolution"||name=="YResolution"||name=="ResolutionUnit")return false;
    if(group!="IFD0"&&group!="ExifIFD"&&group!="GPS"&&group!="IPTC"&&!group.startsWith("XMP-"))return false;
    if(group=="XMP-crs"||group.startsWith("XMP-huelith"))return false;
    // Never copy raw layout, orientation, thumbnails, dimensions or old rendering descriptions.
    static const QRegularExpression excluded("^(ImageWidth|ImageHeight|ExifImageWidth|ExifImageHeight|Orientation|ColorSpace|Software|ProcessingSoftware|BitsPerSample|Compression|PhotometricInterpretation|SamplesPerPixel|PlanarConfiguration|RowsPerStrip|Strip.*|Tile.*|SubfileType|.*Offset|.*ByteCounts|.*Thumbnail.*|Preview.*|JpgFromRaw.*|Raw.*|DNG.*|CFA.*|BlackLevel.*|WhiteLevel|ColorMatrix.*|CameraCalibration.*|AsShot.*|Baseline.*|Default.*|ActiveArea|Opcode.*|.*Digest|.*History|.*DerivedFrom|DocumentID|InstanceID|OriginalDocumentID)$",QRegularExpression::CaseInsensitiveOption);
    return !excluded.match(name).hasMatch();
}
QJsonValue without_gps(const QJsonValue& value){
    if(value.isArray()){QJsonArray result;for(const auto& item:value.toArray())result.append(without_gps(item));return result;}
    if(value.isObject()){QJsonObject result;const auto object=value.toObject();for(auto i=object.begin();i!=object.end();++i)
        if(!i.key().contains("GPS",Qt::CaseInsensitive))result.insert(i.key(),without_gps(i.value()));return result;}
    return value;
}
}
QJsonObject read_photo_metadata(const QString& path,const std::atomic_bool* cancelled) {
    const auto bytes=execute({"-j","-G1","-s","-n","-struct","-charset","filename=UTF8",
        "-EXIF:all","-IPTC:all","-XMP:all","-Canon:HDR-PQ","--XMP-crs:all",QFileInfo(path).absoluteFilePath()},cancelled);
    QJsonParseError error;const auto document=QJsonDocument::fromJson(bytes,&error);
    if(error.error!=QJsonParseError::NoError||!document.isArray()||document.array().size()!=1)
        throw EditError(QStringLiteral("元数据读取结果无效"));
    QJsonObject result;const auto data=document.array().first().toObject();
    for(auto i=data.begin();i!=data.end();++i)if(safe_tag(i.key()))result.insert(i.key(),i.value());
    if(data.contains("Canon:HDR-PQ"))result.insert("Camera:HDRPQ",data["Canon:HDR-PQ"]);
    return result;
}
QByteArray extract_embedded_jpeg(const QByteArray& raw) {
    QTemporaryDir directory;if(!directory.isValid())throw EditError(QStringLiteral("无法创建预览临时目录"));
    QFile file(directory.filePath("source.raw"));
    if(!file.open(QIODevice::WriteOnly)||file.write(raw)!=raw.size())throw EditError(file.errorString());file.close();
    for(const auto& tag:{"-PreviewImage","-JpgFromRaw","-ThumbnailImage"}){
        const auto data=execute({"-b",tag,file.fileName()},nullptr);
        if(data.startsWith(QByteArray::fromHex("ffd8")))return data;
    }
    return {};
}
void write_photo_metadata(const QString& staged,const QJsonObject& metadata,const MetadataPolicy& policy,
                          QSize size,bool srgb,const std::atomic_bool* cancelled) {
    if(metadata.isEmpty()||(!policy.exif&&!policy.iptc&&!policy.xmp&&!policy.copyright&&!policy.gps))return;
    QJsonObject selected;
    for(auto i=metadata.begin();i!=metadata.end();++i) {
        if(!safe_tag(i.key()))continue;
        const auto group=i.key().section(':',0,0);
        const bool gps=group=="GPS"||i.key().contains("GPS",Qt::CaseInsensitive);
        const bool keep=gps?policy.gps:rights(i.key())?policy.copyright:
            group=="IPTC"?policy.iptc:group.startsWith("XMP-")?policy.xmp:policy.exif;
        if(keep)selected.insert(i.key(),policy.gps?i.value():without_gps(i.value()));
    }
    if(selected.isEmpty())return;
    if(policy.exif){selected.insert("IFD0:Orientation",1);selected.insert("IFD0:Software","Huelith");
        selected.insert("ExifIFD:ExifImageWidth",size.width());selected.insert("ExifIFD:ExifImageHeight",size.height());
        selected.insert("ExifIFD:ColorSpace",srgb?1:65535);}
    selected.insert("SourceFile",QFileInfo(staged).absoluteFilePath());
    QTemporaryDir directory;if(!directory.isValid())throw EditError(QStringLiteral("无法创建元数据临时目录"));
    QFile file(directory.filePath("metadata.json"));
    const auto bytes=QJsonDocument(QJsonArray{selected}).toJson(QJsonDocument::Compact);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())throw EditError(file.errorString());file.close();
    execute({"-json="+file.fileName(),"-struct","-n","-overwrite_original","-charset","filename=UTF8",QFileInfo(staged).absoluteFilePath()},cancelled);
}
QString exif_caption(const QJsonObject& metadata) {
    const auto value=[&](const QString& name)->QJsonValue {
        for(auto i=metadata.begin();i!=metadata.end();++i)if(i.key().section(':',1)==name)return i.value();return {};
    };
    QStringList fields;const auto camera=value("Model").toVariant().toString(),lens=value("LensModel").toVariant().toString();
    if(!camera.isEmpty())fields<<camera;if(!lens.isEmpty())fields<<lens;
    const double focal=value("FocalLength").toDouble(),aperture=value("FNumber").toDouble(),shutter=value("ExposureTime").toDouble();
    if(focal>0)fields<<QString("%1 mm").arg(focal,0,'g',4);
    if(aperture>0)fields<<QString("f/%1").arg(aperture,0,'g',3);
    if(shutter>0)fields<<(shutter<1?QString("1/%1 s").arg(qRound(1/shutter)):QString("%1 s").arg(shutter,0,'g',4));
    if(value("ISO").toDouble()>0)fields<<QString("ISO %1").arg(value("ISO").toDouble());
    const auto copyright=value("Copyright").toString();if(!copyright.isEmpty())fields<<copyright;
    return fields.join(QStringLiteral("  ·  "));
}
QString exif_caption_template(const QString& pattern,const QJsonObject& metadata,const QString& name) {
    QString result=pattern;QJsonObject exposure=metadata;
    QString camera,lens,copyright;
    for(auto i=metadata.begin();i!=metadata.end();++i){const auto key=i.key().section(':',1);
        if(key=="Model"){camera=i.value().toString();exposure.remove(i.key());}
        if(key=="LensModel"){lens=i.value().toString();exposure.remove(i.key());}
        if(key=="Copyright"){copyright=i.value().toString();exposure.remove(i.key());}}
    result.replace("{name}",name);result.replace("{camera}",camera);result.replace("{lens}",lens);
    result.replace("{copyright}",copyright);result.replace("{exposure}",exif_caption(exposure));return result;
}
State exif_border(const State& source,int margin,int caption_height,int font_pixels,QColor background,QColor text,const QString& caption,const QImage& logo) {
    if(margin<0||margin>8000||caption_height<1||caption_height>8000||font_pixels<1||font_pixels>2048||caption.size()>4096||!background.isValid()||!text.isValid())
        throw EditError(QStringLiteral("EXIF 边框参数无效"));
    State result=source;result.name+=QStringLiteral(" · EXIF 边框");
    result.size+=QSize(2*margin,2*margin+caption_height);
    if(qint64(result.size.width())*result.size.height()>huelith::editing::maximum_image_pixels)throw EditError(QStringLiteral("边框结果超过 5.12 亿像素预算，请先缩小图片"));
    // A clipped group preserves the original canvas boundary and editable original layers.
    Layer picture;picture.kind=LayerKind::group;picture.name=source.name;picture.children=source.layers;
    picture.transform.translate(margin,margin);picture.vector_enabled=true;picture.vector_mask.addRect(QRectF(QPointF(),source.size));
    picture.frame=QRectF(QPointF(),source.size);
    Layer paper;paper.kind=LayerKind::fill;paper.name=QStringLiteral("边框颜色");paper.fill=background;
    Layer label;label.kind=LayerKind::text;label.name=QStringLiteral("EXIF · 可编辑文字");label.text.paragraph=true;
    label.text.box=QSizeF(source.size.width(),caption_height);
    label.text.html=QString("<p align='center' style='font-family:Microsoft YaHei;font-size:%1px;color:%2'>%3</p>")
        .arg(font_pixels).arg(text.name(),caption.toHtmlEscaped());
    label.transform.translate(margin,margin+source.size.height());
    result.layers={paper,picture,label};result.selection={};result.saved_selections.clear();result.snapshots.clear();result.paths.clear();
    if(!logo.isNull()){
        Layer mark;mark.kind=LayerKind::pixels;mark.name=QStringLiteral("自有标志");mark.pixels=to_working(logo);
        const double scale=std::min(source.size.width()*.2/logo.width(),caption_height*.8/logo.height());
        mark.transform=QTransform(scale,0,0,scale,margin,margin+source.size.height()+(caption_height-logo.height()*scale)/2);
        const double inset=logo.width()*scale+std::max(4,font_pixels);
        result.layers.back().text.box.setWidth(std::max(1.,source.size.width()-inset));
        result.layers.back().transform.translate(inset,0);result.layers.push_back(std::move(mark));
    }
    result.horizontal_guides.clear();result.vertical_guides.clear();validate_state(result);return result;
}
}
