#pragma once
#include <QImage>
#include <QTransform>
#include <QPainterPath>
#include <QJsonObject>
#include <QString>
#include <atomic>
#include <array>
#include <QJsonArray>
#include <vector>
#include <optional>
namespace huelith::editing {
struct PortraitParameter { QString key,label; bool bilateral{}; double minimum{-100},maximum{100}; };
std::vector<PortraitParameter> portrait_parameters(int panel);
// Persisted parameter semantics are independent of the current UI page.
std::optional<PortraitParameter> portrait_parameter(const QString& key);
QJsonObject portrait_makeup_combination(int style);
QJsonValue portrait_side_value(const QJsonObject& settings,const QString& key,bool left);
void portrait_set_side_value(QJsonObject& settings,const QString& key,const QJsonValue& value);
void portrait_set_style_color(QJsonObject& settings,const QString& key,const QJsonValue& value);
QJsonObject portrait_portable_settings(const QJsonObject& settings,const QStringList& categories);
QJsonObject portrait_merge_preset(QJsonObject target,const QJsonObject& preset,double strength);
QImage portrait_deformation(QSize size,const QJsonObject& recipe,double amount=1,
                           const QImage& effect_mask={},const std::atomic_bool* cancelled=nullptr,bool validate_only=false);
// Rebase protection without changing the visible deformation. Call before
// changing its mask, including clearing/thawing it.
QJsonObject portrait_capture_protection(QSize size,const QJsonObject& recipe,int person);
QImage portrait_protection_weights(const QJsonObject& settings,QSize grid);
QImage portrait_manual_deformation(QSize size,const QJsonObject& recipe,int person);
struct PortraitManualInputs { QImage mesh,anchor,residual; };
QImage portrait_deformation_with_manual(QSize size,const QJsonObject& recipe,int person,
                                       QImage& manual,const std::atomic_bool* cancelled=nullptr,
                                       PortraitManualInputs* inputs=nullptr);
QImage apply_portrait(const QImage& source,const QJsonObject& recipe,double amount=1,
                     const QImage& effect_mask={},const std::atomic_bool* cancelled=nullptr);
QImage portrait_region(const QImage& source,const QJsonObject& recipe,QRect region,
                       const std::atomic_bool* cancelled=nullptr);
std::array<double,3> portrait_accessory_color(const QString& material,QPointF uv,const QJsonArray& color,double roughness);
QTransform portrait_accessory_transform(const QJsonObject& person,const QJsonObject& parameters,QSize size);
QImage portrait_accessory_coverage(const QJsonObject& person,QSize size);
QImage portrait_effect_mask(const QJsonObject& person,const QString& key,QSize size);
QJsonObject portrait_tear_volume(const QImage& source,const QJsonObject& person);
QJsonObject portrait_perioral_volume(const QImage& source,const QJsonObject& person);
QJsonObject portrait_orbital_volume(const QImage& source,const QJsonObject& person);
QJsonObject portrait_cheek_volume(const QImage& source,const QJsonObject& person);
QString portrait_arm_unavailable(const QJsonObject& person,QSize size,int side);
QString portrait_neck_unavailable(const QJsonObject& person,QSize size);
QString portrait_height_unavailable(const QJsonObject& person,QSize size,double amount);
QString portrait_leg_length_unavailable(const QJsonObject& person,QSize size,int side,double amount,bool paired=false);
QString portrait_straight_leg_unavailable(const QJsonObject& person,QSize size,int side);
QString portrait_back_unavailable(const QJsonObject& person,QSize size);
QString portrait_abdomen_unavailable(const QJsonObject& person,QSize size,bool local=false);
bool portrait_perioral_lift_available(const QJsonObject& person,QSize size,int side);
bool portrait_orbital_lift_available(const QJsonObject& person,QSize size,int side);
bool portrait_forehead_lift_available(const QJsonObject& person,QSize size);
QJsonObject portrait_lift_combination(const QImage& source,const QJsonObject& person,const std::atomic_bool* cancelled=nullptr);
QJsonObject portrait_volume_combination(const QImage& source,const QJsonObject& person,const std::atomic_bool* cancelled=nullptr);
QJsonObject portrait_malar_volume(const QImage& source,const QJsonObject& person);
QJsonObject portrait_forehead_volume(const QImage& source,const QJsonObject& person);
QImage portrait_volume_region(QImage base,QSize size,const QJsonObject& person,QRect region,double amount,const QImage& effect_mask,const std::atomic_bool* cancelled);
QJsonObject portrait_contour_lighting(const QImage& source,const QJsonObject& person);
QTransform portrait_mole_transform(const QJsonObject& person,QSize size);
QTransform portrait_sticker_surface(const QJsonObject& person,QSize size);
QTransform portrait_sticker_transform(const QJsonObject& person,QSize size);
QPainterPath portrait_tattoo_path(const QString& material);
QTransform portrait_tattoo_transform(const QJsonObject& person,QSize size);
QImage portrait_tattoo_coverage(const QJsonObject& person,QSize size);
QImage portrait_tattoo_region(QImage base,QSize size,const QJsonObject& person,QRect region,double amount,const QImage& effect_mask,const std::atomic_bool* cancelled);
QPainterPath portrait_nail_path(const QJsonObject& nail,QSize size);
QJsonArray portrait_detect_nails(const QImage& image,const QString& model);
QImage portrait_nail_coverage(const QJsonObject& person,QSize size);
QImage portrait_nails_region(QImage base,QSize size,const QJsonObject& person,QRect region,double amount,const QImage& effect_mask,const std::atomic_bool* cancelled);
QPainterPath portrait_sticker_path(const QString& material);
QString portrait_mask_png(const QImage& image);
QJsonObject portrait_double_chin_analysis(const QImage& source,const QJsonObject& person);
QJsonObject portrait_repair_analysis(const QImage& source,const QJsonObject& person,const QImage& blemish_evidence = {});
// Detection evidence only; never modifies pixels or performs inpainting.
QImage portrait_blemish_evidence(const QImage& source,const QJsonObject& person,const QString& model);
QImage portrait_frame_protection(const QImage& source,const QJsonObject& person);
QPainterPath portrait_head_outline(const QJsonArray& points,QSize size);
QImage portrait_head_region(const QJsonObject& person);
QJsonObject portrait_scalp_geometry(const QJsonObject& person);
QImage portrait_repair_region(const QImage& source,const QJsonObject& recipe,QRect region,double amount,
                              const std::atomic_bool* cancelled=nullptr,const QImage& effect_mask={});
QImage portrait_style_mask(const QJsonObject& person,const QString& key,QSize size);
QImage portrait_paint_region(QImage base,QSize size,const QJsonObject& person,QRect region,double amount,const QImage& effect,const std::atomic_bool* cancelled);
QImage portrait_decals_region(QImage base,QSize size,const QJsonObject& person,QRect region,double amount,const QImage& effect,const std::atomic_bool* cancelled);
QImage portrait_style_region(QImage base,QSize source_size,const QJsonObject& recipe,QRect region,double amount,
                             const std::atomic_bool* cancelled=nullptr,const QImage& effect_mask={});
QImage portrait_color_region(const QImage& source,const QJsonObject& recipe,QRect region,double amount,
                             const std::atomic_bool* cancelled=nullptr,const QImage& effect_mask={});
QJsonObject portrait_makeup_repair(const QImage& source,const QJsonObject& person,const QJsonObject& stroke,bool manual_source);
QImage portrait_makeup_repair_mask(const QJsonObject& person,const QString& part,QSize size);
QJsonArray portrait_conceal_reference(const QImage& source,const QJsonObject& person,QPointF point,double radius);
QJsonArray portrait_balance_reference(const QImage& source,const QJsonObject& person);
QJsonArray portrait_sample_skin_color(const QImage& source,QPoint position);
}
