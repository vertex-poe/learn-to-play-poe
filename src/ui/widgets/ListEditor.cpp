#include "ui/widgets/ListEditor.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

static constexpr int BuiltinRole = Qt::UserRole;

ListEditor::ListEditor(const QString &placeholder, QWidget *parent)
    : QWidget(parent)
{
    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);

    auto *inputRow  = new QHBoxLayout;
    m_fileBrowseBtn = new QPushButton("Browse...", this);
    m_fileBrowseBtn->setVisible(false);
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(placeholder);
    m_addBtn = new QPushButton("Add", this);
    inputRow->addWidget(m_fileBrowseBtn);
    inputRow->addWidget(m_input);
    inputRow->addWidget(m_addBtn);
    vbox->addLayout(inputRow);

    m_list = new QListWidget(this);
    m_list->setFixedHeight(110);
    vbox->addWidget(m_list);

    m_removeBtn = new QPushButton("Remove selected", this);
    m_removeBtn->setEnabled(false);
    vbox->addWidget(m_removeBtn, 0, Qt::AlignRight);

    connect(m_addBtn,  &QPushButton::clicked,     this, &ListEditor::addCurrent);
    connect(m_input,   &QLineEdit::returnPressed,  this, &ListEditor::addCurrent);
    connect(m_removeBtn, &QPushButton::clicked,   this, &ListEditor::removeSelected);
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this]() {
        const bool hasUserItem = std::any_of(
            m_list->selectedItems().begin(), m_list->selectedItems().end(),
            [](QListWidgetItem *it) { return !it->data(BuiltinRole).toBool(); });
        m_removeBtn->setEnabled(hasUserItem);
    });
}

QStringList ListEditor::items() const
{
    QStringList result;
    result.reserve(m_list->count());
    for (int i = 0; i < m_list->count(); ++i) {
        if (!m_list->item(i)->data(BuiltinRole).toBool())
            result << m_list->item(i)->text();
    }
    return result;
}

void ListEditor::setItems(const QStringList &items)
{
    // Remove existing user items (keep builtins).
    for (int i = m_list->count() - 1; i >= 0; --i) {
        if (!m_list->item(i)->data(BuiltinRole).toBool())
            delete m_list->takeItem(i);
    }
    for (const QString &item : items)
        m_list->addItem(item);
}

void ListEditor::setBuiltinItems(const QStringList &items)
{
    m_builtinItems = items;
    const QStringList userItems = this->items();
    m_list->clear();
    for (const QString &bi : m_builtinItems)
        addBuiltinRow(bi);
    for (const QString &ui : userItems)
        m_list->addItem(ui);
}

void ListEditor::setInputFileBrowser(bool enabled)
{
    m_fileBrowseBtn->setVisible(enabled);
    if (!enabled)
        return;
    connect(m_fileBrowseBtn, &QPushButton::clicked, this, [this]() {
#ifdef Q_OS_WIN
        const QString filter = "Executables (*.exe);;All files (*.*)";
#else
        const QString filter = "All files (*)";
#endif
        const QString file = QFileDialog::getOpenFileName(this, "Select executable", {}, filter);
        if (!file.isEmpty())
            m_input->setText(QFileInfo(file).fileName());
    });
}

void ListEditor::setBrowseForDirectory(bool enabled)
{
    m_browseMode = enabled;
    m_input->setVisible(!enabled);
    m_addBtn->setText(enabled ? "Browse..." : "Add");
}

void ListEditor::addBuiltinRow(const QString &text)
{
    auto *item = new QListWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled); // visible but not selectable
    QFont f = item->font();
    f.setItalic(true);
    item->setFont(f);
    item->setForeground(QColor(140, 140, 140));
    item->setData(BuiltinRole, true);
    m_list->addItem(item);
}

void ListEditor::addCurrent()
{
    if (m_browseMode) {
        const QString dir = QFileDialog::getExistingDirectory(this, "Select directory");
        if (dir.isEmpty())
            return;
        m_list->addItem(dir);
        emit itemsChanged();
        return;
    }

    const QString text = m_input->text().trimmed();
    if (text.isEmpty())
        return;

    // Reject duplicates (case-insensitive, including builtins).
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->text().compare(text, Qt::CaseInsensitive) == 0)
            return;
    }

    m_list->addItem(text);
    m_input->clear();
    emit itemsChanged();
}

void ListEditor::removeSelected()
{
    for (auto *item : m_list->selectedItems()) {
        if (!item->data(BuiltinRole).toBool())
            delete item;
    }
    emit itemsChanged();
}
