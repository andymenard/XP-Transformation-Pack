#pragma once
// XP's Advanced Appearance dialog, for Windows Classic: an item (Desktop,
// Active Title Bar, Menu, ...), and the size, colours and font XP lets that
// item have, edited on the scheme with the sample following.
#include "app/editor/preview.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QCheckBox;

namespace xpl::editor {

class ColourButton;

class AdvancedDialog : public QDialog
{
    Q_OBJECT
public:
    AdvancedDialog(const ClassicScheme &scheme, preview::Previewer *previewer, QWidget *parent = nullptr);
    ClassicScheme scheme() const { return m_scheme; }

private:
    void showItem();
    void changed();
    void render();

    ClassicScheme m_scheme;
    preview::Previewer *m_previewer;
    bool m_loading = false;
    QLabel *m_sample;
    QComboBox *m_item, *m_fontFace;
    QSpinBox *m_size, *m_fontSize;
    ColourButton *m_colour1, *m_colour2, *m_fontColour;
    QCheckBox *m_bold, *m_italic;
};

// A colour swatch that opens a colour picker.
class ColourButton : public QWidget
{
    Q_OBJECT
public:
    explicit ColourButton(QWidget *parent = nullptr);
    QColor colour() const { return m_colour; }
    void setColour(const QColor &colour);
    QSize sizeHint() const override;

Q_SIGNALS:
    void picked();

private:
    QPushButton *m_button;
    QColor m_colour;
};

} // namespace xpl::editor
