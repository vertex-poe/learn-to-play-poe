#pragma once

#include <QProxyStyle>

// Custom application style wrapping Fusion. Overrides drawing for the
// controls whose appearance can't be expressed via QPalette alone:
// scrollbars, push buttons, line edit panels, and checkbox indicators.
class AppStyle : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;

    int   pixelMetric(PixelMetric metric, const QStyleOption *option,
                      const QWidget *widget) const override;

    QRect subControlRect(ComplexControl cc, const QStyleOptionComplex *option,
                         SubControl sc, const QWidget *widget) const override;

    void  drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                             QPainter *painter, const QWidget *widget) const override;

    void  drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                        QPainter *painter, const QWidget *widget) const override;

    void  drawControl(ControlElement element, const QStyleOption *option,
                      QPainter *painter, const QWidget *widget) const override;
};
