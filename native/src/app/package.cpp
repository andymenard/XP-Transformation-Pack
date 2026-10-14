// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/package.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace xpl {

namespace {
QByteArray png(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.convertToFormat(QImage::Format_ARGB32).save(&buffer, "PNG");
    return bytes;
}

QByteArray escaped(const QString &text)
{
    return text.toHtmlEscaped().toUtf8();
}
} // namespace

void SvgSheet::image(const QString &id, const QImage &image)
{
    if (image.isNull() || image.width() <= 0 || image.height() <= 0)
        return;
    m_elements << QByteArray("<image id=\"") + escaped(id) + "\" x=\"" + QByteArray::number(m_width)
                      + "\" y=\"0\" width=\"" + QByteArray::number(image.width()) + "\" height=\""
                      + QByteArray::number(image.height()) + "\" preserveAspectRatio=\"none\" xlink:href=\"data:image/png;base64,"
                      + png(image).toBase64() + "\"/>";
    m_width += image.width();
    m_height = qMax(m_height, image.height());
}

void SvgSheet::hint(const QString &id, QSize size)
{
    m_elements << QByteArray("<rect id=\"") + escaped(id) + "\" x=\"" + QByteArray::number(m_width) + "\" y=\"0\" width=\""
                      + QByteArray::number(size.width()) + "\" height=\"" + QByteArray::number(size.height())
                      + "\" fill=\"none\"/>";
    m_width += size.width();
    m_height = qMax(m_height, size.height());
}

QByteArray SvgSheet::document() const
{
    QByteArray svg = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                     "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\""
                     + QByteArray::number(qMax(1, m_width)) + "\" height=\"" + QByteArray::number(qMax(1, m_height)) + "\">\n";
    for (const QByteArray &e : m_elements)
        svg += "  " + e + "\n";
    return svg + "</svg>\n";
}

Package::Package(const QString &directory)
    : m_directory(QDir(directory).absolutePath())
{
    QDir(m_directory).removeRecursively();
    if (!QDir().mkpath(m_directory))
        m_error = QStringLiteral("can't create %1").arg(m_directory);
}

bool Package::write(const QString &relative, const QByteArray &bytes)
{
    const QString path = m_directory + QLatin1Char('/') + relative;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) < 0 || !file.commit()) {
        if (m_error.isEmpty())
            m_error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

bool Package::write(const QString &relative, const QImage &image)
{
    return write(relative, png(image));
}

bool Package::writeJson(const QString &relative, const QJsonObject &json)
{
    return write(relative, QJsonDocument(json).toJson(QJsonDocument::Indented));
}

QJsonObject pluginMetadata(const QString &id, const QString &name, const QString &description)
{
    return QJsonObject{
        {QStringLiteral("KPlugin"), QJsonObject{{QStringLiteral("Id"), id},
                                                {QStringLiteral("Name"), name},
                                                {QStringLiteral("Description"), description},
                                                {QStringLiteral("Authors"), QJsonArray{QJsonObject{{QStringLiteral("Name"), QStringLiteral("XPlasma")}}}},
                                                {QStringLiteral("Version"), QStringLiteral("1.0")},
                                                {QStringLiteral("License"), QStringLiteral("Unknown")}}},
        {QLatin1String(kGeneratedKey), true},
    };
}

} // namespace xpl
