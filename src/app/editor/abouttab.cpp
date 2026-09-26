// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/editor/abouttab.h"

#include "app/desktop.h"
#include "app/project.h"
#include "app/session.h"
#include "app/wizard/art.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace xpl::editor {

namespace {

} // namespace

QLabel *AboutTab::wrapped(const QString &text)
{
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    l->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_paragraphs << l;
    return l;
}

void AboutTab::fitWidth(int width)
{
    // Each paragraph's height at the width it gets: the page's, less the
    // margins of the layouts (and group box) it sits in -- measured in the
    // style's fonts and margins, not the defaults before it's polished.
    ensurePolished();
    for (QLabel *l : std::as_const(m_paragraphs)) {
        int inset = 0;
        for (QWidget *w = l->parentWidget(); w && w != parentWidget(); w = w->parentWidget())
            if (w->layout()) {
                const QMargins m = w->layout()->contentsMargins();
                inset += m.left() + m.right() + (w == this ? 0 : w->contentsMargins().left() + w->contentsMargins().right());
            }
        l->setFixedHeight(l->heightForWidth(width - inset));
    }
    // (and every layout in the page and above it told: a page the tabs hide
    // doesn't pass the change on, and the dialog would keep the size it had
    // first worked out)
    for (QWidget *w : findChildren<QWidget *>())
        w->updateGeometry();
    for (QLayout *l : findChildren<QLayout *>())
        l->invalidate();
    updateGeometry();
    for (QWidget *w = this; w; w = w->parentWidget())
        if (w->layout())
            w->layout()->invalidate();
}

AboutTab::AboutTab(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    // The product: its box, its name, its release.
    auto *head = new QHBoxLayout;
    auto *box = new QLabel;
    box->setPixmap(wizard::bannerIcon(64));
    box->setAlignment(Qt::AlignTop);
    head->addWidget(box);
    auto *words = new QVBoxLayout;
    auto *name = new QLabel(QStringLiteral("XPlasma"));
    QFont big = name->font();
    big.setPointSizeF(big.pointSizeF() * 1.6);
    big.setBold(true);
    name->setFont(big);
    words->addWidget(name);
    words->addWidget(new QLabel(QStringLiteral("Release %1").arg(project::kVersion)));
    words->addWidget(new QLabel(QStringLiteral("Copyright © 2026 Sock")));
    words->addStretch(1);
    words->addWidget(wrapped(QStringLiteral("Windows XP's looks and sounds for KDE Plasma.")));
    head->addLayout(words, 1);
    layout->addLayout(head);
    layout->addSpacing(6);

    // The licence.
    layout->addWidget(wrapped(QStringLiteral(
        "XPlasma is free software, under the GNU General Public License, version 3 or (at your option) any later version. "
        "It comes with ABSOLUTELY NO WARRANTY.")));
    auto *legal = new QHBoxLayout;
    auto *licence = new QPushButton(QStringLiteral("&License..."));
    auto *notices = new QPushButton(QStringLiteral("&Third-Party Notices..."));
    legal->addWidget(licence);
    legal->addWidget(notices);
    legal->addStretch(1);
    layout->addLayout(legal);
    connect(licence, &QPushButton::clicked, this, [this] {
        showText(QStringLiteral("GNU General Public License"), QStringLiteral(":/licenses/LICENSE"));
    });
    connect(notices, &QPushButton::clicked, this, [this] {
        showText(QStringLiteral("Third-Party Notices"), QStringLiteral(":/licenses/THIRD-PARTY-NOTICES"));
    });
    layout->addSpacing(6);

    // This computer, as a bug report wants it.
    auto *computer = new QGroupBox(QStringLiteral("This computer"));
    auto *inside = new QVBoxLayout(computer);
    const QString issues = QString::fromUtf8(project::kIssuesUrl);
    auto *report = wrapped(issues.isEmpty()
                               ? QStringLiteral("If something goes wrong, include these details when you report it.")
                               : QStringLiteral("If something goes wrong, <a href=\"%1\">report it</a> with these details.")
                                     .arg(issues.toHtmlEscaped()));
    report->setOpenExternalLinks(true);
    inside->addWidget(report);
    m_details = new QPlainTextEdit;
    m_details->setReadOnly(true);
    m_details->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_details->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_details->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_details->setMinimumHeight(QFontMetrics(m_details->font()).lineSpacing() * 5);
    inside->addWidget(m_details, 1);
    auto *copyRow = new QHBoxLayout;
    copyRow->addStretch(1);
    auto *copy = new QPushButton(QStringLiteral("&Copy"));
    copyRow->addWidget(copy);
    inside->addLayout(copyRow);
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_details->toPlainText()); });
    layout->addWidget(computer, 1);

    // Microsoft's.
    auto *marks = wrapped(QStringLiteral(
        "Windows and Windows XP are trademarks of Microsoft Corporation, which neither makes nor endorses XPlasma. XPlasma "
        "includes none of Windows' files: it takes what it uses from your own Windows XP CD image."));
    QFont small = marks->font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    marks->setFont(small);
    layout->addWidget(marks);
}

void AboutTab::showEvent(QShowEvent *event)
{
    if (m_details->toPlainText().isEmpty()) {
        LiveSession session;
        m_details->setPlainText(desktop::diagnostics(session));
    }
    QWidget::showEvent(event);
}

void AboutTab::showText(const QString &title, const QString &resource)
{
    QDialog d(this);
    d.setWindowTitle(title);
    auto *layout = new QVBoxLayout(&d);
    auto *text = new QPlainTextEdit;
    text->setReadOnly(true);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    QFile f(resource);
    text->setPlainText(f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QStringLiteral("(missing from this build)"));
    layout->addWidget(text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    layout->addWidget(buttons);
    const QFontMetrics fm(text->font());
    d.resize(fm.horizontalAdvance(QLatin1Char('M')) * 84, fm.lineSpacing() * 32);
    d.exec();
}

} // namespace xpl::editor
