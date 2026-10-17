#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
import json,sys,tempfile,threading,subprocess
from pathlib import Path
try:from PyQt6 import QtCore,QtGui,QtWidgets
except ImportError:from PySide6 import QtCore,QtGui,QtWidgets
import core
Signal=getattr(QtCore,'pyqtSignal',None) or QtCore.Signal
class Worker(QtCore.QObject):
 result=Signal(object);error=Signal(str);progress=Signal(str)
 def __init__(self,fn,with_progress=False):super().__init__();self.fn=fn;self.with_progress=with_progress
 def run(self):
  try:self.result.emit(self.fn(self.progress.emit) if self.with_progress else self.fn())
  except Exception as e:self.error.emit(str(e))
class Updates(QtWidgets.QWidget):
 def __init__(self):
  super().__init__();self.setWindowTitle('XPlasma Update');self.resize(640,460);self.folder=None;self.offer=None;self.checked=False
  self.home=Path.home();self.root=self.home/'.local/share/xplasma-updater'
  config=self.root/'config.json';self.config=json.loads((config if config.exists() else core.BASE/'config.json').read_text())
  box=QtWidgets.QVBoxLayout(self);title=QtWidgets.QLabel('Windows XP Transformation Pack Update');title.setStyleSheet('font-size:18px;font-weight:bold;color:palette(link)');box.addWidget(title)
  box.addWidget(QtWidgets.QLabel('Signed updates for your installed features. Your theme settings stay intact.'))
  self.repo=QtWidgets.QLineEdit(self.config['repository']);box.addWidget(self.repo)
  self.status=QtWidgets.QLabel('Public signed updates — no GitHub account needed.' if self.config.get('access')=='public' else 'Private updates use your GitHub CLI login.');self.status.setWordWrap(True);box.addWidget(self.status)
  self.notes=QtWidgets.QTextBrowser();box.addWidget(self.notes,1);self.optional=QtWidgets.QListWidget();self.optional.setMaximumHeight(95);box.addWidget(self.optional)
  self.output=QtWidgets.QPlainTextEdit();self.output.setReadOnly(True);self.output.setMaximumBlockCount(2000);self.output.setMaximumHeight(150);self.output.hide();box.addWidget(self.output)
  self.progress=QtWidgets.QProgressBar();self.progress.setRange(0,0);self.progress.hide();box.addWidget(self.progress)
  self.finish=QtWidgets.QPushButton('Finish pending native setup');self.finish.hide();self.finish.clicked.connect(self.finish_pending);box.addWidget(self.finish)
  row=QtWidgets.QHBoxLayout();box.addLayout(row);self.check=QtWidgets.QPushButton('Check for updates');self.install=QtWidgets.QPushButton('Install update');self.install.setEnabled(False);self.undo=QtWidgets.QPushButton('Roll back last update');self.local=QtWidgets.QPushButton('Open downloaded update…')
  for button in [self.check,self.install,self.undo,self.local]:row.addWidget(button)
  self.check.clicked.connect(self.fetch);self.install.clicked.connect(self.apply);self.undo.clicked.connect(self.rollback);self.local.clicked.connect(self.open_local)
  state=None
  try:core.recover(self.home);state,info,features=core.find_installation(self.home);self.notes.setPlainText('Detected '+state.name+'\nInstalled features: '+', '.join(sorted(features)))
  except Exception as e:self.status.setText(str(e))
  # Apply the signed snapping helper after reopening the updated client.
  # The old running GUI remains unchanged during a self-update.
  try:
   if state is None:return
   self.finish.setVisible(core.pending_setup(self.home) is not None)
   helper=state/'runtime/tools/xp-window-outlines.py'
   after=json.loads((state/'after.json').read_text())
   record=after.get(str(helper.relative_to(self.home)),{})
   if helper.is_file() and record.get('kind')=='file' and record.get('value')==core.digest(helper):
    subprocess.run([sys.executable,str(helper)],check=True,timeout=20,capture_output=True)
  except Exception as e:self.status.setText('Theme maintenance needs attention: '+str(e))
 def busy(self,b):
  for button in [self.check,self.undo,self.local,self.finish]:button.setEnabled(not b)
  self.install.setEnabled(not b and self.offer is not None)
  self.progress.setVisible(b)
  self.repo.setEnabled(not b)
 def append_output(self,text):
  self.output.show();self.output.appendPlainText(text)
 def work(self,fn,callback,progress=False):
  self.busy(True);self.worker=Worker(fn,progress);self.worker.progress.connect(self.append_output);self.worker.result.connect(lambda result:(self.busy(False),callback(result)));self.worker.error.connect(lambda error:(self.busy(False),self.fail(error)));threading.Thread(target=self.worker.run,daemon=True).start()
 def fail(self,error):self.status.setText(error);QtWidgets.QMessageBox.warning(self,'Update could not complete',error)
 def fetch(self):
  repo=self.repo.text().strip();self.status.setText('Checking GitHub and verifying publisher signature…')
  def get():
   release=core.latest(repo,self.config.get('channel','stable'));folder=Path(tempfile.mkdtemp(prefix='xplasma-release-'));core.download(repo,release,folder);data=core.verify_release(folder)
   if release['tag_name']!='v'+data['version']:raise core.UpdateError('Release tag and signed version disagree.')
   return folder,data
  self.work(get,self.show_offer)
 def open_local(self):
  folder=QtWidgets.QFileDialog.getExistingDirectory(self,'Select folder containing release.json, release.sig and payload.tar.gz')
  if folder:self.work(lambda:(Path(folder),core.verify_release(folder)),self.show_offer)
 def show_offer(self,result):
  self.folder,self.offer=result;self.optional.clear();state,info,features=core.find_installation(self.home)
  self.notes.setPlainText('XPlasma '+self.offer['version']+'\n\n'+self.offer.get('notes',''))
  for feature in sorted({f['feature'] for f in self.offer['files'] if f.get('optional')}-features):
   item=QtWidgets.QListWidgetItem('Add '+feature.replace('_',' ').title());item.setData(QtCore.Qt.ItemDataRole.UserRole,feature);item.setFlags(item.flags()|QtCore.Qt.ItemFlag.ItemIsUserCheckable);item.setCheckState(QtCore.Qt.CheckState.Unchecked);self.optional.addItem(item)
  try:p=core.plan(self.home,self.offer,qt='.'.join(QtCore.qVersion().split('.')[:2]));self.status.setText(str(len(p['entries']))+' files ready. '+str(len(p['conflicts']))+' locally modified files need review.');self.install.setEnabled(not p['conflicts'] and bool(p['entries'] or self.optional.count()))
  except Exception as e:self.install.setEnabled(False);self.status.setText(str(e))
 def apply(self):
  selected=[self.optional.item(i).data(QtCore.Qt.ItemDataRole.UserRole) for i in range(self.optional.count()) if self.optional.item(i).checkState()==QtCore.Qt.CheckState.Checked]
  self.status.setText('Backing up files and installing verified update…');self.output.clear();self.work(lambda report:core.apply(self.home,self.folder,selected,qt='.'.join(QtCore.qVersion().split('.')[:2]),progress=report),self.done,progress=True)
 def done(self,result):
  self.offer=None;self.install.setEnabled(False);self.status.setText('Update installed. Reopen updated apps; Plasma components refresh at next login.');self.notes.append('\nRecovery backup: '+str(result));journal=json.loads((Path(result)/'journal.json').read_text());
  self.finish.setVisible(core.pending_setup(self.home) is not None)
  if journal.get('native_setup'):self.status.setText('Update and native setup completed. Log out and back in to reload desktop components.')
  elif journal.get('activation_deferred'):self.status.setText('Update installed. Log out and back in to activate theme maintenance; reopen updated apps.')
 def finish_pending(self):
  self.output.clear();self.status.setText('Completing native setup in this window…')
  self.work(lambda report:core.finish_pending(self.home,progress=report),self.done,progress=True)
 def rollback(self):
  record=self.root/'current.json'
  if not record.exists():self.status.setText('No update to roll back.');return
  transaction=json.loads(record.read_text())['transaction'];self.work(lambda:core.rollback_locked(self.home,transaction),lambda _:(self.status.setText('Last update rolled back.'),self.install.setEnabled(False)))
 def closeEvent(self,event):
  if getattr(self,'worker',None) and not self.check.isEnabled():event.ignore();return
  event.accept()
def setup_appearance(app):
 # Follow the desktop platform theme, including live palette/font changes.
 # Fixed light backgrounds and inherited dark foregrounds must never be mixed.
 app.setApplicationName('xplasma-update');app.setApplicationDisplayName('XPlasma Update')
 app.setDesktopFileName('xplasma-update')
 app.setWindowIcon(QtGui.QIcon.fromTheme('system-software-update',app.style().standardIcon(QtWidgets.QStyle.StandardPixmap.SP_BrowserReload)))

if __name__=='__main__':
 app=QtWidgets.QApplication(sys.argv);setup_appearance(app);window=Updates();window.show()
 if '--preview' in sys.argv:QtCore.QTimer.singleShot(300,lambda:(window.grab().save(sys.argv[-1]),app.quit()))
 sys.exit(app.exec())
