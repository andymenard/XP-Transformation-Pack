// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Setup, from the editor: what's missing on this machine, and getting XP's
// media and the official theme packs (the same steps as `xplasma setup`).
#include <QDialog>

class QLabel;

namespace xpl::editor {

class SetupDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SetupDialog(QWidget *parent = nullptr);
    // Whether anything was added (the library wants reloading).
    bool changedLibrary() const { return m_changed; }

private:
    void refresh();
    bool m_changed = false;
    QLabel *m_checks;
};

} // namespace xpl::editor
