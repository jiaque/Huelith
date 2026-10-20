// Python supplies independent reference cases; all rendering uses production code.
#include "huelith/editing/project.h"
#include <QJsonArray>
#include <QJsonObject>
#include <cstring>

using namespace huelith::editing;
QJsonObject composite_command(const QJsonObject& request) {
    if (request.contains("blendTerms")) {
        QJsonArray results;
        for (const auto& item : request.value("blendTerms").toArray()) {
            const auto input = item.toObject();
            const auto a = input.value("source").toArray(), b = input.value("backdrop").toArray();
            const auto value = artistic_term(
                {float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble())},
                {float(b[0].toDouble()), float(b[1].toDouble()), float(b[2].toDouble())},
                static_cast<Blend>(input.value("mode").toInt()));
            results.append(QJsonArray{value[0], value[1], value[2]});
        }
        return {{"values", results}};
    }
    State state;
    state.name = "Composite reference";
    state.size = QSize(request.value("width").toInt(8), request.value("height").toInt(8));
    if (state.size.width() < 1 || state.size.height() < 1 ||
        qint64(state.size.width()) * state.size.height() > 1000000)
        throw EditError("invalid composite fixture size");
    state.global_light_angle = request.value("globalAngle").toDouble(120);
    std::function<Layer(const QJsonObject&)> parse = [&](const QJsonObject& input) {
        Layer layer;
        layer.name = "reference";
        layer.kind = static_cast<LayerKind>(input.value("kind").toInt());
        layer.blend = static_cast<Blend>(input.value("mode").toInt());
        layer.opacity = float(input.value("opacity").toDouble(1));
        layer.visible = input.value("visible").toBool(true);
        layer.isolated = input.value("isolated").toBool();
        layer.clipped = input.value("clipped").toBool();
        layer.exposure = input.value("exposure").toDouble();
        layer.transform.translate(input.value("x").toDouble(), input.value("y").toDouble());
        if (input.contains("rgba")) {
            const auto color = input.value("rgba").toArray();
            QImage pixels(state.size, QImage::Format_RGBA32FPx4_Premultiplied);
            pixels.setColorSpace(QColorSpace::SRgbLinear);
            for (int y = 0; y < pixels.height(); ++y) {
                auto* row = reinterpret_cast<float*>(pixels.scanLine(y));
                for (int x = 0; x < pixels.width(); ++x)
                    for (int c = 0; c < 4; ++c)
                        row[x * 4 + c] = float(color[c].toDouble());
            }
            layer.pixels = to_working(pixels);
        }
        if (input.contains("mask")) {
            layer.mask = QImage(state.size, QImage::Format_Grayscale8);
            layer.mask.fill(input.value("mask").toInt());
        }
        if (input.contains("vector")) {
            const auto values = input.value("vector").toArray();
            layer.vector_enabled = true;
            layer.vector_mask.addRect(values[0].toDouble(), values[1].toDouble(),
                                      values[2].toDouble(), values[3].toDouble());
        }
        if (input.contains("blur")) {
            Effect effect;
            effect.blur_radius = input.value("blur").toDouble();
            layer.effects.push_back(effect);
        }
        if (input.contains("styles"))
            layer.styles = styles_from_json(input.value("styles").toObject());
        for (const auto& child : input.value("children").toArray())
            layer.children.push_back(parse(child.toObject()));
        return layer;
    };
    for (const auto& layer : request.value("layers").toArray())
        state.layers.push_back(parse(layer.toObject()));
    std::atomic_bool cancelled{request.value("cancelled").toBool()};
    RenderOptions options;
    options.cancelled = &cancelled;
    const auto original = render(state, options);
    bool roundtrip = true;
    if (request.contains("file")) {
        save_project({state, 1}, request.value("file").toString());
        state = load_project(request.value("file").toString());
        roundtrip = original == render(state);
    }
    if (request.contains("capture"))
        display_image(original).save(request.value("capture").toString());
    const auto output = convert_linear_rgb(original, QColorSpace::SRgbLinear);
    return {
        {"roundtrip", roundtrip},
        {"data", QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(output.constBits()),
                                                output.sizeInBytes())
                                         .toBase64())}};
}
