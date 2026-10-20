#include "huelith/editing/healing.h"
#include "huelith/editing/project.h"
#include <QJsonObject>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace huelith::editing {
namespace {
using Rgb = std::array<double, 3>;
void check(const std::atomic_bool* cancelled) {
    if (cancelled && cancelled->load())
        throw EditError(QStringLiteral("修复已取消"));
}
void validate(const HealingStroke& s) {
    if (s.points.empty() || s.points.size() > 16384 || !std::isfinite(s.radius) || s.radius <= 0 ||
        s.radius > .5 || !std::isfinite(s.feather) || s.feather < 0 || s.feather > 1 ||
        !std::isfinite(s.opacity) || s.opacity < 0 || s.opacity > 1 ||
        !std::isfinite(s.offset.x()) || !std::isfinite(s.offset.y()) ||
        std::abs(s.offset.x()) > 1 || std::abs(s.offset.y()) > 1)
        throw EditError(QStringLiteral("修复笔画参数无效"));
    if(!s.source_offsets.empty() && s.source_offsets.size()!=s.points.size())throw EditError("修复来源路径数量无效");
    for(const auto p:s.source_offsets)if(!std::isfinite(p.x()) || !std::isfinite(p.y()) || std::abs(p.x())>1 || std::abs(p.y())>1)throw EditError("修复来源坐标无效");
    for (const auto p : s.points)
        if (!std::isfinite(p.x()) || !std::isfinite(p.y()) || p.x() < 0 || p.x() > 1 || p.y() < 0 ||
            p.y() > 1)
            throw EditError(QStringLiteral("修复笔画坐标无效"));
}
double number(const QJsonObject& object, const char* key, double fallback) {
    if (!object.contains(QLatin1String(key)))
        return fallback;
    if (!object.value(QLatin1String(key)).isDouble())
        throw EditError(QStringLiteral("修复数值格式无效"));
    return object.value(QLatin1String(key)).toDouble();
}
Rgb sample(const QImage& image, QPointF position) {
    const double x = std::clamp(position.x(), 0., double(image.width() - 1));
    const double y = std::clamp(position.y(), 0., double(image.height() - 1));
    const int ix = int(x), iy = int(y);
    Rgb result{};
    for (int yy = 0; yy < 2; ++yy) {
        const auto* row = reinterpret_cast<const float*>(
            image.constScanLine(std::min(iy + yy, image.height() - 1)));
        for (int xx = 0; xx < 2; ++xx) {
            const auto* pixel = row + std::min(ix + xx, image.width() - 1) * 4;
            const double weight = (xx ? x - ix : 1 - x + ix) * (yy ? y - iy : 1 - y + iy);
            for (int c = 0; c < 3; ++c)
                result[c] += pixel[3] > 0 ? weight * pixel[c] / pixel[3] : 0;
        }
    }
    return result;
}
std::vector<QPointF> points(const HealingStroke& s, QSize size) {
    std::vector<QPointF> result;
    for (auto p : s.points)
        result.emplace_back(p.x() * size.width(), p.y() * size.height());
    return result;
}
// Solve the smooth color correction separately from source texture. Coarse-to-fine
// initialization propagates boundary illumination across large strokes without
// making the result depend on a fixed pixel diffusion distance.
std::vector<Rgb> harmonic(int w, int h, const std::vector<float>& mask,
                          const std::vector<Rgb>& boundary, const std::atomic_bool* cancelled) {
    auto solution = boundary;
    if (std::max(w, h) > 32 && std::min(w, h) > 4) {
        const int cw = (w + 1) / 2, ch = (h + 1) / 2;
        std::vector<float> coarse_mask(std::size_t(cw) * ch);
        std::vector<Rgb> coarse_boundary(coarse_mask.size());
        for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x) {
                const auto i = std::size_t(y) * cw + x;
                // Keep the true outer Dirichlet boundary on every pyramid level. Sampling
                // w-2/h-2 on even dimensions can import damaged interior pixels as fixed edges.
                const auto n = std::size_t(y==ch-1 ? h-1 : std::min(y*2,h-1))*w +
                               (x==cw-1 ? w-1 : std::min(x*2,w-1));
                coarse_mask[i] = mask[n];
                coarse_boundary[i] = boundary[n];
            }
        const auto coarse = harmonic(cw, ch, coarse_mask, coarse_boundary, cancelled);
        for (int y = 1; y + 1 < h; ++y)
            for (int x = 1; x + 1 < w; ++x) {
                const auto i = std::size_t(y) * w + x;
                if (mask[i] == 0)
                    continue;
                solution[i] = {};
                for (int yy = 0; yy < 2; ++yy)
                    for (int xx = 0; xx < 2; ++xx) {
                        const double weight = (xx ? (x % 2) * .5 : 1 - (x % 2) * .5) *
                                              (yy ? (y % 2) * .5 : 1 - (y % 2) * .5);
                        const auto n = std::size_t(std::min(y / 2 + yy, ch - 1)) * cw +
                                       std::min(x / 2 + xx, cw - 1);
                        for (int c = 0; c < 3; ++c)
                            solution[i][c] += coarse[n][c] * weight;
                    }
            }
    } else {
        Rgb average{};
        int count{};
        for (std::size_t i = 0; i < mask.size(); ++i)
            if (mask[i] == 0) {
                for (int c = 0; c < 3; ++c)
                    average[c] += boundary[i][c];
                ++count;
            }
        for (auto& c : average)
            c /= std::max(1, count);
        for (std::size_t i = 0; i < mask.size(); ++i)
            if (mask[i] > 0)
                solution[i] = average;
    }
    const int iterations = std::max(w, h) > 32 ? 40 : 240;
    for (int iteration = 0; iteration < iterations; ++iteration) {
        check(cancelled);
        double maximum{};
        for (int parity = 0; parity < 2; ++parity)
            for (int y = 1; y + 1 < h; ++y)
                for (int x = 1; x + 1 < w; ++x) {
                    const auto i = std::size_t(y) * w + x;
                    if ((x + y) % 2 != parity || mask[i] == 0)
                        continue;
                    for (int c = 0; c < 3; ++c) {
                        const double next = (solution[i - 1][c] + solution[i + 1][c] +
                                             solution[i - w][c] + solution[i + w][c]) /
                                            4;
                        maximum = std::max(maximum, std::abs(next - solution[i][c]));
                        solution[i][c] = next;
                    }
                }
        if (maximum < 1e-7)
            break;
    }
    return solution;
}
} // namespace

