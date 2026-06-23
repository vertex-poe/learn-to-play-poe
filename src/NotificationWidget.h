#pragma once

#include "Docs.h"

#include <QColor>
#include <QFrame>
#include <QList>
#include <QPair>
#include <QString>

struct NotificationStyle {
    QColor background{45, 45, 45};
    QColor border{80, 80, 80};
    QColor accentColor{200, 168, 75};    // amber-gold for titles and tags
    QColor textColor{Qt::white};
    QColor bodyColor{180, 180, 180};
    QColor timestampColor{140, 140, 140};
    int    borderRadius{6};
    int    borderWidth{1};
};

class QHBoxLayout;
class QLabel;
class QMouseEvent;
class QVBoxLayout;

class NotificationWidget : public QFrame
{
    Q_OBJECT
public:
    explicit NotificationWidget(const QString &title, const QString &tag,
                                const QString &message, const QString &timestamp,
                                const NotificationStyle &style = {},
                                QWidget *parent = nullptr);

    void setMessage(const QString &text);
    void setHeaderSuffix(const QString &text);
    void setSource(const DocSource &source);
    void setDetailRows(const QList<QPair<QString, QString>> &rows);
    void setAreaName(const QString &name);
    void appendTopRowTag(const QString &tag);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;

private:
    NotificationStyle  m_style;
    QHBoxLayout       *m_topRow{};
    QHBoxLayout       *m_leftLayout{};
    QVBoxLayout       *m_outerLayout{};
    QWidget           *m_bodyWidget{};
    QLabel            *m_headerSuffixLabel{};
    QLabel            *m_expandIndicator{};
    QWidget           *m_sourceIcon{};
    QWidget           *m_separator{};
    QWidget           *m_detailWidget{};
    bool               m_expanded{false};
};
