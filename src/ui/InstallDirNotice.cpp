#include "ui/InstallDirNotice.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

InstallDirNotice::InstallDirNotice(QWidget *parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::StyledPanel);

    auto *box = new QHBoxLayout(this);
    box->setContentsMargins(Theme::spacingBase, Theme::spacingSm, Theme::spacingBase, Theme::spacingSm);
    box->setSpacing(Theme::spacingSm);

    m_label = new QLabel("No Path of Exile install directories are configured.", this);
    QPalette pal = m_label->palette();
    pal.setColor(QPalette::WindowText, Theme::textPlaceholder);
    m_label->setPalette(pal);
    box->addWidget(m_label, 1);

    m_addBtn = new QPushButton("Add install directory…", this);
    connect(m_addBtn, &QPushButton::clicked, this, &InstallDirNotice::addClicked);
    box->addWidget(m_addBtn, 0);
}
