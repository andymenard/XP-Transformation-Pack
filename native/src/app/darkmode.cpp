// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/darkmode.h"
#include "app/crisptext.h"
#include "app/colors.h"
#include "app/gtk.h"
#include "app/panel.h"
#include "app/paths.h"
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
#include <QStandardPaths>
namespace xpl::darkmode {
namespace {
bool write(const QString &path, const QByteArray &bytes, QString *error) {
 QDir().mkpath(QFileInfo(path).absolutePath()); QSaveFile f(path);
 if (!f.open(QIODevice::WriteOnly) || f.write(bytes)!=bytes.size() || !f.commit()) { if(error)*error=f.errorString(); return false; } return true;
}
QByteArray resource(const QString &name) { QFile f(QStringLiteral(":/dark-mode/")+name); if(!f.open(QIODevice::ReadOnly))return {}; return f.readAll(); }
}
std::optional<Look> applicationLook(const Look &source, bool dark, QString *error) {
 if(!dark)return source;
 Look result;
 if(source.classic()) {
  const ClassicScheme palette=ClassicScheme::fromThemeFile(ThemeFile::fromBytes(resource(QStringLiteral("palette.theme"))));
  ClassicScheme scheme=source.system;
  for(const QString &name:ClassicScheme::colourNames()) {
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
  const auto loaded=Look::fromStyle(generated,source.variant,error);if(!loaded)return {};result=*loaded;
 }
 result.id=source.id+QStringLiteral("-dark-applications");result.name=source.name+QStringLiteral(" (dark applications)");result.system.name=result.name;
 return result;
}
bool publish(bool dark,QString *error){return write(paths::data()+QStringLiteral("/appearance.json"),QJsonDocument(QJsonObject{{QStringLiteral("darkMode"),dark},{QStringLiteral("crispText"),State::load().settings.value(QStringLiteral("crispText")).toBool()}}).toJson(QJsonDocument::Compact),error);}
bool apply(Session &session,const Look &source,bool dark,QString *error){
 const auto apps=applicationLook(source,dark,error);if(!apps)return false;
 State state=State::load();QStringList log;
 if(!qtstyle::apply(session,*apps,state,&log,error)||!gtk::apply(session,*apps,state,&log,error))return false;
 const QString target=paths::localShare()+QStringLiteral("/color-schemes/")+source.id+QStringLiteral(".colors");
 if(!write(target,colorScheme(apps->system,source.id,{panelArt(source).panelColour,panelArt(source).textColour}),error))return false;
 session.writeConfig(QStringLiteral("kdeglobals"),{QStringLiteral("General")},QStringLiteral("ColorScheme"),QString(),false);
 if(!session.run({QStringLiteral("plasma-apply-colorscheme"),source.id}).ok()){if(error)*error=QStringLiteral("Could not apply application colors");return false;}
 state.settings.insert(QStringLiteral("darkMode"),dark);if(!state.save(error))return false;
 if(state.settings.value(QStringLiteral("crispText")).toBool())return crisptext::apply(session,true,error);
 return publish(dark,error);
}
}