namespace {
QRect patch_bounds(const QImage& source, const QImage& mask) {
    if (source.format() != QImage::Format_RGBA32FPx4_Premultiplied ||
        mask.format() != QImage::Format_Grayscale8 || source.size() != mask.size())
        throw EditError("修补图像或范围无效");
    int left = mask.width(), top = mask.height(), right = -1, bottom = -1;
    for (int y = 0; y < mask.height(); ++y) {
        const auto* row = mask.constScanLine(y);
        for (int x = 0; x < mask.width(); ++x)
            if (row[x]) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
    }
    return right < left ? QRect{} : QRect(QPoint(left, top), QPoint(right, bottom));
}
bool valid_sample(const QImage& source, QPointF p) {
    if (p.x() < 0 || p.y() < 0 || p.x() > source.width() - 1 || p.y() > source.height() - 1)
        return false;
    for (int yy = 0; yy < 2; ++yy)
        for (int xx = 0; xx < 2; ++xx) {
            const auto* row = reinterpret_cast<const float*>(
                source.constScanLine(std::min(int(p.y()) + yy, source.height() - 1)));
            if (row[std::min(int(p.x()) + xx, source.width() - 1) * 4 + 3] < .001F)
                return false;
        }
    return true;
}
} // namespace
QImage patch_replacement(const QImage& source, const QImage& coverage, QPointF offset,
                         double adaptation, const std::atomic_bool* cancelled) {
    const auto bounds = patch_bounds(source, coverage);
    if (!std::isfinite(offset.x()) || !std::isfinite(offset.y()) || !std::isfinite(adaptation) ||
        adaptation < 0 || adaptation > 1)
        throw EditError("修补偏移或颜色适应无效");
    if (bounds.isEmpty())
        return source;
    const auto roi = bounds.adjusted(-2, -2, 2, 2).intersected(source.rect());
    const double scale = std::min(1., 512. / std::max(roi.width(), roi.height()));
    const int w = std::max(1, qRound(roi.width() * scale)),
              h = std::max(1, qRound(roi.height() * scale));
    const auto reduced =
        coverage.copy(roi).scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    std::vector<float> mask(std::size_t(w) * h);
    std::vector<Rgb> boundary(mask.size());
    for (int y = 0; y < h; ++y) {
        check(cancelled);
        for (int x = 0; x < w; ++x) {
            const auto i = std::size_t(y) * w + x;
            const QPointF p(roi.x() + (x + .5) * roi.width() / w - .5,
                            roi.y() + (y + .5) * roi.height() / h - .5);
            const auto a = sample(source, p), b = sample(source, p + offset);
            mask[i] = reduced.constScanLine(y)[x] ? 1.F : 0.F;
            for (int c = 0; c < 3; ++c)
                boundary[i][c] = a[c] - b[c];
        }
    }
    const auto correction = harmonic(w, h, mask, boundary, cancelled);
    QImage output = source;
    for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
        check(cancelled);
        auto* row = reinterpret_cast<float*>(output.scanLine(y));
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            if (!coverage.constScanLine(y)[x])
                continue;
            const QPointF q = QPointF(x, y) + offset;
            if (!valid_sample(source, q))
                continue;
            const double gx = std::clamp((x - roi.x() + .5) * w / roi.width() - .5, 0.,
                                         double(w - 1)),
                         gy = std::clamp((y - roi.y() + .5) * h / roi.height() - .5, 0.,
                                         double(h - 1));
            const int ix = int(gx), iy = int(gy);
            Rgb delta{};
            for (int yy = 0; yy < 2; ++yy)
                for (int xx = 0; xx < 2; ++xx) {
                    const double weight =
                        (xx ? gx - ix : 1 - gx + ix) * (yy ? gy - iy : 1 - gy + iy);
                    const auto& value = correction[std::size_t(std::min(iy + yy, h - 1)) * w +
                                                   std::min(ix + xx, w - 1)];
                    for (int c = 0; c < 3; ++c)
                        delta[c] += value[c] * weight;
                }
            const auto value = sample(source, q);
            for (int c = 0; c < 3; ++c)
                row[x * 4 + c] = float((value[c] + delta[c] * adaptation) * row[x * 4 + 3]);
        }
    }
    return output;
}
QPointF automatic_patch_offset(const QImage& source, const QImage& coverage,
                               const std::atomic_bool* cancelled) {
    const auto bounds = patch_bounds(source, coverage);
    if (bounds.isEmpty())
        return {};
    std::vector<QPoint> probes;
    const int step = std::max(1, int(std::sqrt(double(bounds.width()) * bounds.height() / 512)));
    const auto area = bounds.adjusted(-1, -1, 1, 1).intersected(source.rect());
    for (int y = area.top(); y <= area.bottom(); y += step)
        for (int x = area.left(); x <= area.right(); x += step)
            if (!coverage.constScanLine(y)[x] || x == area.left() || y == area.top() ||
                x + step > area.right() || y + step > area.bottom())
                probes.emplace_back(x, y);
    double best = std::numeric_limits<double>::infinity();
    QPointF result;
    for (int ring = 1; ring <= 8; ++ring)
        for (int direction = 0; direction < 32; ++direction) {
            check(cancelled);
            const double angle = direction * 2 * std::numbers::pi / 32;
            const QPoint offset(qRound(std::cos(angle) * (bounds.width() + 4) * ring * .6),
                                qRound(std::sin(angle) * (bounds.height() + 4) * ring * .6));
            const auto candidate = bounds.translated(offset);
            if (!source.rect().contains(candidate))
                continue;
            bool overlap = false;
            for (int y = candidate.top(); y <= candidate.bottom() && !overlap; y += step)
                for (int x = candidate.left(); x <= candidate.right(); x += step)
                    if (coverage.constScanLine(y)[x]) {
                        overlap = true;
                        break;
                    }
            if (overlap)
                continue;
            Rgb mean{};
            double sum = 0;
            int count = 0;
            for (const auto p : probes) {
                const QPoint q = p + offset;
                if (!valid_sample(source, q))
                    continue;
                const auto a = sample(source, p), b = sample(source, q);
                for (int c = 0; c < 3; ++c) {
                    const double delta = a[c] - b[c];
                    mean[c] += delta;
                    sum += delta * delta;
                }
                ++count;
            }
            if (count < int(probes.size()) * 9 / 10 || count == 0)
                continue;
            double error = sum / count;
            for (double value : mean)
                error -= value * value / (double(count) * count);
            error += 1e-8 * QPointF::dotProduct(offset, offset);
            if (error < best) {
                best = error;
                result = offset;
            }
        }
    if (!std::isfinite(best))
        throw EditError("没有足够的未选中区域可供修复，请缩小范围或使用修复画笔指定来源");
    return result;
}

