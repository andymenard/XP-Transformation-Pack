#include "app/editor/advanced.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace xpl::editor {

namespace {

// XP's items: what each one's size is, its colours (by .theme name), its
// font and the font's colour. (Empty: XP greys that control out.)
struct Item
{
    const char *label;
    const char *size;        // a metric, below
    const char *colour1, *colour2;
    const char *font;        // "caption", "smcaption", "menu", "status", "message", "icon"
    const char *fontColour;
};
const Item kItems[] = {
    {"3D Objects", nullptr, "ButtonFace", nullptr, nullptr, "ButtonText"},
    {"Active Title Bar", "captionHeight", "ActiveTitle", "GradientActiveTitle", "caption", "TitleText"},
    {"Active Window Border", "borderWidth", "ActiveBorder", nullptr, nullptr, nullptr},
    {"Application Background", nullptr, "AppWorkspace", nullptr, nullptr, nullptr},
    {"Caption Buttons", "captionWidth", nullptr, nullptr, nullptr, nullptr},
    {"Desktop", nullptr, "Background", nullptr, nullptr, nullptr},
    {"Icon", nullptr, nullptr, nullptr, "icon", nullptr},
    {"Icon Spacing (Horizontal)", "iconHorizontal", nullptr, nullptr, nullptr, nullptr},
    {"Icon Spacing (Vertical)", "iconVertical", nullptr, nullptr, nullptr, nullptr},
    {"Inactive Title Bar", "captionHeight", "InactiveTitle", "GradientInactiveTitle", "caption", "InactiveTitleText"},
    {"Inactive Window Border", "borderWidth", "InactiveBorder", nullptr, nullptr, nullptr},
    {"Menu", "menuHeight", "Menu", nullptr, "menu", "MenuText"},
    {"Message Box", nullptr, nullptr, nullptr, "message", "WindowText"},
    {"Palette Title", "smCaptionHeight", nullptr, nullptr, "smcaption", nullptr},
    {"Scrollbar", "scrollWidth", nullptr, nullptr, nullptr, nullptr},
    {"Selected Items", "menuHeight", "Hilight", nullptr, "menu", "HilightText"},
    {"ToolTip", nullptr, "InfoWindow", nullptr, "status", "InfoText"},
    {"Window", nullptr, "Window", nullptr, nullptr, "WindowText"},
};

int *itemMetric(ClassicScheme &s, const char *name)
{
    NonClientMetrics &m = s.metrics;
    const QLatin1String n(name ? name : "");
    if (n == QLatin1String("captionHeight")) return &m.captionHeight;
    if (n == QLatin1String("captionWidth")) return &m.captionWidth;
    if (n == QLatin1String("borderWidth")) return &m.borderWidth;
    if (n == QLatin1String("menuHeight")) return &m.menuHeight;
    if (n == QLatin1String("smCaptionHeight")) return &m.smCaptionHeight;
    if (n == QLatin1String("scrollWidth")) return &m.scrollWidth;
    if (n == QLatin1String("iconHorizontal")) return &s.icons.horzSpacing;
    if (n == QLatin1String("iconVertical")) return &s.icons.vertSpacing;
    return nullptr;
}

LogFont *itemFont(ClassicScheme &s, const char *name)
{
    const QLatin1String n(name ? name : "");
    if (n == QLatin1String("caption")) return &s.metrics.captionFont;
    if (n == QLatin1String("smcaption")) return &s.metrics.smCaptionFont;
    if (n == QLatin1String("menu")) return &s.metrics.menuFont;
    if (n == QLatin1String("status")) return &s.metrics.statusFont;
    if (n == QLatin1String("message")) return &s.metrics.messageFont;
    if (n == QLatin1String("icon")) return &s.icons.font;
    return nullptr;
}

} // namespace

// --- the colour button -----------------------------------------------------------------

ColourButton::ColourButton(QWidget *parent)
    : QWidget(parent)
    , m_button(new QPushButton(this))
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_button);
    m_button->setFixedWidth(46);
    connect(m_button, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_colour, this);
        if (c.isValid()) {
            setColour(c);
            Q_EMIT picked();
        }
    });
    setColour(Qt::black);
}

void ColourButton::setColour(const QColor &colour)
{
    m_colour = colour;
    QPixmap swatch(28, 12);
    swatch.fill(colour);
    QPainter p(&swatch);
    p.setPen(Qt::black);
    p.drawRect(swatch.rect().adjusted(0, 0, -1, -1));
    p.end();
    m_button->setIcon(QIcon(swatch));
    m_button->setIconSize(swatch.size());
}

QSize ColourButton::sizeHint() const
{
    return m_button->sizeHint();
}

// --- the dialog ------------------------------------------------------------------------

