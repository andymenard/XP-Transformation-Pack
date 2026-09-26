// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Writing generated packages: the files, the SVGs Plasma reads art from, and
// the marker that tells XPlasma's own packages from anyone else's.
#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QSize>
#include <QString>

namespace xpl {

// Every package XPlasma generates says so, in its metadata.json (as a key)
// and its INI files (as a comment-free group), so reset only ever removes
// XPlasma's own.
constexpr const char *kGeneratedKey = "X-XPlasma-Generated";

// An SVG of named bitmap elements, the way KSvg / FrameSvg / Aurorae want
// them: each element an <image> (PNG) with its id; KSvg finds an element by id
// and uses its own box, so where it sits in the document doesn't matter.
class SvgSheet
{
public:
    void image(const QString &id, const QImage &image);
    // An invisible element of `size`: FrameSvg's hint-* elements.
    void hint(const QString &id, QSize size = QSize(1, 1));
    bool isEmpty() const { return m_elements.isEmpty(); }
    QByteArray document() const;

private:
    QList<QByteArray> m_elements;
    int m_width = 0;   // elements are laid side by side
    int m_height = 0;
};

class Package
{
public:
    explicit Package(const QString &directory);   // emptied first

    QString directory() const { return m_directory; }
    bool write(const QString &relative, const QByteArray &bytes);
    bool write(const QString &relative, const QImage &png);
    bool writeJson(const QString &relative, const QJsonObject &json);
    // Everything written so far, or the first error.
    bool ok() const { return m_error.isEmpty(); }
    QString error() const { return m_error; }

private:
    QString m_directory;
    QString m_error;
};

// KPlugin metadata.json for a package XPlasma made.
QJsonObject pluginMetadata(const QString &id, const QString &name, const QString &description);

} // namespace xpl
