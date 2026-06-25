#include "ui/Theme.h"
#include "ui/AppStyle.h"

#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QPalette>
#include <QStyleFactory>
#include <QSvgRenderer>

namespace Theme {

QPixmap renderSvgIcon(const QString &svgPath, const QColor &color,
                      QSize logicalSize, qreal dpr)
{
    const int pw = qRound(logicalSize.width()  * dpr);
    const int ph = qRound(logicalSize.height() * dpr);
    const QRectF lr(0, 0, qreal(pw) / dpr, qreal(ph) / dpr);
    QPixmap pix(pw, ph);
    pix.setDevicePixelRatio(dpr);
    pix.fill(Qt::transparent);
    { QPainter gp(&pix); QSvgRenderer(svgPath).render(&gp, lr); }
    { QPainter cp(&pix);
      cp.setCompositionMode(QPainter::CompositionMode_SourceIn);
      cp.fillRect(lr, color); }
    return pix;
}

void apply(QApplication &app)
{
    app.setStyle(new AppStyle(QStyleFactory::create("Fusion")));

    QFont defaultFont = app.font();
    defaultFont.setFamilies({"Whitney", "Segoe UI"});
    defaultFont.setStyleHint(QFont::Serif);
    defaultFont.setPointSizeF(fontBase);
    app.setFont(defaultFont);

    QPalette p;

    // --- Active group ---
    p.setColor(QPalette::Window,          bgApp);
    p.setColor(QPalette::WindowText,      textPrimary);
    p.setColor(QPalette::Base,            bgInput);
    p.setColor(QPalette::AlternateBase,   bgList);
    p.setColor(QPalette::Text,            textInput);
    p.setColor(QPalette::PlaceholderText, textPlaceholder);
    p.setColor(QPalette::Button,          bgButton);
    p.setColor(QPalette::ButtonText,      textPrimary);

    // Selection: dark bg + accent text so menus, lists, and menu bar all
    // get the "charcoal background / gold text" look via Fusion's default
    // selection drawing without needing extra ProxyStyle overrides.
    p.setColor(QPalette::Highlight,       bgListSelected);
    p.setColor(QPalette::HighlightedText, accent);

    p.setColor(QPalette::Link,            accent);
    p.setColor(QPalette::LinkVisited,     accent);
    p.setColor(QPalette::ToolTipBase,     bgMenu);
    p.setColor(QPalette::ToolTipText,     textPrimary);

    // Mid-tones used by Fusion for border rendering on widgets we don't
    // override (spin boxes, combo boxes, etc.).
    p.setColor(QPalette::Light,    bgButtonHover);
    p.setColor(QPalette::Midlight, bgButton);
    p.setColor(QPalette::Mid,      borderNormal);
    p.setColor(QPalette::Dark,     borderNormal);
    p.setColor(QPalette::Shadow,   borderNormal);

    // --- Disabled group ---
    p.setColor(QPalette::Disabled, QPalette::WindowText,  textDisabled);
    p.setColor(QPalette::Disabled, QPalette::Text,        textDisabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText,  textDisabled);
    p.setColor(QPalette::Disabled, QPalette::Button,      bgButton);
    p.setColor(QPalette::Disabled, QPalette::Base,        bgApp);
    p.setColor(QPalette::Disabled, QPalette::Highlight,   bgListSelected);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, textDisabled);

    app.setPalette(p);
}

} // namespace Theme
