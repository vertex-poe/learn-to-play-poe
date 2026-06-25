#pragma once

#include <QColor>
#include <QPixmap>
#include <QSize>
#include <QString>

class QApplication;

namespace Theme {

// --- Backgrounds ---
inline const QColor bgApp          { 30,  30,  30};  // #1e1e1e  main window / dialogs
inline const QColor bgMenuBar      { 35,  35,  35};  // #232323
inline const QColor bgMenu         { 42,  42,  42};  // #2a2a2a
inline const QColor bgInput        { 40,  40,  40};  // #282828  QLineEdit, QListWidget
inline const QColor bgButton       { 46,  46,  46};  // #2e2e2e
inline const QColor bgButtonHover  { 56,  56,  56};  // #383838
inline const QColor bgButtonPressed{ 36,  36,  36};  // #242424
inline const QColor bgList         { 36,  36,  36};  // #242424
inline const QColor bgListHover    { 45,  45,  45};  // #2d2d2d
inline const QColor bgListSelected { 54,  54,  54};  // #363636
inline const QColor bgScrollBar    { 34,  34,  34};  // #222222
inline const QColor bgMenuBarHover { 50,  50,  50};  // #323232
inline const QColor bgMenuItemHover{ 54,  54,  54};  // #363636

// --- Accent ---
inline const QColor accent         {200, 168,  75};  // #c8a84b

// --- Text ---
inline const QColor textPrimary    {208, 208, 208};  // #d0d0d0
inline const QColor textLabel      {192, 192, 192};  // #c0c0c0
inline const QColor textList       {200, 200, 200};  // #c8c8c8
inline const QColor textInput      {224, 224, 224};  // #e0e0e0
inline const QColor textPlaceholder{ 96,  96,  96};  // #606060
inline const QColor textDisabled   { 80,  80,  80};  // #505050
inline const QColor textSelected   { 26,  26,  26};  // #1a1a1a  text on accent highlight

// --- Borders ---
inline const QColor borderNormal   { 82,  82,  82};  // #525252
inline const QColor borderList     { 66,  66,  66};  // #424242
inline const QColor borderMenu     { 68,  68,  68};  // #444444
inline const QColor borderMenuBar  { 51,  51,  51};  // #333333
inline const QColor borderDisabled { 56,  56,  56};  // #383838
inline const QColor borderMenuSep  { 64,  64,  64};  // #404040
inline const QColor scrollHandle   { 72,  72,  72};  // #484848

// --- Spacing (Tailwind 4px-base scale with semantic names) ---
inline constexpr int spacingXs   =  4;  // space-1   4px
inline constexpr int spacingSm   =  8;  // space-2   8px
inline constexpr int spacingBase = 12;  // space-3  12px
inline constexpr int spacingLg   = 16;  // space-4  16px
inline constexpr int spacingXl   = 20;  // space-5  20px
inline constexpr int spacing2xl  = 24;  // space-6  24px
inline constexpr int spacing3xl  = 28;  // space-7  28px

// --- Font sizes (Tailwind typographic scale, px × 0.75 → pt at 96 DPI) ---
inline constexpr double fontXs   =  9.0;  // text-xs   12px
inline constexpr double fontSm   = 10.5;  // text-sm   14px
inline constexpr double fontBase = 12.0;  // text-base 16px
inline constexpr double fontLg   = 13.5;  // text-lg   18px
inline constexpr double fontXl   = 15.0;  // text-xl   20px
inline constexpr double font2xl  = 18.0;  // text-2xl  24px
inline constexpr double font3xl  = 22.5;  // text-3xl  30px
inline constexpr double font4xl  = 27.0;  // text-4xl  36px

// --- Checkbox indicator sizes (paired with font scale) ---
inline constexpr int checkboxSm   = 14;  // paired with fontSm
inline constexpr int checkboxBase = 20;  // paired with fontBase  (AppStyle default)

// --- Geometry ---
inline constexpr int borderRadius    = 3;
inline constexpr int buttonRadius    = 4;
inline constexpr int scrollBarWidth  = 8;
inline constexpr int scrollHandleMin = 24;

void apply(QApplication &app);

// Renders svgPath into a DPR-aware pixmap tinted with color.
// Must pass devicePixelRatioF() from the calling widget — the lr-rect
// workaround for Qt 5.15+ fractional-DPR overflow lives here and nowhere else.
QPixmap renderSvgIcon(const QString &svgPath, const QColor &color,
                      QSize logicalSize, qreal dpr);

} // namespace Theme
