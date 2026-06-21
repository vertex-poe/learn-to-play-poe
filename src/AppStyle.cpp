#include "AppStyle.h"
#include "Theme.h"

#include <QPainter>
#include <QPen>
#include <QStyleOptionButton>
#include <QStyleOptionFrame>
#include <QStyleOptionMenuItem>
#include <QStyleOptionSlider>

// ---------------------------------------------------------------------------
// Scrollbar geometry
// ---------------------------------------------------------------------------

int AppStyle::pixelMetric(PixelMetric metric, const QStyleOption *option,
                           const QWidget *widget) const
{
    if (metric == PM_ScrollBarExtent)    return Theme::scrollBarWidth;
    if (metric == PM_ScrollBarSliderMin) return Theme::scrollHandleMin;
    if (metric == PM_MenuBarVMargin)     return 6;
    if (metric == PM_IndicatorWidth || metric == PM_IndicatorHeight) return 20;
    return QProxyStyle::pixelMetric(metric, option, widget);
}

QRect AppStyle::subControlRect(ComplexControl cc, const QStyleOptionComplex *option,
                                SubControl sc, const QWidget *widget) const
{
    if (cc != CC_ScrollBar)
        return QProxyStyle::subControlRect(cc, option, sc, widget);

    const auto *sb = qstyleoption_cast<const QStyleOptionSlider *>(option);
    if (!sb) return {};

    const QRect  r     = sb->rect;
    const bool   horiz = sb->orientation == Qt::Horizontal;
    const int    total = horiz ? r.width() : r.height();
    const int    range = sb->maximum - sb->minimum;

    auto sliderRect = [&]() -> QRect {
        if (range == 0) return r;
        const int len = qMax(Theme::scrollHandleMin,
            int(qint64(sb->pageStep) * total / (range + sb->pageStep)));
        const int travel = total - len;
        const int pos = travel > 0
            ? int(qint64(sb->sliderPosition - sb->minimum) * travel / range)
            : 0;
        return horiz ? QRect(r.x() + pos, r.y(), len, r.height())
                     : QRect(r.x(), r.y() + pos, r.width(), len);
    };

    switch (sc) {
    case SC_ScrollBarGroove:  return r;
    case SC_ScrollBarSlider:  return sliderRect();
    case SC_ScrollBarAddLine:
    case SC_ScrollBarSubLine: return {};
    case SC_ScrollBarSubPage: {
        const QRect s = sliderRect();
        return horiz ? QRect(r.x(), r.y(), s.x() - r.x(), r.height())
                     : QRect(r.x(), r.y(), r.width(), s.y() - r.y());
    }
    case SC_ScrollBarAddPage: {
        const QRect s = sliderRect();
        return horiz ? QRect(s.right() + 1, r.y(), r.right() - s.right(), r.height())
                     : QRect(r.x(), s.bottom() + 1, r.width(), r.bottom() - s.bottom());
    }
    default: return {};
    }
}

// ---------------------------------------------------------------------------
// Scrollbar drawing
// ---------------------------------------------------------------------------

void AppStyle::drawComplexControl(ComplexControl control,
                                   const QStyleOptionComplex *option,
                                   QPainter *painter, const QWidget *widget) const
{
    if (control != CC_ScrollBar) {
        QProxyStyle::drawComplexControl(control, option, painter, widget);
        return;
    }

    const auto *sb = qstyleoption_cast<const QStyleOptionSlider *>(option);
    if (!sb) return;

    painter->fillRect(sb->rect, Theme::bgScrollBar);

    const QRect handle = subControlRect(CC_ScrollBar, sb, SC_ScrollBarSlider, widget);
    if (!handle.isValid()) return;

    const bool hovered = (sb->activeSubControls & SC_ScrollBarSlider)
                         && (sb->state & State_MouseOver);
    const QColor col = hovered ? Theme::accent : Theme::scrollHandle;

    const bool horiz  = sb->orientation == Qt::Horizontal;
    const int  margin = 1;
    const QRect inset = handle.adjusted(
        horiz ? 0 : margin,
        horiz ? margin : 0,
        horiz ? 0 : -margin,
        horiz ? -margin : 0
    );

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(col);
    const int r = qMin(inset.width(), inset.height()) / 2;
    painter->drawRoundedRect(inset, r, r);
    painter->restore();
}

// ---------------------------------------------------------------------------
// Primitive elements: line edit panel, checkbox indicator, menu frame
// ---------------------------------------------------------------------------

void AppStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                              QPainter *painter, const QWidget *widget) const
{
    switch (element) {

    case PE_PanelLineEdit: {
        painter->save();
        const bool focused = option->state & State_HasFocus;
        const bool enabled = option->state & State_Enabled;
        const QColor border = !enabled   ? Theme::borderDisabled
                            : focused    ? Theme::accent
                            :              Theme::borderNormal;
        painter->setPen(QPen(border, 1));
        painter->setBrush(enabled ? Theme::bgInput : Theme::bgApp);
        painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
        painter->restore();
        return;
    }

    case PE_IndicatorCheckBox: {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const bool checked = option->state & (State_On | State_NoChange);
        const bool enabled = option->state & State_Enabled;
        const QRect r = option->rect.translated(0, 2).adjusted(1, 1, -1, -1);
        const QColor bg = (checked && enabled) ? Theme::accent
                        : !enabled             ? Theme::bgApp
                        :                        Theme::bgInput;
        painter->setPen(QPen(enabled ? Theme::borderNormal : Theme::borderDisabled, 1));
        painter->setBrush(bg);
        painter->drawRoundedRect(r, 2, 2);
        if (checked && enabled) {
            const qreal s = qreal(r.width()) / 12.0;
            const QPointF c = QRectF(r).center();
            painter->setPen(QPen(Theme::textSelected, 1.5 * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->drawLine(QPointF(c.x() - 3*s, c.y()),     QPointF(c.x() - 1*s, c.y() + 2*s));
            painter->drawLine(QPointF(c.x() - 1*s, c.y() + 2*s), QPointF(c.x() + 3*s, c.y() - 2*s));
        }
        painter->restore();
        return;
    }

    case PE_FrameMenu: {
        painter->save();
        painter->setPen(Theme::borderMenu);
        painter->setBrush(Theme::bgMenu);
        painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
        painter->restore();
        return;
    }

    default:
        break;
    }

    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

// ---------------------------------------------------------------------------
// Control elements: push button bevel, menu bar items, menu bar background
// ---------------------------------------------------------------------------

void AppStyle::drawControl(ControlElement element, const QStyleOption *option,
                            QPainter *painter, const QWidget *widget) const
{
    switch (element) {

    case CE_PushButtonBevel: {
        const auto *btn = qstyleoption_cast<const QStyleOptionButton *>(option);
        if (!btn) { QProxyStyle::drawControl(element, option, painter, widget); return; }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        const bool hovered  = btn->state & State_MouseOver;
        const bool pressed  = btn->state & State_Sunken;
        const bool enabled  = btn->state & State_Enabled;
        const bool focused  = btn->state & (State_HasFocus | State_KeyboardFocusChange);

        QColor bg = Theme::bgButton;
        if (enabled) {
            if (pressed)      bg = Theme::bgButtonPressed;
            else if (hovered) bg = Theme::bgButtonHover;
        }

        const QColor border = (!enabled)         ? Theme::borderDisabled
                            : (hovered || focused) ? Theme::accent
                            :                        Theme::borderNormal;

        const QRect r = btn->rect.adjusted(0, 0, -1, -1);
        painter->setPen(QPen(border, 1));
        painter->setBrush(bg);
        painter->drawRoundedRect(r, Theme::buttonRadius, Theme::buttonRadius);

        if (focused && enabled) {
            QPen fp(Theme::accent);
            fp.setStyle(Qt::DotLine);
            painter->setPen(fp);
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(r.adjusted(2, 2, -2, -2),
                                     Theme::buttonRadius, Theme::buttonRadius);
        }

        painter->restore();
        return;
    }

    case CE_MenuBarEmptyArea: {
        painter->save();
        painter->fillRect(option->rect, Theme::bgMenuBar);
        painter->setPen(Theme::borderMenuBar);
        painter->drawLine(option->rect.bottomLeft(), option->rect.bottomRight());
        painter->restore();
        return;
    }

    case CE_MenuBarItem: {
        const auto *mbi = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
        if (!mbi) { QProxyStyle::drawControl(element, option, painter, widget); return; }

        painter->save();
        const bool selected = mbi->state & (State_Selected | State_Sunken);
        painter->fillRect(mbi->rect, selected ? Theme::bgMenuBarHover : Theme::bgMenuBar);

        if (selected) {
            painter->setPen(Theme::borderMenuBar);
            painter->drawRect(mbi->rect.adjusted(0, 0, -1, -1));
        }

        painter->setPen(selected ? Theme::accent : Theme::textPrimary);
        painter->setFont(mbi->font);
        int flags = Qt::AlignCenter | Qt::TextShowMnemonic
                  | Qt::TextDontClip | Qt::TextSingleLine;
        if (!styleHint(SH_UnderlineShortcut, mbi, widget))
            flags |= Qt::TextHideMnemonic;
        painter->drawText(mbi->rect, flags, mbi->text);
        painter->restore();
        return;
    }

    default:
        break;
    }

    QProxyStyle::drawControl(element, option, painter, widget);
}
