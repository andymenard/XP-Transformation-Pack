#pragma once

#include <QProxyStyle>

// XPLasma's Qt style.
//
// A QProxyStyle over Qt's built-in "Windows" style: this mirrors XP itself,
// where comctl32 asks uxtheme to draw a control and whatever the visual style
// doesn't cover is drawn classic. The base style's internal calls come back
// through proxy(), so sub-elements it draws on our behalf are ours too.
class XPLasmaStyle : public QProxyStyle
{
    Q_OBJECT

public:
    XPLasmaStyle();

    void drawControl(ControlElement element, const QStyleOption *option,
                     QPainter *painter, const QWidget *widget = nullptr) const override;

private:
    // XPLASMA_SAFE=1: draw nothing of our own, only the classic base style.
    // The plugin is loaded into plasmashell and every KDE dialog, so there
    // must always be a way to take it out of the picture without uninstalling.
    bool m_safe = false;
};
