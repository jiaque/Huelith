#include "huelith/editing/liquify.h"
#include "huelith/editing/project.h"
#include "huelith/editing/image_storage.h"
#include <QtEndian>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>

namespace huelith::editing {
namespace {
std::array<float, 4> sample(const QImage& image, double x, double y) {
    x = std::clamp(x, 0., double(image.width() - 1));
    y = std::clamp(y, 0., double(image.height() - 1));
    const int ix = int(x), iy = int(y), jx = std::min(ix + 1, image.width() - 1),
              jy = std::min(iy + 1, image.height() - 1);
    const double fx = x - ix, fy = y - iy;
    const auto* a = reinterpret_cast<const float*>(image.constScanLine(iy));
    const auto* b = reinterpret_cast<const float*>(image.constScanLine(jy));
    std::array<float, 4> result{};
    for (int c = 0; c < 4; ++c)
        result[c] = float((a[ix * 4 + c] * (1 - fx) + a[jx * 4 + c] * fx) * (1 - fy) +
                          (b[ix * 4 + c] * (1 - fx) + b[jx * 4 + c] * fx) * fy);
    return result;
}
} // namespace
QImage create_liquify_mesh(QSize size) {
    if (size.width() < 1 || size.height() < 1)
        throw EditError(QStringLiteral("液化需要有效图像"));
    const double scale = std::min(1., 512. / std::max(size.width(), size.height()));
    QImage mesh(std::max(2, int(std::ceil(size.width() * scale)) + 1),
                std::max(2, int(std::ceil(size.height() * scale)) + 1),
                QImage::Format_RGBA32FPx4_Premultiplied);
    for (int y = 0; y < mesh.height(); ++y) {
        auto* p = reinterpret_cast<float*>(mesh.scanLine(y));
        for (int x = 0; x < mesh.width(); ++x) {
            p[4 * x] = p[4 * x + 1] = p[4 * x + 2] = 0;
            p[4 * x + 3] = 1;
        }
    }
    return mesh;
}
double liquify_minimum_jacobian(const QImage& mesh) {
    if(mesh.isNull())return 1.;
    validate_liquify_mesh(mesh);
    const double dx=1./(mesh.width()-1),dy=1./(mesh.height()-1);
    double minimum=1.;
    const auto cross=[](QPointF a,QPointF b){return a.x()*b.y()-a.y()*b.x();};
    for(int y=0;y<mesh.height()-1;++y){const auto* a=reinterpret_cast<const float*>(mesh.constScanLine(y));const auto* b=reinterpret_cast<const float*>(mesh.constScanLine(y+1));
        for(int x=0;x<mesh.width()-1;++x){
            const QPointF tl(a[x*4],a[x*4+1]),tr(dx+a[(x+1)*4],a[(x+1)*4+1]),bl(b[x*4],dy+b[x*4+1]),br(dx+b[(x+1)*4],dy+b[(x+1)*4+1]);
            minimum=std::min({minimum,cross(tr-tl,bl-tl)/(dx*dy),cross(tr-tl,br-tr)/(dx*dy),cross(br-bl,bl-tl)/(dx*dy),cross(br-bl,br-tr)/(dx*dy)});
        }
    }
    return minimum;
}
void validate_liquify_mesh(const QImage& mesh) {
    if (mesh.isNull())
        return;
    if (mesh.format() != QImage::Format_RGBA32FPx4_Premultiplied || mesh.width() < 2 ||
        mesh.height() < 2 || mesh.width() > 513 || mesh.height() > 513)
        throw EditError(QStringLiteral("液化网格格式或尺寸无效"));
    for (int y = 0; y < mesh.height(); ++y) {
        const auto* p = reinterpret_cast<const float*>(mesh.constScanLine(y));
        for (int x = 0; x < mesh.width(); ++x, p += 4)
            if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || std::abs(p[0]) > 4 ||
                std::abs(p[1]) > 4 || !std::isfinite(p[2]) || p[2] < 0 || p[2] > 1 || p[3] != 1)
                throw EditError(QStringLiteral("液化网格含无效数值"));
    }
}
void liquify_dab(QImage& mesh, QSize size, QPointF from, QPointF to, const LiquifyBrush& brush,
                 double amount) {
    validate_liquify_mesh(mesh);
    if (mesh.isNull() || size.width() < 1 || size.height() < 1 || !std::isfinite(brush.diameter) ||
        brush.diameter < 1 || brush.diameter > 15000 || !std::isfinite(brush.density) ||
        brush.density < 0 || brush.density > 1 || !std::isfinite(brush.pressure) ||
        brush.pressure < 0 || brush.pressure > 1 || !std::isfinite(amount) || amount < 0 ||
        amount > 10 || !std::isfinite(from.x()) || !std::isfinite(from.y()) ||
        !std::isfinite(to.x()) || !std::isfinite(to.y()) || int(brush.tool) < 0 ||
        int(brush.tool) > 8)
        throw EditError(QStringLiteral("液化画笔参数无效"));
    const QImage old = mesh;
    const double w = std::max(1, size.width() - 1), h = std::max(1, size.height() - 1),
                 radius = brush.diameter * .5;
    const auto delta = to - from;
    for (int y = 0; y < mesh.height(); ++y) {
        auto* row = reinterpret_cast<float*>(mesh.scanLine(y));
        for (int x = 0; x < mesh.width(); ++x) {
            const QPointF point(x * w / (mesh.width() - 1), y * h / (mesh.height() - 1));
            const auto relative = point - to;
            const double distance = std::hypot(relative.x(), relative.y()) / radius;
            if (distance >= 1)
                continue;
            const double edge =
                std::clamp((1 - distance) / std::max(.25, 1 - brush.density), 0., 1.);
            const double strength =
                std::clamp(edge * edge * (3 - 2 * edge) * brush.pressure * amount, 0., 1.);
            auto* p = row + 4 * x;
            if (brush.tool == LiquifyTool::freeze) {
                p[2] += float(strength * (1 - p[2]));
                continue;
            }
            if (brush.tool == LiquifyTool::thaw) {
                p[2] *= float(1 - strength);
                continue;
            }
            const double effective = strength * (1 - p[2]);
            if (effective == 0)
                continue;
            if (brush.tool == LiquifyTool::reconstruct) {
                p[0] *= float(1 - effective);
                p[1] *= float(1 - effective);
                continue;
            }
            if (brush.tool == LiquifyTool::smooth) {
                for (int c = 0; c < 2; ++c) {
                    double value{};
                    for (int yy = -1; yy <= 1; ++yy)
                        for (int xx = -1; xx <= 1; ++xx)
                            value += sample(old, x + xx, y + yy)[c] / 9.;
                    p[c] += float((value - p[c]) * effective);
                }
                continue;
            }
            QPointF offset;
            switch (brush.tool) {
            case LiquifyTool::warp:
                offset = -delta * effective;
                break;
            case LiquifyTool::push_left:
                offset = QPointF(delta.y(), -delta.x()) * (effective * (brush.reverse ? -1 : 1));
                break;
            case LiquifyTool::pucker: {
                // Follow a radial flow instead of negating the bloat offset.
                // Keep a resolved soft boundary even at maximum hardness.
                const double transition=std::max(.15,1-brush.density);
                const auto velocity=[&](double r){const double edge=std::clamp((1-r/radius)/transition,0.,1.);return r*edge*edge*(3-2*edge);};
                const double dt=.12*brush.pressure*amount*(1-p[2])*(brush.reverse?-1:1)/8;
                double r=distance*radius;const double initial=r;
                for(int step=0;step<8;++step){const double a=velocity(r),b=velocity(r+dt*a*.5),c=velocity(r+dt*b*.5),d=velocity(r+dt*c);r+=dt*(a+2*b+2*c+d)/6;}
                if(initial>1e-12)offset=relative*(r/initial-1);
                break;
            }
            case LiquifyTool::bloat:
                offset = relative * (-.12 * effective * (brush.reverse ? -1 : 1));
                break;
            case LiquifyTool::twirl: {
                const double falloff=std::clamp((1-distance)/std::max(.15,1-brush.density),0.,1.);
                const double angularStrength=falloff*falloff*(3-2*falloff)*brush.pressure*amount*(1-p[2]);
                const double angle = -.15 * angularStrength * (brush.reverse ? -1 : 1);
                offset = QPointF(relative.x() * std::cos(angle) - relative.y() * std::sin(angle),
                                 relative.x() * std::sin(angle) + relative.y() * std::cos(angle)) -
                         relative;
                break;
            }
            default:
                break;
            }
            const auto mapped = point + offset;
            const auto displacement = sample(old, mapped.x() / w * (mesh.width() - 1),
                                             mapped.y() / h * (mesh.height() - 1));
            p[0] = float(std::clamp(displacement[0] + offset.x() / w, -4., 4.));
            p[1] = float(std::clamp(displacement[1] + offset.y() / h, -4., 4.));
        }
    }
    if(brush.tool==LiquifyTool::pucker || brush.tool==LiquifyTool::twirl){
        // Bound inverse stretch to four (forward scale at least 1/4), and
        // retain positive orientation at every corner of each bilinear cell.
        // Existing edits above that limit are preserved but cannot worsen.
        const auto admissible=[&](){
            const double sx=(mesh.width()-1)/w,sy=(mesh.height()-1)/h;
            for(int y=0;y<mesh.height()-1;++y)for(int x=0;x<mesh.width()-1;++x){
                const auto metrics=[&](const QImage& image,int cx,int cy){
                    const auto* top=reinterpret_cast<const float*>(image.constScanLine(y));const auto* bottom=reinterpret_cast<const float*>(image.constScanLine(y+1));const auto* horizontal=cy?bottom:top;
                    const double a=1+(horizontal[(x+1)*4]-horizontal[x*4])*w*sx,b=(bottom[(x+cx)*4]-top[(x+cx)*4])*w*sy;
                    const double c=(horizontal[(x+1)*4+1]-horizontal[x*4+1])*h*sx,d=1+(bottom[(x+cx)*4+1]-top[(x+cx)*4+1])*h*sy;
                    const double det=a*d-b*c,norm=a*a+b*b+c*c+d*d;
                    return std::array<double,2>{det,std::sqrt((norm+std::sqrt(std::max(0.,norm*norm-4*det*det)))*.5)};
                };
                for(int cy=0;cy<2;++cy)for(int cx=0;cx<2;++cx){const auto now=metrics(mesh,cx,cy),before=metrics(old,cx,cy);if(now[0]<std::min(.05,before[0])-1e-6 || now[1]>std::max(4.,before[1])+1e-6)return false;}
            }return true;
        };
        const QImage candidate=mesh;double gain=1;
        for(int attempt=0;!admissible();++attempt){
            if(attempt==12){mesh=old;break;}
            gain*=.5;
            for(int y=0;y<mesh.height();++y){auto* row=reinterpret_cast<float*>(mesh.scanLine(y));const auto* a=reinterpret_cast<const float*>(old.constScanLine(y));const auto* b=reinterpret_cast<const float*>(candidate.constScanLine(y));for(int x=0;x<mesh.width();++x)for(int c=0;c<2;++c)row[x*4+c]=float(a[x*4+c]+(b[x*4+c]-a[x*4+c])*gain);}
        }
    }
}
void reconstruct_liquify(QImage& mesh, double amount) {
    validate_liquify_mesh(mesh);
    if (!std::isfinite(amount) || amount < 0 || amount > 1)
        throw EditError(QStringLiteral("重建强度无效"));
    for (int y = 0; y < mesh.height(); ++y) {
        auto* p = reinterpret_cast<float*>(mesh.scanLine(y));
        for (int x = 0; x < mesh.width(); ++x, p += 4) {
            p[0] *= float(1 - amount);
            p[1] *= float(1 - amount);
        }
    }
}
void freeze_liquify(QImage& mesh, int operation) {
    validate_liquify_mesh(mesh);
    if (operation < 0 || operation > 2)
        throw EditError(QStringLiteral("冻结操作无效"));
    for (int y = 0; y < mesh.height(); ++y) {
        auto* p = reinterpret_cast<float*>(mesh.scanLine(y));
        for (int x = 0; x < mesh.width(); ++x, p += 4)
            p[2] = operation == 2 ? 1 - p[2] : float(operation);
    }
}
QJsonObject liquify_json(const QImage& mesh) {
    validate_liquify_mesh(mesh);
    if (mesh.isNull())
        return {};
    QByteArray data(mesh.width() * mesh.height() * 16, Qt::Uninitialized);
    for (int y = 0; y < mesh.height(); ++y) {
        const auto* row = reinterpret_cast<const float*>(mesh.constScanLine(y));
        for (int i = 0; i < mesh.width() * 4; ++i)
            qToLittleEndian(std::bit_cast<quint32>(row[i]),
                            data.data() + (y * mesh.width() * 4 + i) * 4);
    }
    return {{"version", 1},
            {"width", mesh.width()},
            {"height", mesh.height()},
            {"data", QString::fromLatin1(data.toBase64())}};
}
QImage liquify_from_json(const QJsonObject& json) {
    if (json.isEmpty())
        return {};
    const int w = json.value("width").toInt(), h = json.value("height").toInt();
    if (json.value("version").toInt() != 1 || w < 2 || h < 2 || w > 513 || h > 513 ||
        json.value("data").toString().size() > 6 * 1024 * 1024)
        throw EditError(QStringLiteral("液化预设无效"));
    const auto bytes = QByteArray::fromBase64(json.value("data").toString().toLatin1(),
                                              QByteArray::AbortOnBase64DecodingErrors);
    if (bytes.size() != w * h * 16)
        throw EditError(QStringLiteral("液化预设数据长度无效"));
    QImage mesh(w, h, QImage::Format_RGBA32FPx4_Premultiplied);
    for (int y = 0; y < h; ++y) {
        auto* row = reinterpret_cast<float*>(mesh.scanLine(y));
        for (int i = 0; i < w * 4; ++i)
            row[i] = std::bit_cast<float>(
                qFromLittleEndian<quint32>(bytes.constData() + (y * w * 4 + i) * 4));
    }
    validate_liquify_mesh(mesh);
    return mesh;
}
QImage apply_liquify(const QImage& source, const QImage& mesh, const std::atomic_bool* cancelled) {
    validate_liquify_mesh(mesh);
    if (mesh.isNull())
        return source;
    if (source.isNull() || source.format() != QImage::Format_RGBA32FPx4_Premultiplied)
        throw EditError(QStringLiteral("液化需要FP32图像"));
    auto output=allocate_image(source.size(), source.format());
    output.setColorSpace(source.colorSpace());
    const double w = std::max(1, source.width() - 1), h = std::max(1, source.height() - 1);
    for (int y = 0; y < source.height(); ++y) {
        if (cancelled && cancelled->load())
            throw EditError(QStringLiteral("液化已取消"));
        auto* row = reinterpret_cast<float*>(output.scanLine(y));
        for (int x = 0; x < source.width(); ++x) {
            const auto d = sample(mesh, x / w * (mesh.width() - 1), y / h * (mesh.height() - 1));
            const auto p = sample(source, x + d[0] * w, y + d[1] * h);
            std::copy(p.begin(), p.end(), row + 4 * x);
        }
    }
    return output;
}
} // namespace huelith::editing
