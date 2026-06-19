#include "TaskPanel.h"

#include "Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

static const TaskRecord *findRecord(const TaskManager *manager, int id)
{
    for (const auto &r : manager->tasks())
        if (r.id == id) return &r;
    return nullptr;
}

TaskPanel::TaskPanel(TaskManager *manager, QWidget *parent)
    : QFrame(parent)
    , m_manager(manager)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Theme::bgList);
    setPalette(pal);
    setFrameShape(QFrame::NoFrame);

    auto *separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    QPalette sepPal = separator->palette();
    sepPal.setColor(QPalette::Mid, Theme::borderNormal);
    separator->setPalette(sepPal);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(8, 4, 8, 4);
    m_layout->setSpacing(2);
    m_layout->addWidget(separator);

    connect(manager, &TaskManager::taskAdded,   this, &TaskPanel::onTaskAdded);
    connect(manager, &TaskManager::taskUpdated, this, &TaskPanel::onTaskUpdated);

    refreshVisibility();
}

void TaskPanel::onTaskAdded(int id)
{
    const TaskRecord *record = findRecord(m_manager, id);
    if (record) addRow(*record);
}

void TaskPanel::onTaskUpdated(int id)
{
    const TaskRecord *record = findRecord(m_manager, id);
    if (!record) return;

    const bool terminal = record->status == TaskStatus::Finished
                       || record->status == TaskStatus::Cancelled
                       || record->status == TaskStatus::Failed;
    if (terminal) {
        removeRow(id);
        return;
    }

    if (m_rows.contains(id))
        updateRow(*record);
}

void TaskPanel::addRow(const TaskRecord &record)
{
    auto *row = new QWidget(this);
    auto *hbox = new QHBoxLayout(row);
    hbox->setContentsMargins(0, 3, 0, 3);
    hbox->setSpacing(8);

    auto *name = new QLabel(record.name, row);
    name->setFixedWidth(180);
    {
        QPalette p = name->palette();
        p.setColor(QPalette::WindowText, Theme::textPrimary);
        name->setPalette(p);
    }

    auto *bar = new QProgressBar(row);
    bar->setFixedHeight(14);
    bar->setTextVisible(false);
    {
        QPalette p = bar->palette();
        p.setColor(QPalette::Highlight, Theme::accent);
        p.setColor(QPalette::Base,      Theme::bgInput);
        bar->setPalette(p);
    }

    auto *message = new QLabel(row);
    message->setFixedWidth(200);
    {
        QPalette p = message->palette();
        p.setColor(QPalette::WindowText, Theme::textPlaceholder);
        message->setPalette(p);
    }

    auto *cancelBtn = new QPushButton("✕", row);
    cancelBtn->setFlat(true);
    cancelBtn->setFixedSize(22, 22);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    {
        QPalette p = cancelBtn->palette();
        p.setColor(QPalette::ButtonText, Theme::textPlaceholder);
        cancelBtn->setPalette(p);
    }
    const int id = record.id;
    connect(cancelBtn, &QPushButton::clicked, this, [this, id] {
        m_manager->cancel(id);
    });

    hbox->addWidget(name);
    hbox->addWidget(bar, 1);
    hbox->addWidget(message);
    hbox->addWidget(cancelBtn);

    m_layout->addWidget(row);
    m_rows[record.id] = Row{row, name, bar, message, cancelBtn};

    updateRow(record);
    refreshVisibility();
}

void TaskPanel::updateRow(const TaskRecord &record)
{
    auto it = m_rows.find(record.id);
    if (it == m_rows.end()) return;

    Row &row = it.value();

    if (record.status == TaskStatus::Pending) {
        row.bar->setRange(0, 0);
        row.message->setText("Queued");
    } else {
        row.bar->setRange(0, 100);
        row.bar->setValue(record.percent);
        row.message->setText(record.message);
    }
}

void TaskPanel::removeRow(int id)
{
    auto it = m_rows.find(id);
    if (it == m_rows.end()) return;
    it.value().widget->deleteLater();
    m_rows.erase(it);
    refreshVisibility();
}

void TaskPanel::refreshVisibility()
{
    setVisible(!m_rows.isEmpty());
}
