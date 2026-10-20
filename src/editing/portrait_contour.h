#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QImage>
#include <QPointF>
#include <QSize>
#include <QRectF>
#include <opencv2/core.hpp>
#include <vector>
#include <array>

namespace huelith::editing {
struct PortraitEyeTarget { double size{},spacing{},height{},tilt{},length{},innerCorner{},outerCorner{},aperture{},upperLift{},lowerLid{},outerLift{}; };
struct PortraitNoseTarget { double width{},length{},size{},rootWidth{},bridgeWidth{},tipWidth{}; };
struct PortraitMouthTarget {
    double size{},thickness{};std::array<double,2> smile{};
    double width{},height{},offset{},tilt{},upper{},lower{},cupid{};
};
struct PortraitBrowTarget { double height{},tilt{},length{},spacing{},headSpacing{},archHeight{},archPosition{},thickness{}; };
struct PortraitProportionTarget { double midface{},philtrum{},lowerface{}; };
struct PortraitOutlineTarget { double chinWidth{},smooth{},jawAngle{}; std::array<double,2> temple{}; };
struct PortraitLocalSkinLift {std::array<double,4> amount{};QImage skin;};
struct PortraitForeheadLift {double amount{};QImage skin;};
struct PortraitHeadTarget {
    double size{};QImage region;QJsonArray pose;std::vector<QPointF> background;
    double forehead{},crown{},hairline{},hairVolume{},roll{};QJsonObject scalp;QImage hairRegion;QJsonObject manual;
    bool active() const {return roll!=0 || size!=0 || forehead!=0 || crown!=0 || hairline!=0 || hairVolume!=0;}
};
// Inverse sampling field fitted to semantic contour targets, with stationary
// feature anchors. Coordinates use pixels / face width (not image aspect ratio).
class PortraitContour {
public:
    PortraitContour(QSize size, const QJsonArray& landmarks,
                    double width, double cheek, double jaw, double chin,
                    std::array<PortraitEyeTarget,2> eyes={},const QImage& obstacles={},PortraitNoseTarget nose={},PortraitMouthTarget mouth={},std::array<PortraitBrowTarget,2> brows={},PortraitProportionTarget proportions={},PortraitOutlineTarget outline={},PortraitHeadTarget head={},std::array<double,2> malar={},double cheekFill=0,std::array<double,2> lift={},PortraitForeheadLift foreheadLift={},PortraitLocalSkinLift skinLift={},std::array<double,2> midfaceLift={});
    bool empty() const { return centers_.empty() && foreheadAmount_==0 && skinLiftAmount_[0]==0 && skinLiftAmount_[1]==0 && skinLiftAmount_[2]==0 && skinLiftAmount_[3]==0; }
    bool has_forehead_lift() const {return foreheadAmount_>0 && !foreheadWeights_.empty() && cv::norm(foreheadWeights_,cv::NORM_INF)>.05;}
    bool has_local_lift(int side) const {return skinLiftAmount_[side]>0 && !skinLiftWeights_[side].empty() && cv::norm(skinLiftWeights_[side],cv::NORM_INF)>.05;}
    QPointF displacement(QPointF normalized) const;
    QRectF bounds() const { return bounds_; }
private:
    QPointF local(QPointF normalized) const;
    QPointF origin_, axis_, vertical_;
    QSize size_;
    double scale_{1};
    double support_{.65};
    double edge_fade_{.08};
    std::vector<QPointF> centers_;
    cv::Mat coefficients_, volumeInverse_, foreheadWeights_;
    std::array<cv::Mat,4> skinLiftWeights_;
    std::array<QRectF,4> skinLiftBounds_;
    std::array<double,4> skinLiftAmount_{};
    QRectF foreheadBounds_;
    double foreheadAmount_{};
    QRectF volumeBounds_;
    QRectF bounds_;
};
}
