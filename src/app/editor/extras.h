#pragma once
// Extras..., where XP's Appearance tab had Effects...: the Start button's
// label and logo, in place of XP's "start" and flag -- characters (an
// emoji, say) or a picture -- with the button drawn as it will look.
#include "app/look.h"
#include "app/startbutton.h"

#include <QDialog>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QPushButton;

namespace xpl::editor {

class ExtrasDialog : public QDialog
{
    Q_OBJECT
public:
    ExtrasDialog(const Look &look, const StartButtonExtras &extras, QWidget *parent = nullptr);
    StartButtonExtras extras() const;

private:
    void changed();
    void render();

    Look m_look;
    QString m_file;
    QLineEdit *m_label, *m_characters;
    QButtonGroup *m_logo;
    QLabel *m_fileName, *m_sample;
    QPushButton *m_browse;
};

} // namespace xpl::editor
