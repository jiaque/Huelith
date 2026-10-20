#include "huelith/editing/brush.h"
#include "huelith/editing/project.h"
#include <QBuffer>
#include <QImageReader>
#include <QJsonArray>
#include <QPainter>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
namespace huelith::editing {
namespace {
struct Number {
    const char* key;
    double BrushSettings::* field;
    double low, high;
};
constexpr std::array numbers{Number{"diameter", &BrushSettings::diameter, 1, 5000},
                             Number{"hardness", &BrushSettings::hardness, 0, 1},
                             Number{"opacity", &BrushSettings::opacity, 0, 1},
                             Number{"flow", &BrushSettings::flow, 0, 1},
                             Number{"spacing", &BrushSettings::spacing, .01, 10},
                             Number{"angle", &BrushSettings::angle, -180, 180},
                             Number{"roundness", &BrushSettings::roundness, .01, 1},
                             Number{"sizeJitter", &BrushSettings::size_jitter, 0, 1},
                             Number{"minimumSize", &BrushSettings::minimum_size, 0, 1},
                             Number{"angleJitter", &BrushSettings::angle_jitter, 0, 180},
                             Number{"roundnessJitter", &BrushSettings::roundness_jitter, 0, 1},
                             Number{"scatter", &BrushSettings::scatter, 0, 10},
                             Number{"countJitter", &BrushSettings::count_jitter, 0, 1},
                             Number{"flowJitter", &BrushSettings::flow_jitter, 0, 1},
                             Number{"textureDepth", &BrushSettings::texture_depth, 0, 1},
                             Number{"textureScale", &BrushSettings::texture_scale, .1, 20},
                             Number{"smoothing", &BrushSettings::smoothing, 0, 1}};
constexpr std::array flags{std::pair{"flipX", &BrushSettings::flip_x},
                           std::pair{"flipY", &BrushSettings::flip_y},
                           std::pair{"scatterBoth", &BrushSettings::scatter_both},
                           std::pair{"pressureSize", &BrushSettings::pressure_size},
                           std::pair{"pressureFlow", &BrushSettings::pressure_flow},
                           std::pair{"directionAngle", &BrushSettings::direction_angle},
                           std::pair{"pulledString", &BrushSettings::pulled_string},
                           std::pair{"catchUp", &BrushSettings::catch_up},
                           std::pair{"catchUpEnd", &BrushSettings::catch_up_end},
                           std::pair{"adjustZoom", &BrushSettings::adjust_zoom},
                           std::pair{"airbrush", &BrushSettings::airbrush}};
double hash(int x, int y) {
    std::uint32_t v = std::uint32_t(x) * 0x9e3779b9U ^ std::uint32_t(y) * 0x85ebca6bU;
    v ^= v >> 16;
    v *= 0x7feb352dU;
    v ^= v >> 15;
    v *= 0x846ca68bU;
    v ^= v >> 16;
    return double(v) / 4294967295.;
}
} // namespace
void validate_brush(const BrushSettings& s) {
    for (const auto& number : numbers) {
        const double value = s.*number.field;
        if (!std::isfinite(value) || value < number.low || value > number.high)
            throw EditError(QString("画笔参数超出范围：%1").arg(number.key));
    }
    if (s.tip < 0 || s.tip > 6 || s.count < 1 || s.count > 16 || s.texture < 0 || s.texture > 3)
        throw EditError("画笔类型或数量无效");
    if (s.tip == 6 &&
        (s.custom_tip.isNull() || s.custom_tip.width() > 1024 || s.custom_tip.height() > 1024))
        throw EditError("自定义笔尖必须为不超过1024像素的有效图像");
}
QJsonObject brush_json(const BrushSettings& s) {
    validate_brush(s);
    QJsonObject j{{"version", 1}, {"tip", s.tip}, {"count", s.count}, {"texture", s.texture}};
    for (const auto& number : numbers)
        j.insert(QLatin1String(number.key), s.*number.field);
    for (const auto& [key, field] : flags)
        j.insert(QLatin1String(key), s.*field);
    if (!s.custom_tip.isNull()) {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (!s.custom_tip.save(&buffer, "PNG"))
            throw EditError("无法保存自定义笔尖");
        j.insert("customTip", QString::fromLatin1(bytes.toBase64()));
    }
    return j;
}
BrushSettings brush_from_json(const QJsonObject& j) {
    BrushSettings s;
    if (j.isEmpty())
        return s;
    if (j.value("version").toInt() != 1)
        throw EditError("未知画笔预设版本");
    auto keys = brush_json(s);
    keys.insert("customTip", QString{});
    for (auto it = j.begin(); it != j.end(); ++it)
        if (!keys.contains(it.key()))
            throw EditError("未知画笔预设参数");
    for (const auto& number : numbers)
        if (j.contains(QLatin1String(number.key))) {
            if (!j.value(QLatin1String(number.key)).isDouble())
                throw EditError("画笔数值格式无效");
            s.*number.field = j.value(QLatin1String(number.key)).toDouble();
        }
    for (const auto& [key, field] : flags)
        if (j.contains(QLatin1String(key))) {
            if (!j.value(QLatin1String(key)).isBool())
                throw EditError("画笔开关格式无效");
            s.*field = j.value(QLatin1String(key)).toBool();
        }
    for (const auto& [key, field] :
         {std::pair{"tip", &BrushSettings::tip}, std::pair{"count", &BrushSettings::count},
          std::pair{"texture", &BrushSettings::texture}})
        if (j.contains(QLatin1String(key))) {
            const auto value = j.value(QLatin1String(key));
            if (!value.isDouble() || value.toDouble() != std::floor(value.toDouble()))
                throw EditError("画笔枚举格式无效");
            s.*field = value.toInt(-1);
        }
    if (j.contains("customTip")) {
        const auto encoded = j.value("customTip").toString().toLatin1();
        if (encoded.size() > 8 * 1024 * 1024)
            throw EditError("自定义笔尖数据过大");
        QByteArray bytes = QByteArray::fromBase64(encoded);
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "PNG");
        const auto dimensions = reader.size();
        if (!dimensions.isValid() || dimensions.width() > 1024 || dimensions.height() > 1024)
            throw EditError("自定义笔尖尺寸无效");
        s.custom_tip = reader.read();
        if (s.custom_tip.isNull())
            throw EditError("自定义笔尖数据无效");
        s.custom_tip = s.custom_tip.convertToFormat(QImage::Format_Grayscale8);
    }
    validate_brush(s);
    return s;
}
QImage brush_tip(const BrushSettings& s, int edge) {
    validate_brush(s);
    if (edge < 8 || edge > 1024)
        throw EditError("笔尖预览尺寸无效");
    QImage custom;
    if (s.tip == 6) {
        custom = QImage(edge, edge, QImage::Format_Grayscale8);
        custom.fill(0);
        const auto scaled =
            s.custom_tip.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                .convertToFormat(QImage::Format_Grayscale8);
        const int left = (edge - scaled.width()) / 2, top = (edge - scaled.height()) / 2;
        for (int y = 0; y < scaled.height(); ++y)
            std::copy_n(scaled.constScanLine(y), scaled.width(), custom.scanLine(top + y) + left);
    }
    QImage tip(edge, edge, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < edge; ++y) {
        auto* row = reinterpret_cast<QRgb*>(tip.scanLine(y));
        for (int x = 0; x < edge; ++x) {
            const double nx = (x + .5) * 2 / edge - 1, ny = (y + .5) * 2 / edge - 1,
                         r = std::hypot(nx, ny);
            double distance = s.tip == 1 ? std::max(std::abs(nx), std::abs(ny)) : r;
            if (s.tip == 4)
                distance = r / (.65 + .3 * std::cos(std::atan2(ny, nx) * 5));
            if (s.tip == 5)
                distance = std::abs(nx) + ny * ny;
            double alpha = distance >= 1 ? 0
                           : distance <= s.hardness
                               ? 1
                               : (1 - distance) / std::max(.001, 1 - s.hardness);
            if (s.tip == 2)
                alpha *= .25 + .75 * hash(x / 3, y / 3);
            if (s.tip == 3)
                alpha *= std::pow(std::abs(std::sin(nx * 70)), 3);
            if (s.tip == 6)
                alpha = custom.constScanLine(y)[x] / 255.;
            const int tx = int(std::floor(x / s.texture_scale)),
                      ty = int(std::floor(y / s.texture_scale));
            double texture = 1;
            if (s.texture == 1)
                texture = .3 + .7 * hash(tx, ty);
            if (s.texture == 2)
                texture = .5 + .5 * std::abs(std::sin(tx * .8) * std::sin(ty * .8));
            if (s.texture == 3)
                texture = hash(tx / 4, ty / 4) > .3 ? 1 : .05;
            alpha *= 1 - s.texture_depth * (1 - texture);
            const int a = std::clamp(qRound(alpha * 255), 0, 255);
            row[x] = qRgba(a, a, a, a);
        }
    }
    return tip;
}
void BrushStroke::reset(const BrushSettings& settings, std::uint32_t seed, double zoom) {
    validate_brush(settings);
    if (!std::isfinite(zoom) || zoom <= 0)
        throw EditError("画笔缩放无效");
    settings_ = settings;
    tip_ = brush_tip(settings_);
    random_ = seed ? seed : 1;
    started_ = false;
    stamps_ = 0;
    until_next_ = 0;
    smooth_radius_ = 50 * settings.smoothing / (settings.adjust_zoom ? zoom : 1);
    air_time_ = 0;
}
double BrushStroke::uniform() {
    random_ ^= random_ << 13;
    random_ ^= random_ >> 17;
    random_ ^= random_ << 5;
    return double(random_) / 4294967296.;
}
void BrushStroke::stamp(QImage& coverage, QPointF position, double pressure, double direction) {
    const double pressure_size =
        settings_.pressure_size ? std::max(settings_.minimum_size, pressure) : 1;
    const int count =
        std::max(1, qRound(settings_.count * (1 - settings_.count_jitter * uniform())));
    QPainter painter(&coverage);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    for (int i = 0; i < count; ++i) {
        const double diameter = std::max(
            1., settings_.diameter * pressure_size *
                    std::max(settings_.minimum_size, 1 - settings_.size_jitter * uniform()));
        const double angle = settings_.angle + (settings_.direction_angle ? direction : 0) +
                             (uniform() * 2 - 1) * settings_.angle_jitter;
        const double roundness =
            std::max(.01, settings_.roundness * (1 - settings_.roundness_jitter * uniform()));
        const double scatter = (uniform() * 2 - 1) * settings_.scatter * settings_.diameter;
        const double forward = settings_.scatter_both
                                   ? (uniform() * 2 - 1) * settings_.scatter * settings_.diameter
                                   : 0;
        const double radians = direction * std::numbers::pi / 180;
        const QPointF shift(forward * std::cos(radians) - scatter * std::sin(radians),
                            forward * std::sin(radians) + scatter * std::cos(radians));
        painter.save();
        painter.translate(position + shift);
        painter.rotate(angle);
        painter.scale(settings_.flip_x ? -1 : 1, settings_.flip_y ? -roundness : roundness);
        painter.setOpacity(settings_.flow * (1 - settings_.flow_jitter * uniform()) *
                           (settings_.pressure_flow ? pressure : 1));
        painter.drawImage(QRectF(-diameter / 2, -diameter / 2, diameter, diameter), tip_);
        painter.restore();
        ++stamps_;
    }
}
void BrushStroke::segment(QImage& coverage, QPointF position, double pressure) {
    if (coverage.format() != QImage::Format_ARGB32_Premultiplied || !std::isfinite(position.x()) ||
        !std::isfinite(position.y()) || !std::isfinite(pressure))
        throw EditError("笔画输入格式无效");
    pressure = std::clamp(pressure, 0., 1.);
    const double step = std::max(.25, settings_.diameter * settings_.spacing);
    if (!started_) {
        stamp(coverage, position, pressure, 0);
        started_ = true;
        previous_ = position;
        previous_pressure_ = pressure;
        until_next_ = step;
        return;
    }
    const auto delta = position - previous_;
    const double length = std::hypot(delta.x(), delta.y());
    if (length == 0)
        return;
    const double direction = std::atan2(delta.y(), delta.x()) * 180 / std::numbers::pi;
    double distance = until_next_;
    int work = 0;
    while (distance <= length + 1e-9) {
        if (++work > 200000)
            throw EditError("单次笔画距离过大");
        const double t = std::min(1., distance / length);
        stamp(coverage, previous_ + delta * t,
              previous_pressure_ + (pressure - previous_pressure_) * t, direction);
        distance += step;
    }
    until_next_ = distance - length;
    previous_ = position;
    previous_pressure_ = pressure;
}
void BrushStroke::add(QImage& coverage, QPointF position, double pressure) {
    if (!std::isfinite(position.x()) || !std::isfinite(position.y()) || !std::isfinite(pressure))
        throw EditError("笔画坐标无效");
    pressure = std::clamp(pressure, 0., 1.);
    if (!started_ || smooth_radius_ <= .001) {
        segment(coverage, position, pressure);
        filtered_ = position;
        input_ = position;
        input_pressure_ = pressure;
        return;
    }
    const QPointF delta = position - input_;
    const double length = std::hypot(delta.x(), delta.y());
    if (length > 200000)
        throw EditError("单次笔画距离过大");
    const int pieces = std::max(1, int(std::ceil(length)));
    const double blend = 1 - std::exp(-length / pieces / std::max(.001, smooth_radius_));
    for (int i = 1; i <= pieces; ++i) {
        const double t = double(i) / pieces;
        const auto point = input_ + delta * t;
        if (settings_.pulled_string) {
            const auto leash = point - filtered_;
            const double distance = std::hypot(leash.x(), leash.y());
            if (distance > smooth_radius_)
                filtered_ += leash * (1 - smooth_radius_ / distance);
        } else
            filtered_ += (point - filtered_) * blend;
        segment(coverage, filtered_, input_pressure_ + (pressure - input_pressure_) * t);
    }
    input_ = position;
    input_pressure_ = pressure;
}
void BrushStroke::idle(QImage& coverage, double seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 10)
        throw EditError("笔画时间无效");
    if (!started_)
        return;
    if (smooth_radius_ > 0 && settings_.catch_up && !settings_.pulled_string) {
        filtered_ += (input_ - filtered_) * (1 - std::exp(-seconds * 12));
        segment(coverage, filtered_, input_pressure_);
    }
    if (settings_.airbrush) {
        air_time_ += seconds;
        while (air_time_ >= .04 - 1e-9) {
            stamp(coverage, previous_, input_pressure_, 0);
            air_time_ -= .04;
        }
    }
}
void BrushStroke::finish(QImage& coverage) {
    if (started_ && smooth_radius_ > 0 && settings_.catch_up_end)
        segment(coverage, input_, input_pressure_);
}
std::vector<std::pair<QString, BrushSettings>> builtin_brushes() {
    std::vector<std::pair<QString, BrushSettings>> result;
    const auto add = [&](QString name, BrushSettings s) {
        result.emplace_back(std::move(name), std::move(s));
    };
    for (int size : {5, 15, 40, 100}) {
        BrushSettings s;
        s.diameter = size;
        s.hardness = 1;
        add(QString("硬圆 %1").arg(size), s);
        s.hardness = 0;
        add(QString("柔圆 %1").arg(size), s);
    }
    BrushSettings s;
    s.hardness = 1;
    s.roundness = .2;
    s.angle = -45;
    add("扁平书法", s);
    s = {};
    s.tip = 1;
    s.hardness = 1;
    add("方形硬笔", s);
    s.roundness = .25;
    s.angle = 30;
    add("斜角记号笔", s);
    s = {};
    s.tip = 2;
    s.texture = 1;
    s.hardness = .7;
    add("粉笔", s);
    s.diameter = 8;
    s.flow = .4;
    s.texture_depth = .8;
    add("铅笔纹理", s);
    s = {};
    s.tip = 3;
    s.roundness = .5;
    s.direction_angle = true;
    add("干刷纤维", s);
    s.flow = .3;
    s.texture = 2;
    s.texture_depth = .8;
    add("画布干刷", s);
    s = {};
    s.tip = 4;
    s.hardness = 1;
    s.spacing = 1.3;
    s.scatter = .3;
    s.angle_jitter = 180;
    add("星形散点", s);
    s.tip = 5;
    s.roundness = .5;
    s.size_jitter = .5;
    add("叶形散点", s);
    s = {};
    s.spacing = .3;
    s.scatter = 2;
    s.scatter_both = true;
    s.count = 5;
    s.size_jitter = .8;
    s.flow = .35;
    s.diameter = 15;
    add("喷溅颗粒", s);
    s = {};
    s.hardness = 0;
    s.flow = .08;
    s.pressure_flow = true;
    s.spacing = .03;
    s.diameter = 100;
    add("柔和喷绘", s);
    s = {};
    s.diameter = 3;
    s.hardness = 1;
    s.pressure_size = false;
    add("精细硬笔", s);
    return result;
}
} // namespace huelith::editing
