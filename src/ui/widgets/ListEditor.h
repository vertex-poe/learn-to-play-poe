#pragma once

#include <QStringList>
#include <QWidget>

class QLineEdit;
class QListWidget;
class QPushButton;

class ListEditor : public QWidget
{
    Q_OBJECT

public:
    explicit ListEditor(const QString &placeholder = {}, QWidget *parent = nullptr);

    QStringList items() const;      // returns only user-added items, not builtins
    void setItems(const QStringList &items);

    // Prepend read-only, grayed items that cannot be removed.
    void setBuiltinItems(const QStringList &items);

    // Show a Browse button left of the input that picks a file and fills the input
    // with its basename. Call before the widget is shown.
    void setInputFileBrowser(bool enabled);

    // Hide the text input and relabel the add button to open a directory picker.
    void setBrowseForDirectory(bool enabled);

signals:
    void itemsChanged();

private:
    void addCurrent();
    void removeSelected();
    void addBuiltinRow(const QString &text);

    QPushButton *m_fileBrowseBtn{};
    QLineEdit   *m_input{};
    QPushButton *m_addBtn{};
    QListWidget *m_list{};
    QPushButton *m_removeBtn{};

    QStringList m_builtinItems;
    bool        m_browseMode{false};
};
