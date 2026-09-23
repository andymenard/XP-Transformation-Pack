#include "xplasmastyle.h"

#include <QPainter>
#include <QStyleFactory>
#include <QStyleOption>

XPLasmaStyle::XPLasmaStyle()
    : QProxyStyle(QStyleFactory::create(QStringLiteral("Windows")))
{
    setObjectName(QStringLiteral("XPLasma"));
    m_safe = qEnvironmentVariableIntValue("XPLASMA_SAFE") != 0;
}

void XPLasmaStyle::drawControl(ControlElement element, const QStyleOption *option,
                               QPainter *painter, const QWidget *widget) const
{
    QProxyStyle::drawControl(element, option, painter, widget);
    if (m_safe)
        return;

    // Spike 0.3: an unmistakable mark on every push button, so a screenshot
    // proves this plugin (not Qt's own Windows style) did the drawing.
    if (element == CE_PushButtonBevel) {
        painter->save();
        painter->setPen(QPen(QColor(0xE0, 0x10, 0x90), 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(option->rect.adjusted(1, 1, -2, -2));
        painter->restore();
    }
}
