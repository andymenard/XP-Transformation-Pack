#pragma once
// The layout INI inside a classic (XP-era) .msstyles, and uxtheme's lookup
// rules over it.
//
// Section headers are [Sub::Class.Part(State)], every piece but Class
// optional: [Button], [Button.PushButton], [Button.PushButton(Hot)],
// [Toolbar(Pressed)], [TaskBand::Toolbar.Button]. Everything is stored
// lower-cased; values keep their spelling.

#include <QHash>
#include <QString>

#include <optional>

namespace xpl {

struct SectionKey
{
    QString sub, cls, part, state;   // lower-case; empty when absent
    bool operator==(const SectionKey &o) const
    {
        return sub == o.sub && cls == o.cls && part == o.part && state == o.state;
    }
    QString toString() const;        // canonical "sub::cls.part(state)"
};
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
size_t qHash(const SectionKey &key, size_t seed = 0);
#else
uint qHash(const SectionKey &key, uint seed = 0);
#endif

// UTF-16LE resource text, or latin-1 when it isn't valid UTF-16 (odd length,
// unpaired surrogates) -- the same fallback as xp2plasma's parser.
QString decodeText(const QByteArray &utf16);

using Properties = QHash<QString, QString>;   // lower-case key -> raw value

class Ini
{
public:
    // UTF-16LE text as stored in the TEXTFILE resource (latin-1 if it isn't
    // valid UTF-16, as xp2plasma's parser does). Tolerant: duplicate sections
    // merge, duplicate keys take the last value, ';' starts a comment.
    static Ini parse(const QByteArray &utf16);
    static Ini parseText(const QString &text);
    static SectionKey parseHeader(const QString &header);

    const QHash<SectionKey, Properties> &sections() const { return m_sections; }
    const Properties *section(const SectionKey &key) const;

    // uxtheme's property lookup: a Sub::Class's own (part, state), (part, 0)
    //   and (0, 0) first (unlike Wine's uxtheme -- checked against XP); then
    //   (part, state), (part, 0), (0, 0), each tried in Class, then Globals.
    // A class-level state section [Class(State)] is only ever the (0, state)
    // step, i.e. it applies when drawing the class's part 0.
    std::optional<QString> lookup(QStringView sub, QStringView cls, QStringView part,
                                  QStringView state, QStringView property) const;

private:
    QHash<SectionKey, Properties> m_sections;
};

} // namespace xpl
