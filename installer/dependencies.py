"""Fixed package recipes; never upgrade the distribution or add repositories."""
import subprocess,tempfile,re
from pathlib import Path
APPS={'notepad','calculator','paint','picture_viewer','media_player','sound_recorder','volume_control','command_prompt','task_manager','pinball','minesweeper','solitaire'}
NATIVE={'theme_manager','desktop','apps','taskbar'}
RECIPES={
 'apt':{'base':'fontconfig kde-cli-tools qt6-tools-dev-tools','native':'build-essential cmake ninja-build pkg-config libarchive-dev zlib1g-dev liblzma-dev libbz2-dev qt6-base-dev qt6-declarative-dev','gui':'python3-pyqt6','firefox':'libgtk-3-0t64 libnss3 libnspr4 libasound2t64 libdbus-glib-1-2 libxt6 libx11-xcb1 libxcomposite1 libxdamage1 libxrandr2 libgbm1','login':'sddm python3-pyside6.qtcore python3-pyside6.qtdbus qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qtquick-shapes qml6-module-qt5compat-graphicaleffects','qml':'qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qt5compat-graphicaleffects','boot':'plymouth plymouth-themes','apps':'libgtk-3-0t64 libgtk-4-1 gsettings-desktop-schemas'},
 'dnf':{'base':'fontconfig kde-cli-tools qt6-qttools','native':'gcc-c++ cmake ninja-build pkgconf-pkg-config libarchive-devel zlib-ng-compat-devel xz-devel bzip2-devel qt6-qtbase-devel qt6-qtdeclarative-devel','gui':'python3-pyqt6','firefox':'gtk3 nss nspr alsa-lib dbus-glib libXt libX11 libXcomposite libXdamage libXrandr mesa-libgbm','login':'sddm sddm-wayland-plasma qt6-qtdeclarative qt6-qt5compat','qml':'qt6-qtdeclarative qt6-qt5compat','boot':'plymouth plymouth-plugin-script','apps':'gtk3 gtk4 gsettings-desktop-schemas'},
 'pacman':{'base':'fontconfig kde-cli-tools qt6-tools','native':'base-devel cmake ninja pkgconf libarchive zlib xz bzip2 qt6-base qt6-declarative','gui':'python-pyqt6','firefox':'gtk3 nss nspr alsa-lib dbus-glib libxt libx11 libxcomposite libxdamage libxrandr mesa','login':'sddm qt6-declarative qt6-5compat','qml':'qt6-declarative qt6-5compat','boot':'plymouth','apps':'gtk3 gtk4 gsettings-desktop-schemas'},
 'zypper':{'base':'fontconfig kde-cli-tools6 qt6-tools','native':'gcc-c++ cmake ninja pkg-config libarchive-devel zlib-devel liblzma-devel libbz2-devel qt6-base-devel qt6-declarative-devel','gui':'python3-qt6','firefox':'libgtk-3-0 mozilla-nss mozilla-nspr libasound2 libdbus-glib-1-2 libXt6 libX11-6 libXcomposite1 libXdamage1 libXrandr2 libgbm1','login':'sddm qt6-declarative-imports qt6-5compat-imports','qml':'qt6-declarative-imports qt6-5compat-imports','boot':'plymouth plymouth-scripts','apps':'libgtk-3-0 libgtk-4-1 gsettings-desktop-schemas'}
}
def packages(family,features,bootstrap=False,native_ready=False,caption_ready=False):
 r=RECIPES[family];groups=['gui'] if bootstrap else ['base']
 if not bootstrap:
  if set(features)&NATIVE and not native_ready:groups+=['native']
  if set(features)&({'explorer','control_panel','updates'}|APPS):groups+=['gui']
  groups += [x for x in ['firefox','login','boot','apps'] if x in features]
  if set(features)&{'lock','session'}:groups+=['qml']
 result=set(' '.join(r[g] for g in groups).split())
 extras={
 'apt':{'updates':'openssl gh','paint':'python3-pyqt6.qtwebengine','media_player':'vlc vlc-plugin-skins2','sound_recorder':'python3-pyqt6.qtmultimedia libqt6multimedia6','volume_control':'pulseaudio-utils python3-pyqt6.qtmultimedia','command_prompt':'konsole','task_manager':'python3-psutil','games':'build-essential cmake ninja-build libsdl2-dev','pinball':'libsdl2-mixer-dev','solitaire':'qt6-base-dev qt6-multimedia-dev'},
 'dnf':{'updates':'openssl gh','paint':'python3-pyqt6-webengine','media_player':'vlc vlc-gui-skins2','sound_recorder':'qt6-qtmultimedia','volume_control':'pulseaudio-utils qt6-qtmultimedia','command_prompt':'konsole','task_manager':'python3-psutil','games':'gcc-c++ cmake ninja-build SDL2-devel','pinball':'SDL2_mixer-devel','solitaire':'qt6-qtbase-devel qt6-qtmultimedia-devel'},
 'pacman':{'updates':'openssl github-cli','paint':'python-pyqt6-webengine','media_player':'vlc vlc-gui-skins2 vlc-gui-qt vlc-plugin-ffmpeg','sound_recorder':'qt6-multimedia qt6-multimedia-ffmpeg','volume_control':'libpulse qt6-multimedia','command_prompt':'konsole','task_manager':'python-psutil','games':'base-devel cmake ninja sdl2','pinball':'sdl2_mixer','solitaire':'qt6-base qt6-multimedia'},
 'zypper':{'updates':'openssl gh','paint':'python3-qt6-webengine','media_player':'vlc','sound_recorder':'qt6-multimedia','volume_control':'pulseaudio-utils qt6-multimedia','command_prompt':'konsole','task_manager':'python3-psutil','games':'gcc-c++ cmake ninja libSDL2-devel','pinball':'libSDL2_mixer-devel','solitaire':'qt6-base-devel qt6-multimedia-devel'}
 }
 extras[family]['explorer']={
 'apt':'build-essential cmake ninja-build extra-cmake-modules qt6-base-dev libkf6kio-dev libkf6solid-dev kio-extras udisks2',
 'dnf':'gcc-c++ cmake ninja-build extra-cmake-modules qt6-qtbase-devel kf6-kio-devel kf6-solid-devel kio-extras udisks2',
 'pacman':'base-devel cmake ninja extra-cmake-modules qt6-base kio solid kio-extras udisks2',
 'zypper':'gcc-c++ cmake ninja extra-cmake-modules qt6-base-devel kf6-kio-devel kf6-solid-devel kio-extras6 udisks2'}[family]
 extras[family]['tray']={'apt':'plasma-pa plasma-nm powerdevil bluedevil kscreen dbus-bin qml6-module-qtquick-controls qml6-module-qtquick-layouts','dnf':'plasma-pa plasma-nm powerdevil bluedevil kscreen dbus-tools','pacman':'plasma-pa plasma-nm powerdevil bluedevil kscreen dbus','zypper':'plasma6-pa plasma6-nm powerdevil6 bluedevil6 kscreen6 dbus-1'}[family]
 extras[family]['control_panel']={'apt':'pulseaudio-utils network-manager','dnf':'pulseaudio-utils NetworkManager','pacman':'libpulse networkmanager','zypper':'pulseaudio-utils NetworkManager'}[family]
 extras[family]['icons']={'apt':'python3-pil','dnf':'python3-pillow','pacman':'python-pillow','zypper':'python3-Pillow'}[family]
 extras[family]['caption_animation']={
 'apt':'build-essential cmake ninja-build extra-cmake-modules kwin-dev qt6-base-dev qt6-declarative-dev libdrm-dev libwayland-dev libvulkan-dev',
 'dnf':'gcc-c++ cmake ninja-build extra-cmake-modules kwin-devel qt6-qtbase-devel qt6-qtdeclarative-devel libdrm-devel wayland-devel vulkan-headers',
 'pacman':'base-devel cmake ninja extra-cmake-modules kwin qt6-base qt6-declarative libdrm wayland vulkan-headers',
 'zypper':'gcc-c++ cmake ninja extra-cmake-modules kwin6-devel qt6-base-devel qt6-declarative-devel libdrm-devel wayland-devel vulkan-headers'}[family]
 for key,value in extras[family].items():
  if key=='caption_animation' and caption_ready:continue
  if key in features or (key=='games' and set(features)&{'pinball','minesweeper','solitaire'}):result.update(value.split())
 return sorted(result)
