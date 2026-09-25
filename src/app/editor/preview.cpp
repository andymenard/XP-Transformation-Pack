#include "app/editor/preview.h"

#include "app/media.h"
#include "app/panel.h"
#include "app/paths.h"
#include "app/qttarget.h"
#include "app/startbutton.h"
#include "core/themefile.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QStyleFactory>
#include <QVBoxLayout>

namespace xpl::preview {

namespace {

// XP's monitor: its screen, measured against the bezel (a rectangle, no
// rounding), and the magenta it keys out.
constexpr QRect kMonitorScreen{15, 17, 153, 114};

} // namespace

Previewer::Previewer()
{
    // The style plugin: the one this XPlasma ships, and wherever it's installed.
    const QString shipped = paths::shippedStylePlugin(6);
    if (!shipped.isEmpty())
        QCoreApplication::addLibraryPath(QFileInfo(shipped).dir().absolutePath() + QLatin1String("/.."));
    QCoreApplication::addLibraryPath(paths::stylePluginRoot(6));
}

bool Previewer::styleAvailable() const
{
    return QStyleFactory::keys().contains(QLatin1String(qtstyle::kStyleKey), Qt::CaseInsensitive);
}

QStyle *Previewer::styleFor(const Look &look)
{
    // A look is known by what it draws from: the style and variant, or the
    // Classic scheme's contents (which Advanced edits).
    QString spec;
    if (look.classic()) {
        ThemeFile file;
        look.system.writeTo(file);
        const QByteArray bytes = file.toBytes();
        spec = m_scratch.filePath(QString::number(qHash(bytes), 16) + QLatin1String(".theme"));
        if (!QFileInfo::exists(spec)) {
            QFile f(spec);
            if (!f.open(QIODevice::WriteOnly))
                return nullptr;
            f.write(bytes);
        }
    } else {
        spec = look.stylePath + QLatin1Char(':') + look.variant;
    }
    if (QStyle *s = m_styles.value(spec))
        return s;
    // The plugin reads XPLASMA_THEME when it's made (and then doesn't watch
    // xplasmarc): one instance per look.
    const QByteArray old = qgetenv("XPLASMA_THEME");
    qputenv("XPLASMA_THEME", QFile::encodeName(spec));
    QStyle *style = QStyleFactory::create(QLatin1String(qtstyle::kStyleKey));
    if (old.isNull())
        qunsetenv("XPLASMA_THEME");
    else
        qputenv("XPLASMA_THEME", old);
    if (style)
        m_styles.insert(spec, style);
    return style;
}

QImage Previewer::windows(const Look &look, QSize size, const QImage &behind)
{
    QStyle *style = styleFor(look);
    QImage out = behind.isNull() ? QImage(size, QImage::Format_ARGB32_Premultiplied) : behind;
    if (behind.isNull())
        out.fill(look.system.colour(u"Background"));
    if (!style)
        return out;

    // XP's sample: an inactive window, an active one, a message box.
    QMdiArea area;
    area.setStyle(style);
    area.setPalette(style->standardPalette());
    area.setBackground(Qt::transparent);
    area.setFrameShape(QFrame::NoFrame);
    area.resize(size);
    auto window = [&](const QString &title, QWidget *content, const QRect &geometry) {
        auto *sub = new QMdiSubWindow;
        sub->setWidget(content);
        sub->setWindowTitle(title);
        sub->setWindowFlags(Qt::SubWindow | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint);
        area.addSubWindow(sub);
        QList<QWidget *> widgets = sub->findChildren<QWidget *>();
        widgets << sub;
        for (QWidget *w : std::as_const(widgets)) {
            w->setStyle(style);
            w->setPalette(style->standardPalette());
        }
        sub->setGeometry(geometry);
        sub->show();
        return sub;
    };
    auto text = [](const QString &s) {
        auto *edit = new QPlainTextEdit(s);
        edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        return edit;
    };
    const int w = size.width(), h = size.height();
    window(QStringLiteral("Inactive Window"), text(QString()), QRect(8, 8, w - 60, h - 40));
    QMdiSubWindow *active = window(QStringLiteral("Active Window"), text(QStringLiteral("Window Text")), QRect(18, 34, w - 40, h - 44));
    auto *box = new QWidget;
    auto *layout = new QVBoxLayout(box);
    layout->addWidget(new QLabel(QStringLiteral("Message Text")));
    auto *ok = new QPushButton(QStringLiteral("OK"));
    ok->setDefault(true);
    layout->addWidget(ok, 0, Qt::AlignHCenter);
    QMdiSubWindow *message = window(QStringLiteral("Message Box"), box, QRect(w / 2 - 70, h / 2 - 20, 150, 90));
    message->setWindowFlags(Qt::SubWindow | Qt::WindowTitleHint | Qt::WindowSystemMenuHint);
    message->setGeometry(QRect(w / 2 - 70, h / 2 - 10, 150, 86));
    message->show();
    area.setAttribute(Qt::WA_DontShowOnScreen);
    area.show();
    QCoreApplication::processEvents();
    area.setActiveSubWindow(active);
    message->raise();
    // (The style polishes an MDI area to XP's AppWorkspace colour: here the
    // windows sit on the desktop instead.)
    area.setBackground(Qt::NoBrush);
    QCoreApplication::processEvents();
    QPainter p(&out);
    area.render(&p, QPoint(), QRegion(), QWidget::DrawChildren);
    return out;
}

QImage Previewer::appearance(const Look &look, QSize size, const QColor &background)
{
    QImage behind(size, QImage::Format_ARGB32_Premultiplied);
    behind.fill(background);
    return windows(look, size, behind);
}

QImage placeWallpaper(const QImage &image, int fill, const QColor &colour, QSize size)
{
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(colour);
    if (image.isNull())
        return out;
    // (The sample is the screen scaled down: a centred image keeps its
    // proportion of the screen, as it would on the desktop.)
    const QSize screen(1024, 768);
    const double scale = double(size.width()) / screen.width();
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    switch (fill) {
    case 0:   // stretch
        p.drawImage(out.rect(), image);
        break;
    case 1:   // fit
    case 2: { // crop
        const QSize fitted = image.size().scaled(size, fill == 1 ? Qt::KeepAspectRatio : Qt::KeepAspectRatioByExpanding);
        p.drawImage(QRect(QPoint((size.width() - fitted.width()) / 2, (size.height() - fitted.height()) / 2), fitted), image);
        break;
    }
    case 3: { // tile
        const QImage tile = image.scaled(image.size() * scale, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        for (int y = 0; y < size.height(); y += qMax(1, tile.height()))
            for (int x = 0; x < size.width(); x += qMax(1, tile.width()))
                p.drawImage(x, y, tile);
        break;
    }
    default: { // centred
        const QSize scaled = image.size() * scale;
        p.drawImage(QRect(QPoint((size.width() - scaled.width()) / 2, (size.height() - scaled.height()) / 2), scaled), image);
        break;
    }
    }
    return out;
}

QImage Previewer::desktop(const Wallpaper &wallpaper, QSize size)
{
    if (!m_monitorTried) {
        m_monitorTried = true;
        if (const auto bitmap = media::bitmap(QStringLiteral("themeui.dll"), 126)) {
            m_monitor = bitmap->convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < m_monitor.height(); ++y)
                for (int x = 0; x < m_monitor.width(); ++x)
                    if ((m_monitor.pixel(x, y) & 0xffffff) == 0xff00ff)
                        m_monitor.setPixel(x, y, 0);
        }
    }
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    const QImage image = wallpaper.image.isEmpty() ? QImage() : QImageReader(wallpaper.image).read();
    if (m_monitor.isNull()) {   // (without the XP media: just the screen)
        p.drawImage(0, 0, placeWallpaper(image, wallpaper.fill.value_or(0), wallpaper.colour, size));
        return out;
    }
    // XP's monitor, with the wallpaper in its screen.
    const QPoint at((size.width() - m_monitor.width()) / 2, (size.height() - m_monitor.height()) / 2);
    p.drawImage(at, m_monitor);
    p.drawImage(kMonitorScreen.translated(at).topLeft(), placeWallpaper(image, wallpaper.fill.value_or(0), wallpaper.colour, kMonitorScreen.size()));
    return out;
}

QImage Previewer::theme(const Look &look, const Wallpaper &desktop, QSize size, const StartButtonExtras &start)
{
    // The screen scaled down: the wallpaper, the windows, the taskbar with
    // the Start button along the bottom.
    const PanelArt art = panelArt(look);
    const int barHeight = qMax(12, art.height * size.width() / 800);
    const QSize screen(size.width(), size.height() - barHeight);
    const QImage image = desktop.image.isEmpty() ? QImage() : QImageReader(desktop.image).read();
    QImage full(size, QImage::Format_ARGB32_Premultiplied);
    full.fill(Qt::black);
    QPainter p(&full);
    p.drawImage(0, 0, windows(look, screen, placeWallpaper(image, desktop.fill.value_or(0), desktop.colour, screen)));
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRect(0, screen.height(), size.width(), barHeight), composeFrame(art.panel, QString(), QSize(800, art.height)));
    const auto flag = look.classic() ? media::classicStartFlag() : media::startFlag();
    if (flag)
        if (const auto button = startButton(look, *flag, start))
            p.drawImage(QRect(0, screen.height(), button->normal.width() * barHeight / button->normal.height(), barHeight), button->normal);
    return full;
}

} // namespace xpl::preview
