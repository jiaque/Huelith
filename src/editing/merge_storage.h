#pragma once
#include "huelith/editing/project.h"
#include <QDir>
#include <QTemporaryFile>
#include <opencv2/core.hpp>
#include <memory>

namespace huelith::editing {
// OpenCV continues to operate on scanlines/ROIs; large backing arrays live on
// scratch disk rather than accumulating on the process heap during stitching.
class MergeAllocator final : public cv::MatAllocator {
    struct Mapping {
        QTemporaryFile file{QDir::tempPath()+"/Huelith-merge-XXXXXX"};
        uchar* bytes{};
        ~Mapping(){if(bytes)file.unmap(bytes);}
    };
public:
    cv::UMatData* allocate(int dims,const int* sizes,int type,void* data,std::size_t* step,
                          cv::AccessFlag flags,cv::UMatUsageFlags usage) const override {
        if(data)return cv::Mat::getStdAllocator()->allocate(dims,sizes,type,data,step,flags,usage);
        std::size_t total=CV_ELEM_SIZE(type);
        for(int i=dims-1;i>=0;--i) {if(step)step[i]=total;total*=std::size_t(sizes[i]);}
        if(data || total<64ULL*1024*1024)
            return cv::Mat::getStdAllocator()->allocate(dims,sizes,type,data,step,flags,usage);
        auto mapping=std::make_unique<Mapping>();
        if(!mapping->file.open() || !mapping->file.resize(qint64(total)) ||
           !(mapping->bytes=mapping->file.map(0,qint64(total))))
            throw EditError(QStringLiteral("合成临时磁盘空间不足或无法映射"));
        auto result=std::make_unique<cv::UMatData>(this);
        result->data=result->origdata=mapping->bytes;result->size=total;
        result->userdata=mapping.release();
        return result.release();
    }
    bool allocate(cv::UMatData* data,cv::AccessFlag flags,cv::UMatUsageFlags usage) const override {
        return cv::Mat::getStdAllocator()->allocate(data,flags,usage);
    }
    void deallocate(cv::UMatData* data) const override {
        if(!data)return;
        delete static_cast<Mapping*>(data->userdata);delete data;
    }
};
}
