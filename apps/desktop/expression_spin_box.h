#pragma once
#include <QDoubleSpinBox>
#include <QFocusEvent>
#include <QLineEdit>
#include <cmath>

namespace huelith::desktop {
// A bounded arithmetic parser for numeric controls; never evaluates script or code.
class NumericExpression {
  public:
    explicit NumericExpression(QString text, double ppi = 0, double unit = 1, double percent = 1)
        : ppi_(ppi), unit_(unit), percent_(percent), text_(std::move(text)) {}
    bool parse(double& result) {
        if (text_.size() > 256)
            return false;
        result = sum(0);
        space();
        return valid_ && position_ == text_.size() && std::isfinite(result);
    }

  private:
    void space() {
        while (position_ < text_.size() && text_[position_].isSpace())
            ++position_;
    }
    bool take(QChar c) {
        space();
        if (position_ < text_.size() && text_[position_] == c) {
            ++position_;
            return true;
        }
        return false;
    }
    double sum(int depth) {
        double value = product(depth);
        while (valid_) {
            if (take('+'))
                value += product(depth);
            else if (take('-'))
                value -= product(depth);
            else
                break;
        }
        return value;
    }
    double product(int depth) {
        double value = number(depth);
        while (valid_) {
            if (take('*'))
                value *= number(depth);
            else if (take('/')) {
                const double divisor = number(depth);
                if (divisor == 0) {
                    valid_ = false;
                    return 0;
                }
                value /= divisor;
            } else
                break;
        }
        return value;
    }
    double number(int depth) {
        if (depth > 16) {
            valid_ = false;
            return 0;
        }
        if (take('+'))
            return number(depth + 1);
        if (take('-'))
            return -number(depth + 1);
        if (take('(')) {
            const double value = sum(depth + 1);
            if (!take(')'))
                valid_ = false;
            return value;
        }
        space();
        const auto begin = position_;
        while (position_ < text_.size() && (text_[position_].isDigit() || text_[position_] == '.'))
            ++position_;
        bool ok = false;
        double value = text_.mid(begin, position_ - begin).toDouble(&ok);
        valid_ = valid_ && ok;
        if (take('%'))
            value = value * percent_ / 100;
        else {
            space();
            const auto start = position_;
            while (position_ < text_.size() && text_[position_].isLetter())
                ++position_;
            const auto suffix = text_.mid(start, position_ - start).toLower();
            if (!suffix.isEmpty()) {
                double factor = 1;
                if (ppi_ <= 0)
                    valid_ = false;
                else if (suffix == "px")
                    factor = 1;
                else if (suffix == "mm")
                    factor = ppi_ / 25.4;
                else if (suffix == "cm")
                    factor = ppi_ / 2.54;
                else if (suffix == "in")
                    factor = ppi_;
                else if (suffix == "pt")
                    factor = ppi_ / 72.;
                else
                    valid_ = false;
                value *= factor / unit_;
            }
        }
        return value;
    }
    double ppi_{}, unit_{1}, percent_{1};
    QString text_;
    qsizetype position_{};
    bool valid_{true};
};

class ExpressionSpinBox final : public QDoubleSpinBox {
  public:
    void setUnitContext(double ppi, double unit = 1, double percent = 1) {
        ppi_ = ppi;
        unit_ = unit;
        percent_ = percent;
    }
    explicit ExpressionSpinBox(QWidget* parent = nullptr) : QDoubleSpinBox(parent) {
        setKeyboardTracking(false);
        setToolTip(tr("支持 + − * / 和括号；尺寸百分比相对于调整前的尺寸"));
    }

  protected:
    QString textFromValue(double value) const override {
        QString text = QString::number(value, 'f', decimals());
        if (text.contains('.')) {
            while (text.endsWith('0'))
                text.chop(1);
            if (text.endsWith('.'))
                text.chop(1);
        }
        return text;
    }
    void focusOutEvent(QFocusEvent* event) override {
        const auto original = lineEdit()->text();
        const bool acceptable = hasAcceptableInput();
        QDoubleSpinBox::focusOutEvent(event);
        if (!acceptable)
            lineEdit()->setText(original);
    }
    QValidator::State validate(QString& text, int&) const override {
        double result;
        return NumericExpression(clean(text), ppi_, unit_, percent_).parse(result) &&
                       result >= minimum() && result <= maximum()
                   ? QValidator::Acceptable
                   : QValidator::Intermediate;
    }
    double valueFromText(const QString& text) const override {
        double result;
        return NumericExpression(clean(text), ppi_, unit_, percent_).parse(result) &&
                       result >= minimum() && result <= maximum()
                   ? result
                   : value();
    }

  private:
    double ppi_{}, unit_{1}, percent_{1};
    QString clean(QString text) const {
        if (!prefix().isEmpty() && text.startsWith(prefix()))
            text.remove(0, prefix().size());
        if (!suffix().isEmpty() && text.endsWith(suffix()))
            text.chop(suffix().size());
        return text.trimmed();
    }
};
} // namespace huelith::desktop
