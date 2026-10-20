#include "huelith/editing/raw_recipe.h"
#include "huelith/editing/project.h"
#include "huelith/editing/raw_adjustments.h"
#include "huelith/editing/raw_profile.h"
#include "huelith/editing/white_balance.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
namespace huelith::editing {
QStringList raw_group_names() {
    QStringList result;
    for (const auto* label :
         {"光线", "颜色", "效果", "曲线与色阶", "色彩混合器", "颜色分级", "锐化与手动降噪",
          "AI 降噪与模型", "局部蒙版与调整", "裁剪", "校正", "光学", "几何", "修复与仿制"})
        result.append(QCoreApplication::translate("RawDialog", label));
    return result;
}
QJsonObject transfer_raw_settings(const QJsonObject& source, QJsonObject target_recipe,
                                  const RawGroups& groups) {
    const std::array<QStringList, 8> fields{
        QStringList{"exposure", "contrast", "highlights", "shadows", "whites", "blacks"},
        QStringList{"temperature", "tint", "saturation", "vibrance", "monochrome"},
        QStringList{"texture", "clarity", "dehaze", "vignette", "grain", "vignetteStyle",
                    "vignetteMidpoint", "vignetteRoundness", "vignetteFeather",
                    "vignetteHighlights", "grainSize", "grainRoughness", "legacyFilmEffects"},
        QStringList{"curve", "channelCurves", "parametricCurve", "curveSplits",
                    "legacyCurveInterpolation", "blackPoint", "whitePoint", "gamma"},
        QStringList{"hue", "saturationBands", "luminanceBands", "pointColors"},
        QStringList{"gradingHue", "gradingSaturation", "gradingLuminance", "gradingBalance",
                    "gradingBlending"},
        QStringList{"sharpening", "details"},
        QStringList{}};

    auto read_tone = [](const QJsonObject& recipe) {
        auto result = tone_from_json(recipe.value("tone").toObject());
        if (!recipe.contains("tone"))
            result.exposure = recipe.value("exposure").toDouble();
        return tone_json(result);
    };
    const auto tone = read_tone(source);
    auto target = read_tone(target_recipe);
    for (int group = 0; group < 7; ++group)
        if (groups[group])
            for (const auto& field : fields[group])
                target.insert(field, tone.value(field));
    auto flags = target.value("sections").toArray();
    const auto source_flags = tone.value("sections").toArray();
    const std::array<int, 7> sections{0, 1, 6, 3, 4, 5, 7};
    for (int i = 0; i < 7; ++i)
        if (groups[i])
            flags[sections[i]] = source_flags[sections[i]];
    if (groups[3])
        flags[2] = source_flags[2];
    if (groups[10]) {
        target.insert("calibration", tone.value("calibration"));
        flags[8] = source_flags[8];
    }
    if (std::all_of(groups.begin(), groups.begin() + 7, [](bool selected) { return selected; }))
        target.insert("legacyColorTransform", tone.value("legacyColorTransform"));
    target.insert("sections", flags);
    target_recipe.insert("tone", target);
    target_recipe.insert("exposure", target.value("exposure"));
    QStringList copied;
    if (groups[1])
        copied << "whiteBalance" << "profile";
    if (groups[7])
        copied << "denoise" << "denoiseAmount" << "directml" << "modelPath" << "customModel"
               << "demosaic";
    if (groups[8])
        copied << "locals";
    if (groups[9])
        copied << "crop" << "quarterTurns";
    if (groups[11])
        copied << "optics";
    if (groups[12])
        copied << "geometry";
    if (groups[13])
        copied << "healing";
    for (const auto& key : copied) {
        if (source.contains(key))
            target_recipe.insert(key, source.value(key));
        else
            target_recipe.remove(key);
    }
    return target_recipe;
}
RawGroups raw_preset_groups(const QJsonObject& preset) {
    const auto array = preset.value("groups").toArray();
    if (array.size() != 14)
        throw EditError(QStringLiteral("RAW 预设分组无效"));
    RawGroups result{};
    for (int i = 0; i < 14; ++i) {
        if (!array[i].isBool())
            throw EditError(QStringLiteral("RAW 预设分组无效"));
        result[i] = array[i].toBool();
    }
    return result;
}
QJsonObject raw_preset(const QString& name, const QJsonObject& recipe, const RawGroups& groups) {
    if (name.trimmed().isEmpty() || name.size() > 120)
        throw EditError(QStringLiteral("请输入不超过120字的预设名称"));
    QJsonArray selection;
    bool any{};
    for (bool group : groups) {
        selection.append(group);
        any |= group;
    }
    if (!any)
        throw EditError(QStringLiteral("请至少选择一组设置"));
    return {{"type", "huelith.raw-preset"},
            {"version", 1},
            {"name", name.trimmed()},
            {"groups", selection},
            {"recipe", transfer_raw_settings(recipe, {}, groups)}};
}
QJsonObject read_raw_preset(const QByteArray& data) {
    if (data.size() > 16 * 1024 * 1024)
        throw EditError(QStringLiteral("RAW 预设文件过大"));
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(data, &error);
    const auto object = document.object();
    if (error.error != QJsonParseError::NoError || object.value("type") != "huelith.raw-preset" ||
        object.value("version").toInt() != 1 || !object.value("recipe").isObject())
        throw EditError(QStringLiteral("不是有效的 Huelith RAW 预设"));
    auto result = raw_preset(object.value("name").toString(), object.value("recipe").toObject(),
                             raw_preset_groups(object));
    QImage fixture(8, 8, QImage::Format_RGBA32FPx4_Premultiplied);
    fixture.setColorSpace(working_space());
    fixture.fill(Qt::gray);
    auto validation_recipe = result.value("recipe").toObject();
    // Camera matrices are validated structurally here. Applying them requires
    // the destination photo's actual camera metadata, absent from portable presets.
    (void)validate_raw_profile(validation_recipe.value("profile").toObject());
    validation_recipe.remove("profile");
    (void)white_balance_from_json(validation_recipe.value("whiteBalance").toObject());
    validation_recipe.remove("whiteBalance");
    (void)apply_raw_adjustments(fixture, validation_recipe);
    return result;
}
} // namespace huelith::editing
