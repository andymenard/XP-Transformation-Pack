#!/usr/bin/python3
"""At login: says so, once per Plasma, when a Plasma update took the XP taskbar's builds off, and opens Setup to rebuild them."""
import json,subprocess,sys
from pathlib import Path
BASE=Path(__file__).resolve().parent
sys.path.insert(0,str(BASE))
import rebuild
SERVICE=INTERFACE='org.freedesktop.Notifications'
PATH='/org/freedesktop/Notifications'

def told_path(home):return Path(home)/'.local/share'/rebuild.STATE_NAME/'rebuild-notice.json'

def pending(home=None):
 # What to tell, if anything: not again for a Plasma already told about.
 home=Path(home or Path.home());lost=rebuild.status(home,BASE)
 if not lost:return None
 try:told=json.loads(told_path(home).read_text()).get('plasma')
 except (OSError,ValueError):told=None
 return None if told==lost['plasma'] else lost

def message(lost):
 if lost['available']:
  return ('Plasma was updated to '+lost['plasma'],
          'The XP taskbar is paused: its parts were built for the Plasma before. Choose Rebuild to build them for this one.')
 return ('Plasma was updated to '+lost['plasma'],
         'The XP taskbar is paused: this installer has no parts for Plasma '+lost['series']+'. Download the latest XP Transformation Pack release to bring it back.')

def notify(lost,home=None):
 from PyQt6 import QtCore,QtDBus
 app=QtCore.QCoreApplication(sys.argv[:1])
 bus=QtDBus.QDBusConnection.sessionBus();shown={'id':None,'tries':0}
 class Listener(QtCore.QObject):
  @QtCore.pyqtSlot(QtDBus.QDBusMessage)
  def invoked(self,message):
   ident,key=message.arguments()
   if ident!=shown['id']:return
   if key in ('default','rebuild'):subprocess.Popen([sys.executable,str(BASE/'gui.py'),'--rebuild'],start_new_session=True)
   app.quit()
  @QtCore.pyqtSlot(QtDBus.QDBusMessage)
  def closed(self,message):
   if message.arguments()[0]==shown['id']:app.quit()
 listener=Listener()
 bus.connect(SERVICE,PATH,INTERFACE,'ActionInvoked',listener.invoked)
 bus.connect(SERVICE,PATH,INTERFACE,'NotificationClosed',listener.closed)
 title,body=message(lost)
 actions=['default','Rebuild…','rebuild','Rebuild…'] if lost['available'] else []
 def show():
  # At login the notification server may not be up yet: waited for, a minute at most.
  if not bus.interface().isServiceRegistered(SERVICE).value():
   shown['tries']+=1
   if shown['tries']>60:app.quit()
   return
  timer.stop()
  call=QtDBus.QDBusMessage.createMethodCall(SERVICE,PATH,INTERFACE,'Notify')
  call.setArguments(['XP Transformation Pack',QtDBus.QDBusArgument(0,QtCore.QMetaType.Type.UInt.value),'preferences-desktop-theme',title,body,
                     QtDBus.QDBusArgument(actions,QtCore.QMetaType.Type.QStringList.value),{'desktop-entry':'kde-xp-suite'},0])
  reply=bus.call(call)
  if reply.type()!=QtDBus.QDBusMessage.MessageType.ReplyMessage:app.quit();return
  shown['id']=reply.arguments()[0]
  told_path(home or Path.home()).write_text(json.dumps({'plasma':lost['plasma']})+'\n')
  if not actions:app.quit()
 timer=QtCore.QTimer();timer.timeout.connect(show);timer.start(1000);QtCore.QTimer.singleShot(0,show)
 return app.exec()

def main():
 lost=pending()
 return notify(lost) if lost else 0
if __name__=='__main__':sys.exit(main())
