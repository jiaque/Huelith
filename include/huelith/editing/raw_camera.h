#pragma once
#include "huelith/editing/project.h"
#include <libraw/libraw.h>
#include <cmath>
namespace huelith::editing {
inline void normalize_raw_matrix(libraw_colordata_t& color) {
    double magnitude=0;
    for(int y=0;y<3;++y)for(int x=0;x<3;++x)magnitude+=std::abs(color.cam_xyz[y][x]);
    if(magnitude>0)return;
    for(int entry=0;entry<2;++entry){
        magnitude=0;
        for(int y=0;y<3;++y)for(int x=0;x<3;++x)magnitude+=std::abs(color.dng_color[entry].colormatrix[y][x]);
        if(magnitude>0){
            for(int y=0;y<3;++y)for(int x=0;x<3;++x)color.cam_xyz[y][x]=color.dng_color[entry].colormatrix[y][x];
            return;
        }
    }
}
// Some older CRW cameras do not record an as-shot neutral. Use LibRaw's
// calibrated daylight neutral rather than inventing unit gains or rejecting RAW.
inline bool normalize_raw_neutral(libraw_colordata_t& color) {
    bool as_shot=true;
    for(int i=0;i<3;++i)as_shot=as_shot && std::isfinite(color.cam_mul[i]) && color.cam_mul[i]>0;
    if(as_shot)return false;
    for(int i=0;i<3;++i)
        if(!std::isfinite(color.pre_mul[i])||color.pre_mul[i]<=0)
            throw EditError(QStringLiteral("RAW 缺少有效的拍摄白平衡和日光校准值"));
    for(int i=0;i<4;++i)color.cam_mul[i]=color.pre_mul[i]>0?color.pre_mul[i]:color.pre_mul[1];
    return true;
}
}
