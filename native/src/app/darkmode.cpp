// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/darkmode.h"
#include "app/crisptext.h"
#include "app/colors.h"
#include "app/gtk.h"
#include "app/panel.h"
#include "app/paths.h"
#include "app/maintenance.h"
#include "app/qttarget.h"
#include "app/state.h"
#include "core/themefile.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QJsonObject>
#include <QStandardPaths>
namespace xpl::darkmode {
namespace {
bool write(const QString &path, const QByteArray &bytes, QString *error) {
 QDir().mkpath(QFileInfo(path).absolutePath()); QSaveFile f(path);
 if (!f.open(QIODevice::WriteOnly) || f.write(bytes)!=bytes.size() || !f.commit()) { if(error)*error=f.errorString(); return false; } return true;
}
QByteArray resource(const QString &name) { QFile f(QStringLiteral(":/dark-mode/")+name); if(!f.open(QIODevice::ReadOnly))return {}; return f.readAll(); }
// Each build names its style and builder, so pruneCache knows what it's of.
QString builder() { return QString::fromLatin1(QCryptographicHash::hash(QByteArray(resource(QStringLiteral("build.py"))+resource(QStringLiteral("palette.theme"))),QCryptographicHash::Sha256).toHex()); }
QString cacheRoot() { return paths::cache()+QStringLiteral("/dark-mode"); }
}
std::optional<Look> applicationLook(const Look &source, bool dark, QString *error) {
 if(!dark)return source;
 Look result;
 if(source.classic()) {
  const ClassicScheme palette=ClassicScheme::fromThemeFile(ThemeFile::fromBytes(resource(QStringLiteral("palette.theme"))));
  ClassicScheme scheme=source.system;
  for(const QString &name:ClassicScheme::colourNames()) {
   if(source.system.shellColors && (name == QStringLiteral("Hilight") || name == QStringLiteral("HilightText")))continue;
   if(name.contains(QStringLiteral("Title")) || name.contains(QStringLiteral("Border")) || name==QStringLiteral("Background") || name==QStringLiteral("WindowFrame"))continue;
   scheme.setColour(name,palette.colour(name));
  }
  result=Look::fromScheme(scheme);
 } else {
  QFile original(source.stylePath); if(!original.open(QIODevice::ReadOnly)){if(error)*error=original.errorString();return {};}
  const QByteArray script=resource(QStringLiteral("build.py")),palette=resource(QStringLiteral("palette.theme"));
  if(script.isEmpty()||palette.isEmpty()){if(error)*error=QStringLiteral("Missing bundled dark-mode builder");return {};}
  const QString hash=QString::fromLatin1(QCryptographicHash::hash(QByteArray(original.readAll()+script+palette),QCryptographicHash::Sha256).toHex());
  const QString folder=paths::cache()+QStringLiteral("/dark-mode/")+hash;
  const QString generated=folder+QStringLiteral("/applications.msstyles");
  if(!QFileInfo::exists(generated)) {
   if(!write(folder+QStringLiteral("/build.py"),script,error)||!write(folder+QStringLiteral("/palette.theme"),palette,error))return {};
   QProcess process;process.start(QStandardPaths::findExecutable(QStringLiteral("python3")),{folder+QStringLiteral("/build.py"),source.stylePath,generated});
   if(!process.waitForFinished(60000)||process.exitCode()!=0){process.kill();process.waitForFinished();QFile::remove(generated);if(error)*error=QString::fromUtf8(process.readAllStandardError());return {};}
  }
  const QByteArray marker=QJsonDocument(QJsonObject{{QStringLiteral("source"),QFileInfo(source.stylePath).absoluteFilePath()},{QStringLiteral("builder"),builder()}}).toJson(QJsonDocument::Compact);
  QFile known(folder+QStringLiteral("/source.json"));
  if((!known.open(QIODevice::ReadOnly) || known.readAll()!=marker) && !write(known.fileName(),marker,error))return {};
  const auto loaded=Look::fromStyle(generated,source.variant,error);if(!loaded)return {};result=*loaded;
  LiveSession previewSession;maintenance::run(previewSession,{},folder,true);
 }
 result.id=source.id+QStringLiteral("-dark-applications");result.name=source.name+QStringLiteral(" (dark applications)");result.system.name=result.name;
 return result;
}
void pruneCache(const std::function<bool(const QString &)> &wanted){
 // (The applied style's build stays whatever it is: xplasmarc names it.)
 QSet<QString> inUse;
 for(const QString &rc:{paths::config()+QStringLiteral("/xplasmarc"),paths::data()+QStringLiteral("/active/xplasmarc")})
  if(const QString used=QSettings(rc,QSettings::IniFormat).value(QStringLiteral("Theme/Source")).toString();!used.isEmpty())inUse.insert(QFileInfo(used).absolutePath());
 const QString current=builder();
 for(const QFileInfo &folder:QDir(cacheRoot()).entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot)) {
  if(inUse.contains(folder.absoluteFilePath()))continue;
  QFile known(folder.absoluteFilePath()+QStringLiteral("/source.json"));
  const QJsonObject marker=known.open(QIODevice::ReadOnly)?QJsonDocument::fromJson(known.readAll()).object():QJsonObject();
  const QString source=marker.value(QStringLiteral("source")).toString();
  if(source.isEmpty() || marker.value(QStringLiteral("builder")).toString()!=current || !QFileInfo::exists(source) || !wanted(source))
   QDir(folder.absoluteFilePath()).removeRecursively();
 }
}
bool publish(bool dark,QString *error){return write(paths::data()+QStringLiteral("/appearance.json"),QJsonDocument(QJsonObject{{QStringLiteral("darkMode"),dark},{QStringLiteral("crispText"),State::load().settings.value(QStringLiteral("crispText")).toBool()}}).toJson(QJsonDocument::Compact),error);}
bool apply(Session &session,const Look &source,bool dark,QString *error){
 const auto apps=applicationLook(source,dark,error);if(!apps)return false;
 State state=State::load();QStringList log;
 if(!qtstyle::apply(session,*apps,state,&log,error)||!gtk::apply(session,*apps,state,&log,error))return false;
 const QString schemeId=state.settings.value(QStringLiteral("appliedPackageId")).toString(source.id);
 const QString target=paths::localShare()+QStringLiteral("/color-schemes/")+schemeId+QStringLiteral(".colors");
 if(!write(target,colorScheme(apps->system,schemeId,{panelArt(source).panelColour,panelArt(source).textColour}),error))return false;
 session.writeConfig(QStringLiteral("kdeglobals"),{QStringLiteral("General")},QStringLiteral("ColorScheme"),QString(),false);
 if(!session.run({QStringLiteral("plasma-apply-colorscheme"),schemeId}).ok()){if(error)*error=QStringLiteral("Could not apply application colors");return false;}
 state.settings.insert(QStringLiteral("darkMode"),dark);if(!state.save(error))return false;
 maintenance::run(session);
 if(state.settings.value(QStringLiteral("crispText")).toBool())return crisptext::apply(session,true,error);
 return publish(dark,error);
}
}
