#include "ListEditor.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

ListEditor::ListEditor(const QString &placeholder, QWidget *parent)
    : QWidget(parent)
{
    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);

    auto *inputRow = new QHBoxLayout;
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(placeholder);
    auto *addBtn = new QPushButton("Add", this);
    inputRow->addWidget(m_input);
    inputRow->addWidget(addBtn);
    vbox->addLayout(inputRow);

    m_list = new QListWidget(this);
    m_list->setFixedHeight(100);
    vbox->addWidget(m_list);

    m_removeBtn = new QPushButton("Remove selected", this);
    m_removeBtn->setEnabled(false);
    vbox->addWidget(m_removeBtn, 0, Qt::AlignRight);

    connect(addBtn,     &QPushButton::clicked,  this, &ListEditor::addCurrent);
    connect(m_input,    &QLineEdit::returnPressed, this, &ListEditor::addCurrent);
    connect(m_removeBtn,&QPushButton::clicked,  this, &ListEditor::removeSelected);
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this]() {
        m_removeBtn->setEnabled(!m_list->selectedItems().isEmpty());
    });
}

QStringList ListEditor::items() const
{
    QStringList result;
    result.reserve(m_list->count());
    for (int i = 0; i < m_list->count(); ++i)
        result << m_list->item(i)->text();
    return result;
}

void ListEditor::setItems(const QStringList &items)
{
    m_list->clear();
    m_list->addItems(items);
}

void ListEditor::addCurrent()
{
    const QString text = m_input->text().trimmed();
    if (text.isEmpty())
        return;
    m_list->addItem(text);
    m_input->clear();
    emit itemsChanged();
}

void ListEditor::removeSelected()
{
    const auto selected = m_list->selectedItems();
    for (auto *item : selected)
        delete item;
    emit itemsChanged();
}
