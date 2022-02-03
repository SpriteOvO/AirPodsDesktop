//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021-2022 SpriteOvO
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

//
// The original code of this widget was taken from another project.
// https://github.com/feiyangqingyun/QWidgetDemo
//
// And a lot of modifications were made to fit the needs of the project
//
// The author of the original project has licensed "Feel free to use" in the "About" section of
// the GitHub repository, and the code is licensed under the "Mulan PSL License" as well.
//

#pragma once

#include <QWidget>

namespace Gui::Widget {

class Battery : public QWidget
{
    Q_OBJECT

    Q_PROPERTY(ValueType minValue READ getMinValue WRITE setMinValue)
    Q_PROPERTY(ValueType maxValue READ getMaxValue WRITE setMaxValue)
    Q_PROPERTY(ValueType alarmValue READ getAlarmValue WRITE setAlarmValue)
    Q_PROPERTY(ValueType value READ getValue WRITE setValue)

    Q_PROPERTY(qreal borderWidth READ getBorderWidth WRITE setBorderWidth)
    Q_PROPERTY(qreal borderRadius READ getBorderRadius WRITE setBorderRadius)
    Q_PROPERTY(qreal backgroundRadius READ getBackgroundRadius WRITE setBackgroundRadius)
    Q_PROPERTY(qreal headRadius READ getHeadRadius WRITE setHeadRadius)

    Q_PROPERTY(QColor borderColor READ getBorderColor WRITE setBorderColor)
    Q_PROPERTY(QColor alarmColor READ getAlarmColor WRITE setAlarmColor)
    Q_PROPERTY(QColor normalColor READ getNormalColor WRITE setNormalColor)

    Q_PROPERTY(bool isCharging READ isCharging WRITE setCharging)
    Q_PROPERTY(bool isShowText READ isShowText WRITE setShowText)
    Q_PROPERTY(qreal textPadding READ getTextPadding WRITE setTextPadding)

public:
    using ValueType = quint32;

    explicit Battery(QWidget *parent = nullptr);

    ValueType getMinValue() const;
    ValueType getMaxValue() const;
    ValueType getAlarmValue() const;
    ValueType getValue() const;

    qreal getBorderWidth() const;
    qreal getBorderRadius() const;
    qreal getBackgroundRadius() const;
    qreal getHeadRadius() const;

    QColor getBorderColor() const;
    QColor getAlarmColor() const;
    QColor getNormalColor() const;

    bool isCharging() const;
    bool isShowText() const;
    qreal getTextPadding() const;

    // QSize sizeHint() const override;
    // QSize minimumSizeHint() const override;

public Q_SLOTS:
    void setRange(ValueType minValue, ValueType maxValue);

    void setMinValue(ValueType value);
    void setMaxValue(ValueType value);
    void setAlarmValue(ValueType value);
    void setValue(ValueType value);

    void setBorderWidth(qreal value);
    void setBorderRadius(qreal value);
    void setBackgroundRadius(qreal value);
    void setHeadRadius(qreal value);

    void setBorderColor(const QColor &value);
    void setAlarmColor(const QColor &value);
    void setNormalColor(const QColor &value);

    void setCharging(bool value);
    void setShowText(bool value);
    void setTextPadding(qreal value);

    void setBatterySize(int width, int height);

Q_SIGNALS:
    void valueChanged(ValueType value);
    void chargingStateChanged(bool isCharging);

private:
    constexpr static inline qreal ChargingPadding = 3;

    ValueType _minValue{0};
    ValueType _maxValue{100};
    ValueType _alarmValue{20};
    ValueType _value{0};

    qreal _borderWidth{2};
    qreal _borderRadius{2};
    qreal _backgroundRadius{2};
    qreal _headRadius{2};

    QColor _borderColor{142, 142, 146};
    QColor _normalColor{101, 196, 102};
    QColor _alarmColor{235, 77, 61};

    bool _isCharging{false};
    bool _isShowText{true};
    qreal _textPadding{10};

    QRectF _batteryRect{};
    QRectF _headRect{};
    QRectF _chargingRect{};
    QRectF _textRect{};

    QSizeF _batterySize{};

    void drawBorder(QPainter &painter);
    void drawBackground(QPainter &painter);
    void drawHead(QPainter &painter);
    void drawChargingIcon(QPainter &painter);
    void drawText(QPainter &painter);

    qreal getHeadWidth() const;
    qreal getChargingIconWidth() const;

protected:
    void paintEvent(QPaintEvent *event) override;
};
} // namespace Gui::Widget