QJsonObject healing_json(const HealingStroke& s) {
    validate(s);
    QJsonArray path;
    for (auto p : s.points)
        path.append(QJsonArray{p.x(), p.y()});
    QJsonObject result{{"version", 1},       {"clone", s.clone},        {"enabled", s.enabled},
            {"points", path},     {"offsetX", s.offset.x()}, {"offsetY", s.offset.y()},
            {"radius", s.radius}, {"feather", s.feather},    {"opacity", s.opacity}};
    if(!s.source_offsets.empty()){QJsonArray offsets;for(auto p:s.source_offsets)offsets.append(QJsonArray{p.x(),p.y()});result["sourceOffsets"]=offsets;}return result;
}
HealingStroke healing_from_json(const QJsonObject& object) {
    if (number(object, "version", 0) != 1 || !object.value("points").isArray())
        throw EditError(QStringLiteral("未知修复配方版本或格式"));
    for (const auto* key : {"clone", "enabled"})
        if (object.contains(QLatin1String(key)) && !object.value(QLatin1String(key)).isBool())
            throw EditError(QStringLiteral("修复开关格式无效"));
    HealingStroke s;
    s.clone = object.value("clone").toBool();
    s.enabled = object.value("enabled").toBool(true);
    s.radius = number(object, "radius", .02);
    s.feather = number(object, "feather", .5);
    s.opacity = number(object, "opacity", 1);
    s.offset = {number(object, "offsetX", 0), number(object, "offsetY", 0)};
    for (const auto& value : object.value("points").toArray()) {
        const auto p = value.toArray();
        if (p.size() != 2 || !p[0].isDouble() || !p[1].isDouble())
            throw EditError(QStringLiteral("修复笔画格式无效"));
        s.points.emplace_back(p[0].toDouble(), p[1].toDouble());
    }
    if(object.contains("sourceOffsets")){if(!object["sourceOffsets"].isArray())throw EditError("修复来源路径无效");for(const auto value:object["sourceOffsets"].toArray()){const auto a=value.toArray();if(a.size()!=2 || !a[0].isDouble() || !a[1].isDouble())throw EditError("修复来源路径无效");s.source_offsets.emplace_back(a[0].toDouble(),a[1].toDouble());}}
    validate(s);
    const auto known = healing_json(s);
    for (auto i = object.begin(); i != object.end(); ++i)
        if (!known.contains(i.key()))
            throw EditError(QStringLiteral("未知修复参数"));
    return s;
}
QPointF automatic_healing_offset(const QImage& image, const HealingStroke& s, int alternative, const QImage& source_mask, const QPointF* preferred_offset, bool match_texture, bool nearby_only) {
    validate(s);
    if (image.isNull() || image.format() != QImage::Format_RGBA32FPx4_Premultiplied)
        throw EditError(QStringLiteral("自动取样需要 FP32 图像"));
    const auto path = points(s, image.size());
    const double radius = std::max(1., s.radius * std::min(image.width(), image.height()));
    QRectF bounds(path.front(), QSizeF{});
    for (auto p : path)
        bounds |= QRectF(p, QSizeF(.001, .001));
    bounds.adjust(-radius, -radius, radius, radius);
    // Compare actual brush footprints, not rectangular bounds: a diagonal
    // stroke can have a valid adjacent source inside its bounding rectangle.
    QPainterPath centerline;centerline.moveTo(path.front());
    for(std::size_t i=1;i<path.size();++i)centerline.lineTo(path[i]);
    QPainterPathStroker stroker;stroker.setWidth(radius*2);stroker.setCapStyle(Qt::RoundCap);stroker.setJoinStyle(Qt::RoundJoin);
    QPainterPath footprint=stroker.createStroke(centerline);
    if(path.size()==1)footprint.addEllipse(path.front(),radius,radius);
    std::vector<std::pair<double, QPointF>> candidates;
    std::vector<QPointF> offsets;
    if(preferred_offset){const QPointF previous(preferred_offset->x()*image.width(),preferred_offset->y()*image.height());offsets.push_back(previous);for(int d=0;d<8;++d){const double a=d*std::numbers::pi/4;offsets.push_back(previous+QPointF(std::cos(a),std::sin(a))*radius*.5);}}
    if(nearby_only){
        for(double reach:{2.2,2.6,3.,4.,5.,6.})for(int d=0;d<32;++d){
            const double a=d*std::numbers::pi/16;
            offsets.emplace_back(std::cos(a)*radius*reach,std::sin(a)*radius*reach);
        }
    }else for(int ring=1;ring<=4;++ring)for(int d=0;d<16;++d){const double a=d*std::numbers::pi/8,reach=(std::max(bounds.width(),bounds.height())+radius)*ring;offsets.emplace_back(std::cos(a)*reach,std::sin(a)*reach);}
    for(const auto offset:offsets){
            // Portrait repair must not jump to a different anatomical region merely
            // because its boundary color happens to match better.
            const double distance=QLineF(QPointF{},offset).length();
            if(nearby_only && distance>radius*6.+1e-6)continue;
            if(preferred_offset){const QPointF previous(preferred_offset->x()*image.width(),preferred_offset->y()*image.height());if(QLineF(previous,offset).length()>radius*.5+1e-6)continue;}
            const auto target = bounds.translated(offset);
            if (!QRectF(image.rect()).contains(target) || (nearby_only?footprint.intersects(footprint.translated(offset)):bounds.intersects(target)))
                continue;
            if(!source_mask.isNull()){
                bool valid=true;
                // Sample the entire source footprint, not only its border or center.
                const double step=std::max(1.,radius*.35);
                for(const auto point:path){
                    for(double dy=-radius;dy<=radius && valid;dy+=step)for(double dx=-radius;dx<=radius;dx+=step){
                        if(dx*dx+dy*dy>radius*radius)continue;
                        const auto q=point+offset+QPointF(dx,dy);
                        const int mx=std::clamp(int(q.x()*source_mask.width()/image.width()),0,source_mask.width()-1),my=std::clamp(int(q.y()*source_mask.height()/image.height()),0,source_mask.height()-1);
                        if(source_mask.constScanLine(my)[mx]<200){valid=false;break;}
                    }
                    for(int edge=0;edge<24 && valid;++edge){
                        const double angleEdge=edge*std::numbers::pi/12.;const auto q=point+offset+QPointF(std::cos(angleEdge),std::sin(angleEdge))*radius;
                        const int mx=std::clamp(int(q.x()*source_mask.width()/image.width()),0,source_mask.width()-1),my=std::clamp(int(q.y()*source_mask.height()/image.height()),0,source_mask.height()-1);
                        if(source_mask.constScanLine(my)[mx]<200)valid=false;
                    }
                    if(!valid)break;
                }
                if(!valid)continue;
            }
            double score{};
            int count{};
            for (std::size_t i = 0; i < path.size(); i += std::max(std::size_t(1), path.size() / 8))
                for (int a = 0; a < 12; ++a) {
                    const double angle_sample = a * std::numbers::pi / 6;
                    const QPointF p =
                        path[i] + QPointF(std::cos(angle_sample), std::sin(angle_sample)) * radius;
                    const auto before = sample(image, p), after = sample(image, p + offset);
                    for (int c = 0; c < 3; ++c)
                        score += std::pow(before[c] - after[c], 2);
                    if(match_texture){
                        for(const QPointF axis:{QPointF(1,0),QPointF(0,1)}){const auto t0=sample(image,p-axis),t1=sample(image,p+axis),s0=sample(image,p+offset-axis),s1=sample(image,p+offset+axis);for(int c=0;c<3;++c)score+=.75*std::pow((t1[c]-t0[c])-(s1[c]-s0[c]),2);}
                    }
                    ++count;
                }
            double cost=score/std::max(1,count);
            if(nearby_only)cost+=.002*distance/radius;
            if(preferred_offset){const QPointF previous(preferred_offset->x()*image.width(),preferred_offset->y()*image.height());cost+=.002*QPointF::dotProduct(offset-previous,offset-previous)/(radius*radius);}
            candidates.emplace_back(cost,offset);
        }
    if (candidates.empty())
        throw EditError(
            QStringLiteral("没有足够的邻近区域自动取样，请缩小笔刷或 Alt 单击指定来源"));
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    if(nearby_only && alternative==0){
        // Prefer the inner 1.1–1.5 diameter band when its match is comparable.
        // Expand only for a materially better boundary/texture match or when
        // the inner band contains no usable skin. Both bands share the hard cap.
        const double acceptable=candidates.front().first*1.25+.001;
        for(const auto& candidate:candidates)
            if(candidate.first<=acceptable && QLineF(QPointF{},candidate.second).length()<=radius*3.+1e-6)
                return {candidate.second.x()/image.width(),candidate.second.y()/image.height()};
    }
    const auto offset =
        candidates[std::size_t(std::max(0, alternative)) % candidates.size()].second;
    return {offset.x() / image.width(), offset.y() / image.height()};
}
HealingStroke automatic_healing_path(const QImage& image,const HealingStroke& stroke,const QImage& source_mask,bool nearby_only){
    validate(stroke);auto result=stroke;result.points.clear();result.source_offsets.clear();
    const double radius=std::max(1.,stroke.radius*std::min(image.width(),image.height())),spacing=std::max(1.,radius*.75);
    const auto path=points(stroke,image.size());std::vector<QPointF> uniform{path.front()};double next=spacing;
    for(std::size_t i=1;i<path.size();++i){const auto delta=path[i]-path[i-1];const double length=QLineF(path[i],path[i-1]).length();if(length<1e-9)continue;double along=next;for(;along<=length;along+=spacing)uniform.push_back(path[i-1]+delta*(along/length));next=along-length;}
    if(QLineF(uniform.back(),path.back()).length()>1e-6)uniform.push_back(path.back());
    QImage allowed(image.size().scaled(768,768,Qt::KeepAspectRatio),QImage::Format_ARGB32);allowed.fill(Qt::white);
    if(!source_mask.isNull())allowed=source_mask.scaled(allowed.size()).convertToFormat(QImage::Format_ARGB32);
    QPainter painter(&allowed);painter.setRenderHint(QPainter::Antialiasing);painter.scale(double(allowed.width())/image.width(),double(allowed.height())/image.height());painter.setPen(QPen(Qt::black,radius*2+2,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));QPainterPath excluded;excluded.moveTo(path.front());for(auto p:path)excluded.lineTo(p);if(path.size()==1)painter.drawPoint(path.front());else painter.drawPath(excluded);painter.end();allowed=allowed.convertToFormat(QImage::Format_Grayscale8);
    // Prefer a nearby source that works for the entire gesture. Selecting only
    // for the first dab can point along the wrinkle and strand later dabs.
    QPointF initial;bool hasInitial=false;
    try{if(nearby_only){initial=automatic_healing_offset(image,stroke,0,allowed,nullptr,true,true);hasInitial=true;}}catch(const EditError&){}
    for(const auto point:uniform){HealingStroke local=stroke;local.source_offsets.clear();local.points={{point.x()/image.width(),point.y()/image.height()}};const auto offset=automatic_healing_offset(image,local,0,allowed,result.source_offsets.empty()?(hasInitial?&initial:nullptr):&result.source_offsets.back(),nearby_only,nearby_only);result.points.push_back(local.points.front());result.source_offsets.push_back(offset);}
    // Confirm the swept source footprint as well as each anchor. A source path must not cross
    // a semantic hole (eye, hair or background) between otherwise valid samples.
    for(std::size_t i=1;i<result.points.size();++i){const auto a=result.points[i-1]+result.source_offsets[i-1],b=result.points[i]+result.source_offsets[i];const QPointF start(a.x()*image.width(),a.y()*image.height()),end(b.x()*image.width(),b.y()*image.height());const int steps=std::max(1,int(std::ceil(QLineF(start,end).length()/std::max(1.,radius*.2))));
        for(int j=1;j<steps;++j){const auto center=start+(end-start)*(double(j)/steps);for(int ring=0;ring<=2;++ring)for(int edge=0;edge<24;++edge){const double angle=edge*std::numbers::pi/12;const auto q=center+QPointF(std::cos(angle),std::sin(angle))*(radius*ring*.5);const int x=std::clamp(int(q.x()*allowed.width()/image.width()),0,allowed.width()-1),y=std::clamp(int(q.y()*allowed.height()/image.height()),0,allowed.height()-1);if(allowed.constScanLine(y)[x]<200)throw EditError("连续修复来源经过非皮肤区域，请缩小笔刷或 Alt 指定来源");}}
    }
    result.offset=result.source_offsets.front();validate(result);return result;
}
QImage apply_healing(const QImage& source, const QJsonArray& strokes,
                     const std::atomic_bool* cancelled, QImage* effective_coverage) {
    check(cancelled);
    // Optional FP32 effective opacity for compositing over earlier repair strokes.
    if (effective_coverage) {
        *effective_coverage = QImage(source.size(), QImage::Format_RGBA32FPx4);
        effective_coverage->fill(Qt::transparent);
    }
    if (strokes.size() > 256)
        throw EditError(QStringLiteral("修复笔画数量超过 256"));
    if (strokes.isEmpty())
        return source;
    if (source.isNull() || source.format() != QImage::Format_RGBA32FPx4_Premultiplied)
        throw EditError(QStringLiteral("修复需要 FP32 图像"));
    auto image = source;
    for (const auto& value : strokes) {
        check(cancelled);
        const auto s = healing_from_json(value.toObject());
        if (!s.enabled || s.opacity == 0)
            continue;
        const auto path = points(s, image.size());
        const double radius = std::max(.5, s.radius * std::min(image.width(), image.height()));
        QRectF bounds(path.front(), QSizeF(.001, .001));
        for (auto p : path)
            bounds |= QRectF(p, QSizeF(.001, .001));
        const auto roi = bounds.adjusted(-radius - 2, -radius - 2, radius + 2, radius + 2)
                             .toAlignedRect()
                             .intersected(image.rect());
        const int w = roi.width(), h = roi.height();
        if (roi.isEmpty())
            continue;
        const QImage input = image;
        const QPointF offset(s.offset.x() * image.width(), s.offset.y() * image.height());
        std::vector<float> mask(std::size_t(w) * h);
        std::vector<Rgb> target(mask.size()), sampled(mask.size()), solution(mask.size());
        std::vector<float> distances(mask.size(), float(radius + 1));
        std::vector<QPointF> source_offsets(s.source_offsets.empty()?0:mask.size(),offset);
        for (std::size_t segment = 0; segment < path.size(); ++segment) {
            check(cancelled);
            const auto a = path[segment], b = segment ? path[segment - 1] : a;
            const auto area = QRectF(a, b)
                                  .normalized()
                                  .adjusted(-radius, -radius, radius, radius)
                                  .toAlignedRect()
                                  .intersected(roi);
            const auto ab = b - a;
            const double length = QPointF::dotProduct(ab, ab);
            for (int y = area.top(); y <= area.bottom(); ++y)
                for (int x = area.left(); x <= area.right(); ++x) {
                    const QPointF p(x, y), ap = p - a;
                    const double t =
                        length > 0 ? std::clamp(QPointF::dotProduct(ap, ab) / length, 0., 1.) : 0;
                    auto& distance = distances[std::size_t(y - roi.y()) * w + x - roi.x()];
                    const float candidate=float(QLineF(p,a+ab*t).length());
                    if(candidate<=distance){distance=candidate;if(!s.source_offsets.empty()){const auto q=s.source_offsets[segment]*(1-t)+s.source_offsets[segment?segment-1:segment]*t;source_offsets[std::size_t(y-roi.y())*w+x-roi.x()]={q.x()*image.width(),q.y()*image.height()};}}
                }
        }
        for (int y = 0; y < h; ++y) {
            check(cancelled);
            for (int x = 0; x < w; ++x) {
                const QPointF p(roi.x() + x, roi.y() + y);
                const auto index = std::size_t(y) * w + x;
                const double distance = distances[index];
                const auto q = p + (source_offsets.empty()?offset:source_offsets[index]);
                const double coverage =
                    s.feather > 0 ? std::clamp((radius - distance) / (radius * s.feather), 0., 1.)
                                  : double(distance <= radius);
                mask[index] = QRectF(image.rect()).contains(q)
                                  ? float(coverage * coverage * (3 - 2 * coverage))
                                  : 0;
                target[index] = sample(input, p);
                sampled[index] = sample(input, q);
                solution[index] = sampled[index];
            }
        }
        if (!s.clone) {
            std::vector<Rgb> boundary(mask.size());
            for (std::size_t i = 0; i < mask.size(); ++i)
                for (int c = 0; c < 3; ++c)
                    boundary[i][c] = target[i][c] - sampled[i][c];
            const auto correction = harmonic(w, h, mask, boundary, cancelled);
            for (std::size_t i = 0; i < mask.size(); ++i)
                for (int c = 0; c < 3; ++c)
                    solution[i][c] = sampled[i][c] + correction[i][c];
        }
        for (int y = 0; y < h; ++y) {
            check(cancelled);
            auto* row = reinterpret_cast<float*>(image.scanLine(roi.y() + y));
            for (int x = 0; x < w; ++x) {
                const auto i = std::size_t(y) * w + x;
                auto* pixel = row + (roi.x() + x) * 4;
                if (effective_coverage) {
                    auto* opacity = reinterpret_cast<float*>(effective_coverage->scanLine(roi.y()+y))+(roi.x()+x)*4;
                    opacity[0] += (1-opacity[0])*mask[i]*s.opacity;
                }
                for (int c = 0; c < 3; ++c)
                    pixel[c] = float(std::lerp(double(pixel[c]), solution[i][c] * pixel[3],
                                               mask[i] * s.opacity));
            }
        }
    }
    return image;
}
} // namespace huelith::editing
