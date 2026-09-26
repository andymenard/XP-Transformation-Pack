// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// XPlasma Properties' About tab: what XPlasma is (its name, release number
// and copyright), its licence and the third-party notices (each viewable,
// from the program itself), Check for Updates (when project.ini names where
// to look), where to report a problem, this computer as a
// bug report wants it (desktop::diagnostics, with Copy), and Microsoft's
// trademarks disclaimed.
#include <QWidget>

class QLabel;
class QPlainTextEdit;

namespace xpl::editor {

class AboutTab : public QWidget
{
    Q_OBJECT
public:
    explicit AboutTab(QWidget *parent = nullptr);

    // Its paragraphs wrapped to `width`, the page's (they never widen the
    // dialog: the other tabs set its width).
    void fitWidth(int width);

protected:
    // (this computer's details worked out when first shown, not at start)
    void showEvent(QShowEvent *event) override;

private:
    void showText(const QString &title, const QString &resource);
    QLabel *wrapped(const QString &text);

    QPlainTextEdit *m_details = nullptr;
    QList<QLabel *> m_paragraphs;
};

} // namespace xpl::editor