def apt_builddeps(run):
 # Private source-list copy, retaining URIs, suites, components and Signed-By.
 # No persistent repository changes and no untrusted repositories introduced.
 with tempfile.TemporaryDirectory(prefix='xp-apt-sources-') as temp:
  root=Path(temp);root.chmod(0o755);parts=root/'parts';parts.mkdir()
  originals=[Path('/etc/apt/sources.list'),*Path('/etc/apt/sources.list.d').glob('*.list'),*Path('/etc/apt/sources.list.d').glob('*.sources')]
  count=0
  for source in originals:
   if not source.exists():continue
   text=source.read_text()
   if source.suffix=='.sources':text=re.sub(r'^Types:.*$', 'Types: deb deb-src',text,flags=re.M)
   else:text=re.sub(r'^(\s*)deb\s',r'\1deb-src ',text,flags=re.M)
   (parts/source.name).write_text(text);count+=1
  if not count:raise RuntimeError('APT sources are missing; cannot obtain widget build dependencies.')
  options=['-o','Dir::Etc::sourcelist=/dev/null','-o','Dir::Etc::sourceparts='+str(parts)]
  run(['apt-get',*options,'update'])
  run(['apt-get',*options,'build-dep','--assume-yes','plasma-workspace','plasma-desktop'])
