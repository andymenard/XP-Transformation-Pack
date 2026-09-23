// xplasma-render: XPLasma's renderer, driven by the same job files as uxdump.
//
//   xplasma-render JOB OUTDIR --style NAME=PATH [--style ...]
//   xplasma-render --plan PATH NAME      print a job covering the whole style
//
// For every `part` line, writes s<N>_<Class>_<part>_<state>_<w>x<h>_black.png
// and _white.png (the render composited over opaque black / white, exactly
// how uxdump captures real uxtheme) and a metrics.txt line in uxdump's
// format. native/tests/oracle_compare.py puts the two side by side.
// `style -` (whatever style XP has applied) has no meaning here; parts under
// it are skipped.
#include "core/render.h"
#include "core/schema.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QTextStream>

using namespace xpl;

// The layout INI for a (colour, size) pair: FILERESNAMES is colour-major.
static QString variantFor(const QString &path, const QString &colour, const QString &size)
{
    const auto probe = Theme::load(path);
    if (!probe)
        return QString();
    const VariantTable &t = probe->variants();
    const int c = colour == QLatin1String("-") ? 0 : int(t.colourNames.indexOf(colour));
    const int s = size == QLatin1String("-") ? 0 : int(t.sizeNames.indexOf(size));
    const int n = int(t.sizeNames.size());
    const int index = c * qMax(1, n) + s;
    if (c < 0 || s < 0 || index >= t.fileNames.size())
        return QString();
    return t.fileNames.at(index);
}

// --plan: a job covering every part `path` defines (by the schema), in every
// state, at its natural size, stretched, and squeezed below its margins.
static int plan(const QString &path, const QString &name)
{
    QString error;
    const auto theme = Theme::load(path, QString(), &error);
    if (!theme) {
        QTextStream(stderr) << error << '\n';
        return 1;
    }
    Renderer r(theme);
    QTextStream out(stdout);
    out << "style {style:" << name << "} - -\n";
    QSet<QString> seen;
    QStringList keys;
    for (auto it = theme->ini().sections().cbegin(); it != theme->ini().sections().cend(); ++it)
        if (!it.key().part.isEmpty() && it.key().sub.isEmpty())
            keys.append(it.key().cls + QLatin1Char('.') + it.key().part);
    keys.sort();
    for (const QString &key : keys) {
        if (seen.contains(key))
            continue;
        seen.insert(key);
        const QString cls = key.section(QLatin1Char('.'), 0, 0), part = key.section(QLatin1Char('.'), 1);
        const int partId = schema::partId(cls, part);
        if (!partId)
            continue;
        int states = 0;
        while (!schema::stateName(cls, part, states + 1).isEmpty())
            ++states;
        const PartRef first{cls, partId, 1, QString()};
        if (!r.hasPart(first))
            continue;
        QSize natural = r.partSize(first, PartSize::True);
        if (natural.isEmpty())
            natural = QSize(16, 16);
        const QSize sizes[] = {natural, natural * 2 + QSize(37, 11), QSize(qMax(1, natural.width() / 2), qMax(1, natural.height() / 2))};
        for (int state = qMin(1, states); state <= states; ++state)
            for (const QSize &sz : sizes)
                out << "part " << cls << ' ' << partId << ' ' << state << ' ' << sz.width() << ' ' << sz.height() << '\n';
    }
    return 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() >= 4 && args.at(1) == QLatin1String("--plan"))
        return plan(args.at(2), args.at(3));
    if (args.size() < 3) {
        QTextStream(stderr) << "usage: xplasma-render JOB OUTDIR --style NAME=PATH ...\n";
        return 2;
    }
    QHash<QString, QString> styles;
    for (int i = 3; i + 1 < args.size(); ++i)
        if (args.at(i) == QLatin1String("--style")) {
            const QString spec = args.at(++i);
            styles.insert(spec.section(QLatin1Char('='), 0, 0), spec.section(QLatin1Char('='), 1));
        }
    QFile job(args.at(1));
    if (!job.open(QIODevice::ReadOnly | QIODevice::Text))
        return 2;
    const QString outdir = args.at(2);
    QDir().mkpath(outdir);
    QFile metricsFile(outdir + QLatin1String("/metrics.txt"));
    metricsFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    QTextStream metrics(&metricsFile);

    std::unique_ptr<Renderer> renderer;
    int styleIndex = 0;
    while (!job.atEnd()) {
        const QString line = QString::fromUtf8(job.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const QStringList t = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (t.first() == QLatin1String("style")) {
            ++styleIndex;
            renderer.reset();
            if (t.size() < 4)
                continue;   // "style -": the VM's applied style
            // {style:NAME} ... colour size; the name may contain spaces.
            const QString spec = t.mid(1, t.size() - 3).join(QLatin1Char(' '));
            const QString name = spec.mid(7, spec.size() - 8);   // strip "{style:" and "}"
            const QString path = styles.value(name);
            const QString variant = variantFor(path, t.at(t.size() - 2), t.last());
            QString error;
            if (auto theme = Theme::load(path, variant, &error))
                renderer = std::make_unique<Renderer>(theme);
            metrics << "# style s" << styleIndex << ": " << path << ' ' << variant
                    << (renderer ? QString() : QLatin1String(" error=") + error) << '\n';
        } else if (t.first() == QLatin1String("part") && t.size() == 6 && renderer) {
            PartRef ref{t.at(1), t.at(2).toInt(), t.at(3).toInt(), QString()};
            const QSize size(t.at(4).toInt(), t.at(5).toInt());
            const QImage img = renderer->background(ref, size);
            const QString stem = QStringLiteral("%1/s%2_%3_%4_%5_%6x%7").arg(outdir).arg(styleIndex)
                                     .arg(ref.cls).arg(ref.part).arg(ref.state).arg(size.width()).arg(size.height());
            Renderer::over(img, qRgb(0, 0, 0)).save(stem + QLatin1String("_black.png"));
            Renderer::over(img, qRgb(255, 255, 255)).save(stem + QLatin1String("_white.png"));
            const QSize smin = renderer->partSize(ref, PartSize::Min, size);
            const QSize strue = renderer->partSize(ref, PartSize::True, size);
            const QSize sdraw = renderer->partSize(ref, PartSize::Draw, size);
            const QRect content = renderer->contentRect(ref, QRect(QPoint(0, 0), size));
            const QMargins sm = renderer->margins(ref, u"sizingmargins");
            const QMargins cm = renderer->margins(ref, u"contentmargins");
            metrics << 's' << styleIndex << ' ' << ref.cls << ' ' << ref.part << ' ' << ref.state << ' '
                    << size.width() << ' ' << size.height() << " drawn=1"
                    << " min=" << smin.width() << ',' << smin.height()
                    << " true=" << strue.width() << ',' << strue.height()
                    << " draw=" << sdraw.width() << ',' << sdraw.height()
                    << " content=" << content.left() << ',' << content.top() << ','
                    << content.right() + 1 << ',' << content.bottom() + 1
                    << " sizing=" << sm.left() << ',' << sm.right() << ',' << sm.top() << ',' << sm.bottom()
                    << " contentmargins=" << cm.left() << ',' << cm.right() << ',' << cm.top() << ',' << cm.bottom()
                    << '\n';
        }
    }
    metrics << "# done\n";
    return 0;
}
