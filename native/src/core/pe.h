// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Read the resources out of a PE file (.msstyles are resource-only DLLs).
//
// Every read is bounds-checked against the file: this code runs inside
// plasmashell and every Qt app on the desktop, and fan-made .msstyles are
// exactly the kind of file that is subtly malformed. A bad file is an error
// string, never a crash.

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

namespace xpl {

struct Resource
{
    QString type;   // a string type ("TEXTFILE") or a decimal id ("2" is RT_BITMAP)
    QString name;   // likewise; string names are kept as written in the file
    QByteArray data;
    qint64 fileOffset = -1; // bounds-checked resource payload position
};

class PeResources
{
public:
    // The first language of every (type, name) -- the same one pefile-based
    // code (xp2plasma's msstyles_parse) takes. With `borrow`, each
    // resource's data points into `file`'s bytes instead of copying them:
    // the caller keeps those alive and unchanged for as long as it uses any.
    static std::optional<PeResources> parse(const QByteArray &file, QString *error = nullptr, bool borrow = false);

    const QList<Resource> &all() const { return m_resources; }

    // Case-insensitive lookups; nullptr / empty when absent.
    const Resource *find(QStringView type, QStringView name) const;
    QList<const Resource *> ofType(QStringView type) const;

private:
    QList<Resource> m_resources;
};

} // namespace xpl