def install(family,features,run,bootstrap=False,native_ready=False,caption_ready=False):
 chosen=packages(family,features,bootstrap,native_ready,caption_ready)
 if family=='apt':
  # Debian before the t64 transition uses the unsuffixed package names.
  for i,p in enumerate(chosen):
   if p.endswith('t64') and subprocess.run(['apt-cache','show',p],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode:chosen[i]=p[:-3]
  run(['apt-get','update']);run(['apt-get','install','--assume-yes',*chosen])
 elif family=='dnf':run(['dnf','install','-y',*chosen])
 elif family=='pacman':run(['pacman','-S','--needed','--noconfirm',*chosen])
 else:run(['zypper','--non-interactive','install',*chosen])
 if bootstrap or native_ready or 'taskbar' not in features:return
 if family=='apt':apt_builddeps(run)
 elif family=='dnf':
  run(['dnf','install','-y','dnf5-command(builddep)' if 'dnf5' in subprocess.check_output(['dnf','--version'],text=True).lower() else 'dnf-plugins-core'])
  run(['dnf','builddep','-y','plasma-workspace','plasma-desktop'])
 elif family=='zypper':run(['zypper','--non-interactive','source-install','--build-deps-only','plasma6-workspace','plasma6-desktop'])
 else:
  # Arch packages contain headers and CMake config files; never perform a
  # database-only refresh (which would create a partial-upgrade environment).
  run(['pacman','-S','--needed','--noconfirm','extra-cmake-modules','plasma-workspace','plasma-desktop','libplasma','qt6-wayland','qt6-svg','qt6-shadertools','qt6-positioning','qt6-location','qt6-webchannel','qt6-tools','kpackage','kcmutils','kglobalacceld','kscreenlocker','libksysguard','libkscreen','breeze','breeze-icons','dbusmenu-qt6','wayland-protocols','kuserfeedback','kaccounts-integration','kio-extras','networkmanager-qt','packagekit-qt6','scim','ibus','libcanberra','libxkbfile','libqalculate','appstream-qt','qcoro-qt6','phonon-qt6','accountsservice','polkit-qt6','libxcrypt','libxcursor'])
