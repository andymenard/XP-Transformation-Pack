#include "app/apply.h"
#include "app/startmenu.h"
#include "app/state.h"
#include "app/paths.h"
#include "app/library.h"
#include "app/overrides.h"
#include <QThread>
#include <QVariantMap>
#include <QApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QTextStream>
int main(int argc,char **argv){
 QApplication app(argc,argv);const auto state=xpl::State::load();QString error;
 // Keep any active editor preview, not only the last saved preset.
 QJsonObject live=state.settings.value(QLatin1String(xpl::kLiveStartColorsKey)).toObject();
 QFile active(QDir::homePath()+QStringLiteral("/.config/xplasma-live-colors.json"));
 if(active.open(QIODevice::ReadOnly)){
  const auto doc=QJsonDocument::fromJson(active.readAll());
  if(doc.isObject())live=doc.object();
 }
 auto look=state.appliedKind==QLatin1String("style")?xpl::Look::fromStyle(state.style,state.variant,&error):std::optional<xpl::Look>(xpl::Look::fromScheme(xpl::schemes::load(state.scheme).value_or(xpl::ClassicScheme())));
 if(!look){QTextStream(stderr)<<error;return 1;}
 const QString dir=xpl::paths::data()+QStringLiteral("/start/live-preview-")+QString::number(QDateTime::currentMSecsSinceEpoch());
 auto theme=xpl::writeStartTheme(*look,dir,&error,xpl::StartButtonExtras::fromJson(state.settings.value(QLatin1String(xpl::kStartButtonKey)).toObject()));
 if(theme.isEmpty()){QTextStream(stderr)<<error;return 2;}
 xpl::LiveSession session;bool installed=false;
 session.restartPlasmaShell([&]{installed=xpl::installStartPlasmoid(xpl::paths::localShare()+QStringLiteral("/plasma/plasmoids"),&error)
 && xpl::overrides::installTitleShadows(xpl::paths::localShare()+QStringLiteral("/aurorae/themes"),xpl::paths::localShare()+QStringLiteral("/kwin/aurorae/aurorae.qml"),&error);});
 if(!installed){QTextStream(stderr)<<error;return 3;}
 auto result=session.script(xpl::scriptFrom(QStringLiteral("start-theme"),{{QStringLiteral("START"),QLatin1String(xpl::kStartPlasmoidId)},{QStringLiteral("THEME"),theme.toVariantMap()}}),&error);
 if(!result || result->trimmed().toInt()<1){QTextStream(stderr)<<error;return 4;}
 if(!xpl::applyLiveStartColors(session,live,&error)){QTextStream(stderr)<<error;return 5;}
 const QString library=session.readConfig(QStringLiteral("kwinrc"),{QStringLiteral("org.kde.kdecoration2")},QStringLiteral("library"));
 if(library==QLatin1String("org.kde.kwin.aurorae")) {
  session.writeConfig(QStringLiteral("kwinrc"),{QStringLiteral("org.kde.kdecoration2")},QStringLiteral("library"),QStringLiteral("org.kde.breeze"));session.reconfigureKWin();QThread::msleep(350);
  session.writeConfig(QStringLiteral("kwinrc"),{QStringLiteral("org.kde.kdecoration2")},QStringLiteral("library"),library);session.reconfigureKWin();
 }
 QTextStream(stdout)<<"Installed live tint artwork and QML; existing base theme retained.\n";
}
