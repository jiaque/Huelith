#include "project_archive.h"
#include "huelith/editing/project.h"
#include <QDataStream>
#include <QJsonDocument>
#include <array>

namespace huelith::editing {
namespace {
quint32 update_crc(quint32 crc,const QByteArray& bytes) {
    static const auto table=[] {
        std::array<quint32,256> values{};
        for(quint32 i=0;i<256;++i) {auto c=i;for(int b=0;b<8;++b)c=(c>>1)^(0xedb88320U&(0U-(c&1U)));values[i]=c;}
        return values;
    }();
    for(const auto c:bytes) crc=table[(crc^static_cast<uchar>(c))&255]^(crc>>8);
    return crc;
}
void check(bool value) { if(!value)throw EditError(QStringLiteral("工程容器读写失败或校验不匹配")); }
struct Entry {QByteArray name;quint64 offset{},length{};quint32 crc{};};
void local(QDataStream& out,const Entry& e) {
    out<<quint32(0x04034b50)<<quint16(45)<<quint16(0)<<quint16(0)<<quint16(0)<<quint16(0)
       <<e.crc<<quint32(0xffffffff)<<quint32(0xffffffff)<<quint16(e.name.size())<<quint16(20);
    out.writeRawData(e.name.data(),int(e.name.size()));
    out<<quint16(1)<<quint16(16)<<e.length<<e.length;
}
}
void write_project_archive(QIODevice& output,QTemporaryFile& payload,const QJsonObject& manifest) {
    check(payload.flush() && payload.seek(0));
    Entry data{"document.bin",0,quint64(payload.size()),0xffffffffU};
    while(!payload.atEnd()) {const auto part=payload.read(8*1024*1024);check(!part.isEmpty());data.crc=update_crc(data.crc,part);}
    data.crc=~data.crc;
    QDataStream out(&output);out.setByteOrder(QDataStream::LittleEndian);
    local(out,data);check(payload.seek(0));
    while(!payload.atEnd()) {const auto part=payload.read(8*1024*1024);check(!part.isEmpty() && output.write(part)==part.size());}
    const auto json=QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    Entry meta{"manifest.json",quint64(output.pos()),quint64(json.size()),~update_crc(0xffffffffU,json)};
    local(out,meta);check(output.write(json)==json.size());
    const quint64 start=quint64(output.pos());
    for(const auto& e:{data,meta}) {
        out<<quint32(0x02014b50)<<quint16(45)<<quint16(45)<<quint16(0)<<quint16(0)<<quint16(0)<<quint16(0)
           <<e.crc<<quint32(0xffffffff)<<quint32(0xffffffff)<<quint16(e.name.size())<<quint16(28)
           <<quint16(0)<<quint16(0)<<quint16(0)<<quint32(0)<<quint32(0xffffffff);
        out.writeRawData(e.name.data(),int(e.name.size()));
        out<<quint16(1)<<quint16(24)<<e.length<<e.length<<e.offset;
    }
    const quint64 end=quint64(output.pos());
    out<<quint32(0x06064b50)<<quint64(44)<<quint16(45)<<quint16(45)<<quint32(0)<<quint32(0)
       <<quint64(2)<<quint64(2)<<(end-start)<<start;
    out<<quint32(0x07064b50)<<quint32(0)<<end<<quint32(1);
    out<<quint32(0x06054b50)<<quint16(0)<<quint16(0)<<quint16(0xffff)<<quint16(0xffff)
       <<quint32(0xffffffff)<<quint32(0xffffffff)<<quint16(0);
    check(out.status()==QDataStream::Ok);
}
QJsonObject read_project_archive(QFile& input,QTemporaryFile& payload) {
    check(input.size()>=98 && input.seek(input.size()-22));
    QDataStream tail(&input);tail.setByteOrder(QDataStream::LittleEndian);quint32 signature{};tail>>signature;
    check(signature==0x06054b50 && input.seek(0));
    QDataStream in(&input);in.setByteOrder(QDataStream::LittleEndian);
    QJsonObject manifest;bool document=false;
    for(int entry=0;entry<2;++entry) {
        quint16 version{},flags{},method{},time{},date{},names{},extras{};
        quint32 crc{},packed{},size{};
        in>>signature>>version>>flags>>method>>time>>date>>crc>>packed>>size>>names>>extras;
        check(signature==0x04034b50 && flags==0 && method==0 && packed==size && names<=100 && extras<=1024);
        QByteArray name(names,Qt::Uninitialized),extra(extras,Qt::Uninitialized);
        check(in.readRawData(name.data(),names)==names && in.readRawData(extra.data(),extras)==extras);
        quint64 length=size;
        if(size==0xffffffffU) {
            QDataStream ex(extra);ex.setByteOrder(QDataStream::LittleEndian);quint16 tag{},bytes{};quint64 compressed{};
            ex>>tag>>bytes>>length>>compressed;
            check(tag==1 && bytes>=16 && length==compressed && ex.status()==QDataStream::Ok);
        }
        check(length<=quint64(input.size()-input.pos()) && length<=1024ULL*1024*1024*1024);
        const bool is_document=name=="document.bin";
        check((is_document && !document) || (name=="manifest.json" && manifest.isEmpty() && length<=65536));
        QByteArray json;quint32 actual=0xffffffffU;
        for(quint64 left=length;left;) {
            const auto part=input.read(qint64(std::min(left,quint64(8*1024*1024))));check(!part.isEmpty());
            actual=update_crc(actual,part);left-=quint64(part.size());
            if(is_document) check(payload.write(part)==part.size());else json+=part;
        }
        check(~actual==crc);
        if(is_document)document=true;else {manifest=QJsonDocument::fromJson(json).object();check(!manifest.isEmpty());}
    }
    in>>signature;check(signature==0x02014b50 && document && !manifest.isEmpty() && payload.flush() && payload.seek(0));
    return manifest;
}
}
