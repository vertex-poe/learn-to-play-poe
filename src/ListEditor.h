#pragma once

#include <QWidget>
#include <QStringList>

class QLineEdit;
class QListWidget;
class QPushButton;

class ListEditor : public QWidget
{
    Q_OBJECT

public:
    explicit ListEditor(const QString &placeholder = {}, QWidget *parent = nullptr);

    QStringList items() const;
    void        setItems(const QStringList &items);

signals:
    void itemsChanged();

private:
    void addCurrent();
    void removeSelected();

    QLineEdit   *m_input{};
    QListWidget *m_list{};
    QPushButton *m_removeBtn{};
};