AdvancedDialog::AdvancedDialog(const ClassicScheme &scheme, preview::Previewer *previewer, QWidget *parent)
    : QDialog(parent)
    , m_scheme(scheme)
    , m_previewer(previewer)
{
    setWindowTitle(QStringLiteral("Advanced Appearance"));
    auto *layout = new QVBoxLayout(this);
    m_sample = new QLabel;
    m_sample->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    m_sample->setFixedSize(preview::kAppearanceSize + QSize(4, 4));
    m_sample->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_sample, 0, Qt::AlignHCenter);
    auto *blurb = new QLabel(QStringLiteral("If you select a windows and buttons setting other than Windows Classic, it will "
                                            "override the following settings, except in some older programs."));
    blurb->setWordWrap(true);
    layout->addWidget(blurb);

    auto *grid = new QGridLayout;
    m_item = new QComboBox;
    for (const Item &item : kItems)
        m_item->addItem(QLatin1String(item.label));
    m_size = new QSpinBox;
    m_size->setRange(0, 200);
    m_colour1 = new ColourButton;
    m_colour2 = new ColourButton;
    grid->addWidget(new QLabel(QStringLiteral("Item:")), 0, 0);
    grid->addWidget(new QLabel(QStringLiteral("Size:")), 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("Color 1:")), 0, 2);
    grid->addWidget(new QLabel(QStringLiteral("Color 2:")), 0, 3);
    grid->addWidget(m_item, 1, 0);
    grid->addWidget(m_size, 1, 1);
    grid->addWidget(m_colour1, 1, 2);
    grid->addWidget(m_colour2, 1, 3);
    m_fontFace = new QComboBox;
    m_fontFace->addItems(QFontDatabase::families());
    m_fontSize = new QSpinBox;
    m_fontSize->setRange(6, 36);
    m_fontColour = new ColourButton;
    m_bold = new QCheckBox(QStringLiteral("B"));
    m_italic = new QCheckBox(QStringLiteral("I"));
    grid->addWidget(new QLabel(QStringLiteral("Font:")), 2, 0);
    grid->addWidget(new QLabel(QStringLiteral("Size:")), 2, 1);
    grid->addWidget(new QLabel(QStringLiteral("Color:")), 2, 2);
    grid->addWidget(m_fontFace, 3, 0);
    grid->addWidget(m_fontSize, 3, 1);
    grid->addWidget(m_fontColour, 3, 2);
    auto *styleRow = new QHBoxLayout;
    styleRow->addWidget(m_bold);
    styleRow->addWidget(m_italic);
    grid->addLayout(styleRow, 3, 3);
    grid->setColumnStretch(0, 1);
    layout->addLayout(grid);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_item, &QComboBox::currentIndexChanged, this, &AdvancedDialog::showItem);
    connect(m_size, &QSpinBox::valueChanged, this, &AdvancedDialog::changed);
    connect(m_fontSize, &QSpinBox::valueChanged, this, &AdvancedDialog::changed);
    connect(m_fontFace, &QComboBox::currentTextChanged, this, &AdvancedDialog::changed);
    for (ColourButton *b : {m_colour1, m_colour2, m_fontColour})
        connect(b, &ColourButton::picked, this, &AdvancedDialog::changed);
    for (QCheckBox *b : {m_bold, m_italic})
        connect(b, &QCheckBox::toggled, this, &AdvancedDialog::changed);
    m_item->setCurrentIndex(5);   // Desktop, as XP opens on it
    showItem();
    render();
}

void AdvancedDialog::showItem()
{
    const Item &item = kItems[m_item->currentIndex()];
    m_loading = true;
    int *size = itemMetric(m_scheme, item.size);
    m_size->setEnabled(size);
    m_size->setValue(size ? *size : 0);
    m_colour1->setEnabled(item.colour1);
    m_colour1->setColour(item.colour1 ? m_scheme.colour(QString::fromLatin1(item.colour1)) : QColor(Qt::gray));
    m_colour2->setEnabled(item.colour2);
    m_colour2->setColour(item.colour2 ? m_scheme.colour(QString::fromLatin1(item.colour2)) : QColor(Qt::gray));
    LogFont *f = itemFont(m_scheme, item.font);
    for (QWidget *w : std::initializer_list<QWidget *>{m_fontFace, m_fontSize, m_bold, m_italic})
        w->setEnabled(f);
    if (f) {
        m_fontFace->setCurrentText(f->face);
        m_fontSize->setValue(f->points());
        m_bold->setChecked(f->bold());
        m_italic->setChecked(f->italic);
    }
    m_fontColour->setEnabled(item.fontColour);
    m_fontColour->setColour(item.fontColour ? m_scheme.colour(QString::fromLatin1(item.fontColour)) : QColor(Qt::gray));
    m_loading = false;
}

void AdvancedDialog::changed()
{
    if (m_loading)
        return;
    const Item &item = kItems[m_item->currentIndex()];
    if (int *size = itemMetric(m_scheme, item.size))
        *size = m_size->value();
    if (item.colour1)
        m_scheme.setColour(QString::fromLatin1(item.colour1), m_colour1->colour());
    if (item.colour2)
        m_scheme.setColour(QString::fromLatin1(item.colour2), m_colour2->colour());
    if (item.fontColour)
        m_scheme.setColour(QString::fromLatin1(item.fontColour), m_fontColour->colour());
    if (LogFont *f = itemFont(m_scheme, item.font)) {
        const bool italic = m_italic->isChecked();
        *f = LogFont::fromPoints(m_fontFace->currentText(), m_fontSize->value(), m_bold->isChecked());
        f->italic = italic;
    }
    // The scheme is the user's own now.
    if (!m_scheme.name.endsWith(QLatin1String(" (modified)")))
        m_scheme.name += QLatin1String(" (modified)");
    render();
}

void AdvancedDialog::render()
{
    const QImage image = m_previewer->appearance(Look::fromScheme(m_scheme), preview::kAppearanceSize, m_scheme.colour(u"Background"));
    m_sample->setPixmap(QPixmap::fromImage(image));
}

} // namespace xpl::editor
