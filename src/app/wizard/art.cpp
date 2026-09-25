#include "app/wizard/art.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

namespace xpl::wizard {

namespace {

// A product box, drawn in three faces: a window on its front (four panes,
// sky and hill), its lid and side in the shades of the front.
void drawBox(QPainter &p, const QRectF &r)
{
    const qreal d = r.width() * 0.22;   // how far back the box goes
    const QRectF front(r.left(), r.top() + d, r.width() - d, r.height() - d);
    QPainterPath lid;
    lid.moveTo(front.topLeft());
    lid.lineTo(front.left() + d, r.top());
    lid.lineTo(r.right(), r.top());
    lid.lineTo(front.right(), front.top());
    lid.closeSubpath();
    QPainterPath side;
    side.moveTo(front.topRight());
    side.lineTo(r.right(), r.top());
    side.lineTo(r.right(), r.bottom() - d);
    side.lineTo(front.bottomRight());
    side.closeSubpath();
    p.setPen(QPen(QColor(10, 20, 60), 1));
    p.setBrush(QColor(214, 222, 240));
    p.drawPath(lid);
    p.setBrush(QColor(120, 140, 190));
    p.drawPath(side);
    QLinearGradient face(front.topLeft(), front.bottomRight());
    face.setColorAt(0, QColor(250, 250, 255));
    face.setColorAt(1, QColor(176, 192, 226));
    p.setBrush(face);
    p.drawRect(front);
    // The window on it: a frame, a title bar, and a little landscape.
    const QRectF win = front.adjusted(front.width() * 0.16, front.height() * 0.18, -front.width() * 0.16, -front.height() * 0.18);
    p.setBrush(QColor(30, 70, 190));
    p.drawRect(win);
    const QRectF pane = win.adjusted(win.width() * 0.08, win.height() * 0.22, -win.width() * 0.08, -win.width() * 0.08);
    QLinearGradient sky(pane.topLeft(), pane.bottomLeft());
    sky.setColorAt(0, QColor(90, 150, 240));
    sky.setColorAt(1, QColor(200, 225, 255));
    p.setPen(Qt::NoPen);
    p.setBrush(sky);
    p.drawRect(pane);
    QPainterPath hill;
    hill.moveTo(pane.left(), pane.bottom());
    hill.quadTo(pane.center().x(), pane.top() + pane.height() * 0.35, pane.right(), pane.top() + pane.height() * 0.7);
    hill.lineTo(pane.bottomRight());
    hill.closeSubpath();
    p.setBrush(QColor(70, 160, 60));
    p.drawPath(hill);
}

} // namespace

QPixmap sidePicture()
{
    const QSize size(164, 314);
    QPixmap out(size);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    // Deep blue into black.
    QLinearGradient ground(0, 0, size.width() * 0.6, size.height());
    ground.setColorAt(0, QColor(6, 24, 110));
    ground.setColorAt(0.55, QColor(4, 10, 60));
    ground.setColorAt(1, QColor(0, 0, 0));
    p.fillRect(out.rect(), ground);
    // Light sweeping across, in bands.
    for (int i = 0; i < 5; ++i) {
        QPainterPath band;
        const qreal y = 70 + i * 46;
        band.moveTo(-20, y + 60);
        band.cubicTo(40, y - 10, 110, y + 40, 190, y - 30);
        band.lineTo(190, y - 12);
        band.cubicTo(110, y + 58, 40, y + 8, -20, y + 78);
        band.closeSubpath();
        QLinearGradient shine(0, 0, size.width(), 0);
        shine.setColorAt(0, QColor(40, 90, 230, 0));
        shine.setColorAt(0.5, QColor(60, 120, 255, 70 - i * 10));
        shine.setColorAt(1, QColor(120, 170, 255, 0));
        p.fillPath(band, shine);
    }
    // A glow where the box sits.
    QRadialGradient glow(QPointF(size.width() - 52, 58), 70);
    glow.setColorAt(0, QColor(80, 130, 255, 110));
    glow.setColorAt(1, QColor(80, 130, 255, 0));
    p.fillRect(out.rect(), glow);
    // The box, in a white frame, top right as the classic art has its icon.
    const QRectF frame(size.width() - 82, 22, 62, 62);
    p.setPen(QPen(Qt::white, 3));
    p.setBrush(QColor(0, 0, 0, 160));
    p.drawRect(frame);
    drawBox(p, frame.adjusted(9, 9, -9, -9));
    return out;
}

QPixmap bannerIcon(int size)
{
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    drawBox(p, QRectF(2, 2, size - 4, size - 4));
    return out;
}

} // namespace xpl::wizard
