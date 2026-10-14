// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/editor/extras.h"

#include "app/media.h"
#include "app/panel.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace xpl::editor {

namespace {
enum Logo { Flag, Characters, Picture };
}

ExtrasDialog::ExtrasDialog(const Look &look, const StartButtonExtras &extras, QWidget *parent)
    : QDialog(parent)
    , m_look(look)
    , m_file(extras.logoFile)
{
    setWindowTitle(QStringLiteral("Extras"));
    auto *layout = new QVBoxLayout(this);
    auto *box = new QGroupBox(QStringLiteral("Start button"));
    auto *grid = new QGridLayout(box);
    grid->addWidget(new QLabel(QStringLiteral("&Label:")), 0, 0);
    m_label = new QLineEdit(extras.label);
    m_label->setPlaceholderText(look.classic() ? QStringLiteral("Start") : QStringLiteral("start"));
    qobject_cast<QLabel *>(grid->itemAtPosition(0, 0)->widget())->setBuddy(m_label);
    grid->addWidget(m_label, 0, 1, 1, 2);

    grid->addWidget(new QLabel(QStringLiteral("Logo:")), 1, 0, Qt::AlignTop);
    m_logo = new QButtonGroup(this);
    auto *flag = new QRadioButton(QStringLiteral("&Windows flag"));
    auto *characters = new QRadioButton(QStringLiteral("&Characters:"));
    auto *picture = new QRadioButton(QStringLiteral("&Picture:"));
    m_logo->addButton(flag, Flag);
    m_logo->addButton(characters, Characters);
    m_logo->addButton(picture, Picture);
    m_characters = new QLineEdit(extras.logoText);
    m_characters->setPlaceholderText(QStringLiteral("\U0001F427"));
    m_characters->setMaxLength(8);
    m_fileName = new QLabel;
    m_browse = new QPushButton(QStringLiteral("&Browse..."));
    grid->addWidget(flag, 1, 1, 1, 2);
    grid->addWidget(characters, 2, 1);
    grid->addWidget(m_characters, 2, 2);
    grid->addWidget(picture, 3, 1);
    auto *pictureRow = new QHBoxLayout;
    pictureRow->addWidget(m_fileName, 1);
    pictureRow->addWidget(m_browse);
    grid->addLayout(pictureRow, 3, 2);
    grid->setColumnStretch(2, 1);

    m_sample = new QLabel;
    m_sample->setAlignment(Qt::AlignCenter);
    m_sample->setMinimumHeight(40);
    grid->addWidget(m_sample, 4, 0, 1, 3);
    layout->addWidget(box);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *restore = buttons->addButton(QStringLiteral("&Restore XP's"), QDialogButtonBox::ResetRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(restore, &QPushButton::clicked, this, [this] {
        m_label->clear();
        m_characters->clear();
        m_file.clear();
        m_logo->button(Flag)->setChecked(true);
        changed();
    });

    (!extras.logoFile.isEmpty() ? picture : !extras.logoText.isEmpty() ? characters : flag)->setChecked(true);
    connect(m_label, &QLineEdit::textChanged, this, &ExtrasDialog::changed);
    connect(m_characters, &QLineEdit::textEdited, this, [this] {
        m_logo->button(Characters)->setChecked(true);
        changed();
    });
    connect(m_logo, &QButtonGroup::idClicked, this, &ExtrasDialog::changed);
    connect(m_browse, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(
            this, QStringLiteral("Browse"),
            m_file.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) : QFileInfo(m_file).absolutePath(),
            QStringLiteral("Pictures (*.png *.jpg *.jpeg *.bmp *.gif *.svg *.ico)"));
        if (file.isEmpty())
            return;
        m_file = file;
        m_logo->button(Picture)->setChecked(true);
        changed();
    });
    changed();
}

StartButtonExtras ExtrasDialog::extras() const
{
    StartButtonExtras e;
    e.label = m_label->text().trimmed();
    if (m_logo->checkedId() == Characters)
        e.logoText = m_characters->text().trimmed();
    else if (m_logo->checkedId() == Picture)
        e.logoFile = m_file;
    return e;
}

void ExtrasDialog::changed()
{
    m_fileName->setText(m_file.isEmpty() ? QStringLiteral("(none)") : QFileInfo(m_file).fileName());
    m_fileName->setToolTip(m_file);
    render();
}

void ExtrasDialog::render()
{
    // The button as the taskbar will have it, on the taskbar's colour
    // (what's round the button is transparent).
    const auto flag = m_look.classic() ? media::classicStartFlag() : media::startFlag();
    const auto button = flag ? startButton(m_look, *flag, extras()) : std::nullopt;
    if (!button) {
        m_sample->setText(QStringLiteral("(The Start button needs the XP media to be drawn.)"));
        return;
    }
    QImage shown(button->normal.size() + QSize(16, 0), QImage::Format_ARGB32_Premultiplied);
    shown.fill(panelArt(m_look).panelColour);
    QPainter p(&shown);
    p.drawImage(0, 0, button->normal);
    p.end();
    m_sample->setPixmap(QPixmap::fromImage(shown));
}

} // namespace xpl::editor
