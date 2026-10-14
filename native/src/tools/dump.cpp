// SPDX-License-Identifier: GPL-3.0-or-later
// xplasma-dump: what the core parsed out of a .msstyles, as canonical JSON.
//
//   xplasma-dump FILE.msstyles [--variant NAME_INI | --all-variants] [--no-bitmaps]
//
// Variant tables, every INI section (headers canonicalised to
// sub::class.part(state)), and a SHA-256 of every bitmap's decoded RGBA
// pixels: for diffing what two builds, or two styles, parse to.
#include "core/schema.h"
#include "core/theme.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

using namespace xpl;

static QJsonArray strings(const QStringList &list)
{
    QJsonArray out;
    for (const QString &s : list)
        out.append(s);
    return out;
}

static QJsonObject sections(const Ini &ini)
{
    QJsonObject out;
    for (auto it = ini.sections().cbegin(); it != ini.sections().cend(); ++it) {
        QJsonObject props;
        for (auto p = it.value().cbegin(); p != it.value().cend(); ++p)
            props.insert(p.key(), p.value());
        out.insert(it.key().toString(), props);
    }
    return out;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 2) {
        QTextStream(stderr) << "usage: xplasma-dump FILE.msstyles [--variant NAME | --all-variants] [--no-bitmaps]\n";
        return 2;
    }
    QString variant;
    const bool all = args.contains(QLatin1String("--all-variants"));
    const bool bitmaps = !args.contains(QLatin1String("--no-bitmaps"));
    const int v = int(args.indexOf(QLatin1String("--variant")));
    if (v > 0 && v + 1 < args.size())
        variant = args.at(v + 1);

    QString error;
    const auto theme = Theme::load(args.at(1), variant, &error);
    if (theme && args.contains(QLatin1String("--schema-check"))) {
        // Sections whose part or state XP's schema doesn't know. uxtheme
        // ignores those, so each is either a gap in core/schema.cpp or a
        // style's own typo. Classes outside the schema (SysMetrics,
        // Globals, documentation, ...) are skipped.
        int unknown = 0;
        for (auto it = theme->ini().sections().cbegin(); it != theme->ini().sections().cend(); ++it) {
            const SectionKey &k = it.key();
            if (k.part.isEmpty() || !schema::partName(k.cls, 1).size())
                continue;
            const bool partOk = schema::partId(k.cls, k.part) != 0;
            const bool stateOk = k.state.isEmpty() || schema::stateId(k.cls, k.part, k.state) != 0;
            if (!partOk || !stateOk) {
                QTextStream(stdout) << "unknown " << (partOk ? "state " : "part  ") << k.toString() << '\n';
                ++unknown;
            }
        }
        return unknown ? 3 : 0;
    }
    if (!theme) {
        QJsonObject fail{{QStringLiteral("error"), error}};
        QTextStream(stdout) << QJsonDocument(fail).toJson(QJsonDocument::Compact) << '\n';
        return 1;
    }
    const VariantTable &t = theme->variants();
    QJsonObject out{
        {QStringLiteral("default"), theme->variant()},
        {QStringLiteral("iniNames"), strings(t.iniNames)},
        {QStringLiteral("colourNames"), strings(t.colourNames)},
        {QStringLiteral("sizeNames"), strings(t.sizeNames)},
        {QStringLiteral("fileNames"), strings(t.fileNames)},
    };
    QJsonObject variants;
    const QStringList wanted = all ? t.iniNames : QStringList{theme->variant()};
    for (const QString &name : wanted) {
        if (name == QLatin1String("THEMES_INI"))
            continue;
        const auto one = name == theme->variant() ? theme : Theme::load(args.at(1), name, &error);
        if (one)
            variants.insert(name, sections(one->ini()));
    }
    out.insert(QStringLiteral("variants"), variants);
    if (bitmaps) {
        QJsonObject bmps;
        for (const QString &name : theme->bitmapNames()) {
            // bitmap() takes an ImageFile path; a resource name maps to itself.
            const Dib dib = theme->bitmap(name);
            QJsonObject b;
            if (dib.image.isNull()) {
                b.insert(QStringLiteral("error"), QStringLiteral("undecodable"));
            } else {
                const QImage rgba = dib.image.convertToFormat(QImage::Format_RGBA8888);
                QCryptographicHash hash(QCryptographicHash::Sha256);
                for (int y = 0; y < rgba.height(); ++y)
                    hash.addData(QByteArray::fromRawData(reinterpret_cast<const char *>(rgba.constScanLine(y)), rgba.width() * 4));
                b.insert(QStringLiteral("size"), QJsonArray{rgba.width(), rgba.height()});
                b.insert(QStringLiteral("alpha"), dib.hasAlpha);
                b.insert(QStringLiteral("sha256"), QString::fromLatin1(hash.result().toHex()));
            }
            bmps.insert(name, b);
        }
        out.insert(QStringLiteral("bitmaps"), bmps);
    }
    QTextStream(stdout) << QJsonDocument(out).toJson(QJsonDocument::Indented);
    return 0;
}
