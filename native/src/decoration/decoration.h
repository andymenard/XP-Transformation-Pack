// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// XPlasma's KWin window decoration: XP's caption, frame and caption buttons,
// drawn natively from the look XPlasma has applied.
//
// What it replaces, an Aurorae theme XPlasma generates for each look, made
// KWin rebuild every window's decoration from SVG on each apply (a pause of
// the whole desktop) and on every resize (8-15 times Breeze's work, measured
// in a virtual KWin). This draws the same parts with the core's renderer, as
// the Qt style does, at each window's real size -- XP's own stretching and
// tiling, not a fixed picture stretched -- and follows xplasmarc itself, so
// a new look or new colours is a repaint, not a new theme for KWin to load.

#include "core/classic.h"
#include "core/render.h"

#include <KDecoration3/Decoration>
#include <KDecoration3/DecorationButton>

#include <QCache>
#include <QFont>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QVariant>

#include <functional>
#include <memory>
#include <optional>

class QFileSystemWatcher;
class QTimer;

namespace xpd {

// The applied look, shared by every window's decoration in the compositor,
// and the parts rendered from it. Reloaded when xplasmarc names another.
class Look : public QObject
{
    Q_OBJECT

public:
    static Look &instance();

    bool classic() const { return !m_renderer; }
    const xpl::Renderer *renderer() const { return m_renderer.get(); }
    const xpl::ClassicScheme &system() const { return m_system; }

    // XP's geometry: SM_CXSIZEFRAME, SM_CYCAPTION, the caption buttons.
    int frame() const { return m_frame; }
    int caption() const { return m_caption; }
    QSize button() const { return m_button; }
    QFont captionFont() const { return m_font; }

    // WP_* `part` in `state` at `size` (device-independent), for `scale`;
    // shared between windows, and kept while it fits the budget.
    QImage part(int part, int state, QSize size, qreal scale) const;
    // Windows Classic's frame, cut as the parts are: the caption band, a
    // side, the bottom, or a maximized caption, at `length` (a width, or a
    // side's height), for `scale`.
    enum class Piece { Top = -1, Left = -2, Right = -3, Bottom = -4, Maximized = -5 };
    QImage classicPiece(Piece piece, bool active, int length, qreal scale) const;
    bool hasPart(int part, int state) const;
    std::optional<QString> captionProperty(bool active, QStringView name) const;

Q_SIGNALS:
    void changed();

private:
    Look();
    void load();
    void reloadIfChanged();

    std::unique_ptr<xpl::Renderer> m_renderer;
    xpl::ClassicScheme m_system;
    int m_frame = 4, m_caption = 19;
    QSize m_button{14, 14};
    QFont m_font;
    std::pair<QString, QString> m_loaded;   // (Path, Variant)
    QString m_rc;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_reload = nullptr;

    struct Key
    {
        int part, state, width, height, scale100;
        bool operator==(const Key &o) const
        {
            return part == o.part && state == o.state && width == o.width && height == o.height && scale100 == o.scale100;
        }
        friend size_t qHash(const Key &k, size_t seed = 0)
        {
            return ::qHash((quint64(k.part * 16 + k.state) << 48) ^ (quint64(k.width) << 28) ^ (quint64(k.height) << 8)
                               ^ quint64(k.scale100),
                           seed);
        }
    };
    mutable QCache<Key, QImage> m_parts;
};

class Decoration : public KDecoration3::Decoration
{
    Q_OBJECT

public:
    explicit Decoration(QObject *parent = nullptr, const QVariantList &args = QVariantList());

    bool init() override;
    void paint(QPainter *painter, const QRectF &repaintArea) override;

    bool maximized() const;

private:
    void relayout();
    void paintStyle(QPainter *painter, bool active);
    void paintClassic(QPainter *painter, bool active);
    void drawEdges(QPainter *painter, bool active, const std::function<QImage(int, int, int)> &piece);
    void paintCaption(QPainter *painter, bool active);

    QList<QPointer<KDecoration3::DecorationButton>> m_buttons;   // the menu (icon), then right to left
    QRectF m_label;
};

class Button : public KDecoration3::DecorationButton
{
    Q_OBJECT

public:
    Button(KDecoration3::DecorationButtonType type, Decoration *decoration, QObject *parent = nullptr);
    // (How KDecoration's previews make buttons through the plugin factory.)
    Button(QObject *parent, const QVariantList &args);

    void paint(QPainter *painter, const QRectF &repaintArea) override;
};

} // namespace xpd
