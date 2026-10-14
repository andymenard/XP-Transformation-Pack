// SPDX-License-Identifier: GPL-3.0-or-later
#include "ini.h"

#include <QHashFunctions>

namespace xpl {

QString SectionKey::toString() const
{
    QString out;
    if (!sub.isEmpty())
        out += sub + QLatin1String("::");
    out += cls;
    if (!part.isEmpty())
        out += QLatin1Char('.') + part;
    if (!state.isEmpty())
        out += QLatin1Char('(') + state + QLatin1Char(')');
    return out;
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
size_t qHash(const SectionKey &key, size_t seed)
#else
uint qHash(const SectionKey &key, uint seed)
#endif
{
    // Qt 5 has no qHashMulti; this is its combining step.
    auto h = seed;
    for (const QString *part : {&key.sub, &key.cls, &key.part, &key.state})
        h ^= ::qHash(*part) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

SectionKey Ini::parseHeader(const QString &header)
{
    SectionKey key;
    QString rest = header.trimmed();
    // A trailing "(State)" belongs to the part, or to the class if there's no part.
    if (rest.endsWith(QLatin1Char(')'))) {
        const int open = rest.lastIndexOf(QLatin1Char('('));
        if (open > 0) {
            key.state = rest.mid(open + 1, rest.size() - open - 2).trimmed().toLower();
            rest = rest.left(open).trimmed();
        }
    }
    const int scope = rest.indexOf(QLatin1String("::"));
    if (scope >= 0) {
        key.sub = rest.left(scope).trimmed().toLower();
        rest = rest.mid(scope + 2);
    }
    const int dot = rest.indexOf(QLatin1Char('.'));
    if (dot >= 0) {
        key.cls = rest.left(dot).trimmed().toLower();
        key.part = rest.mid(dot + 1).trimmed().toLower();
    } else {
        key.cls = rest.trimmed().toLower();
    }
    return key;
}

QString decodeText(const QByteArray &utf16)
{
    bool valid = utf16.size() % 2 == 0;
    QString text;
    if (valid) {
        text = QString::fromUtf16(reinterpret_cast<const char16_t *>(utf16.constData()), int(utf16.size() / 2));
        // An unpaired surrogate means this was never UTF-16.
        for (int i = 0; valid && i < text.size(); ++i) {
            const QChar c = text.at(i);
            if (c.isHighSurrogate())
                valid = i + 1 < text.size() && text.at(++i).isLowSurrogate();
            else if (c.isLowSurrogate())
                valid = false;
        }
    }
    return valid ? text : QString::fromLatin1(utf16);
}

Ini Ini::parse(const QByteArray &utf16)
{
    return parseText(decodeText(utf16));
}

Ini Ini::parseText(const QString &text)
{
    Ini ini;
    Properties *current = nullptr;
    const QStringView all(text);
    for (qsizetype start = 0; start <= all.size();) {
        qsizetype end = all.indexOf(QLatin1Char('\n'), start);
        if (end < 0)
            end = all.size();
        QStringView line = all.mid(start, end - start);
        start = end + 1;
        // Comments first, as uxtheme does: a header can carry one too
        // ("[ExplorerBar.HeaderBackground]; Not used in IE 6" -- 25 such
        // headers in Royale alone). Read as a key line instead, its section's
        // keys would all land in the previous section. Values never contain
        // ';'. Found by the XP oracle; xp2plasma's parser had the same bug.
        const qsizetype comment = line.indexOf(QLatin1Char(';'));
        if (comment >= 0)
            line = line.left(comment);
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')) && line.size() > 2) {
            current = &ini.m_sections[parseHeader(line.mid(1, line.size() - 2).toString())];
            continue;
        }
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (!current || eq < 0)
            continue;
        current->insert(line.left(eq).trimmed().toString().toLower(), line.mid(eq + 1).trimmed().toString());
    }
    return ini;
}

const Properties *Ini::section(const SectionKey &key) const
{
    auto it = m_sections.constFind(key);
    return it == m_sections.cend() ? nullptr : &it.value();
}

std::optional<QString> Ini::lookup(QStringView sub, QStringView cls, QStringView part,
                                   QStringView state, QStringView property) const
{
    const QString prop = property.toString().toLower();
    const QString c = cls.toString().toLower();
    const QString s = sub.toString().toLower();
    auto find = [&](const SectionKey &key) -> std::optional<QString> {
        if (const Properties *props = section(key)) {
            auto it = props->constFind(prop);
            if (it != props->cend())
                return it.value();
        }
        return std::nullopt;
    };
    // A Sub::Class is complete before the class it overrides: its (part,
    // state), (part, 0) and (0, 0) all come first. XP draws Start::Button's
    // push button (part 1) from [Start::Button], not [Button.PushButton]
    // (oracle: SetWindowTheme(hwnd, "Start") then OpenThemeData "Button").
    if (!s.isEmpty()) {
        QString p = part.toString().toLower(), st = state.toString().toLower();
        for (;;) {
            if (auto v = find({s, c, p, st}))
                return v;
            if (!st.isEmpty())
                st.clear();
            else if (!p.isEmpty())
                p.clear();
            else
                break;
        }
    }
    QString p = part.toString().toLower();
    QString st = state.toString().toLower();
    for (;;) {
        // Then the class, then Globals.
        for (const SectionKey &key : {SectionKey{QString(), c, p, st}, SectionKey{QString(), QStringLiteral("globals"), p, st}})
            if (auto v = find(key))
                return v;
        if (!st.isEmpty())
            st.clear();
        else if (!p.isEmpty())
            p.clear();
        else
            return std::nullopt;
    }
}

} // namespace xpl
