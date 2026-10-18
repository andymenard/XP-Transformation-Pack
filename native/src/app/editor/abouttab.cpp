// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/editor/abouttab.h"


#include "app/desktop.h"
#include "app/project.h"
#include "app/session.h"
#include "app/wizard/art.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QUrl>
#include <QClipboard>
#include <QDialog>
#include <QComboBox>
#include <QTextBrowser>
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
    auto *name = new QLabel(QStringLiteral("XPlasma Plus"));
    QFont big = name->font();
    big.setPointSizeF(big.pointSizeF() * 1.6);
    big.setBold(true);
    name->setFont(big);
    words->addWidget(name);
    words->addWidget(new QLabel(QStringLiteral("Engine revision %1").arg(project::kVersion)));
    words->addWidget(new QLabel(QStringLiteral("Modified fork of XPlasma by hotsocket (© 2026 Sock)")));
    words->addStretch(1);
    words->addWidget(wrapped(QStringLiteral("XPlasma Plus includes bundled XP assets and additional desktop integration.")));
    head->addLayout(words, 1);
    layout->addLayout(head);
    layout->addSpacing(6);

    // The licence.
    layout->addWidget(wrapped(QStringLiteral(
        "The XPlasma Plus code is free software, under the GNU General Public License, version 3 or (at your option) any later version. "
        "It comes with ABSOLUTELY NO WARRANTY.")));
    auto *legal = new QHBoxLayout;
    auto *licence = new QPushButton(QStringLiteral("&License..."));
    auto *notices = new QPushButton(QStringLiteral("&Third-Party Notices..."));
    auto *credits = new QPushButton(QStringLiteral("&Credits..."));
    legal->addWidget(credits);
    connect(credits, &QPushButton::clicked, this, [this] {
        showText(QStringLiteral("Credits"), QStringLiteral(":/licenses/CREDITS.md"));
    });
    legal->addWidget(licence);
    legal->addWidget(notices);
    legal->addStretch(1);
    {
        auto *updates = new QPushButton(QStringLiteral("&Download New Releases"));
        legal->addWidget(updates);
        connect(updates, &QPushButton::clicked, this, [] {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/andymenard/XPlasma-Plus/releases")));
        });
    }
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
        "Windows and Windows XP are trademarks of Microsoft Corporation, which does not endorse this project. "
        "This edition bundles original XP assets; no Windows XP ISO is required. Those assets retain their separate "
        "copyrights and are not covered by the code license. See the transformation pack credits and asset notices."));
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
    auto *documents = new QComboBox;
    documents->addItem(QStringLiteral("Credits"), QStringLiteral(":/licenses/CREDITS.md"));
    documents->addItem(QStringLiteral("Project license"), QStringLiteral(":/licenses/LICENSE"));
    documents->addItem(QStringLiteral("Third-party notices"), QStringLiteral(":/licenses/THIRD-PARTY-NOTICES"));
    const QDir licenses(QStringLiteral(":/licenses/LICENSES"));
    for (const auto &file : licenses.entryList({QStringLiteral("*.txt")}, QDir::Files, QDir::Name))
        documents->addItem(file, licenses.filePath(file));
    layout->addWidget(documents);
    auto *text = new QTextBrowser;
    text->setOpenExternalLinks(true);
    const auto display = [text, documents] {
        const auto path = documents->currentData().toString();
        QFile f(path);
        const auto content = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll())
                             : QStringLiteral("This document is missing from this build.");
        if (path.endsWith(QStringLiteral(".md"))) text->setMarkdown(content);
        else text->setPlainText(content);
    };
    connect(documents, &QComboBox::currentIndexChanged, this, [display](int) { display(); });
    documents->setCurrentIndex(documents->findData(resource));
    display();
    layout->addWidget(text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    layout->addWidget(buttons);
    const QFontMetrics fm(text->font());
    d.resize(fm.horizontalAdvance(QLatin1Char('M')) * 84, fm.lineSpacing() * 32);
    d.exec();
}

} // namespace xpl::editor
