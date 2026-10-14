#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build an XP fallback layer from credited CC0 recreations and semantic aliases."""
import argparse,configparser,hashlib,json,re,zipfile,os,shutil,sys
from pathlib import Path
from PIL import Image,ImageDraw
sys.path.insert(0,str(Path(__file__).resolve().parent))
import glyphs

ROLES={
 'folder':'Folder Closed','folder-open':'Folder Opened','folder-new':'New Folder','folder-home':'My Documents','folder-documents':'My Documents','folder-pictures':'My Pictures','folder-music':'My Music','folder-videos':'My Videos','folder-download':'Folder Closed','folder-remote':'Network Folder','folder-publicshare':'Shared Folder','user-desktop':'Desktop','user-trash':'Recycle Bin (empty)','user-trash-full':'Recycle Bin (full)',
 'document-new':'Generic Document','document-open':'Open','document-save':'Save','document-save-as':'Save','document-print':'Printer','document-print-preview':'Print photos','document-properties':'Properties','document-close':'Exit','edit-copy':'Copy','edit-cut':'Cut','edit-paste':'Paste','edit-delete':'Delete','edit-undo':'Undo','edit-redo':'Redo','edit-find':'Search','edit-rename':'Rename','edit-clear':'Erase','edit-select-all':'Checklist','list-add':'Add','list-remove':'Delete','go-home':'IE Home','go-up':'Up','go-next':'Forward','go-previous':'Back','go-first':'Setup back','go-last':'Setup Skip','view-refresh':'IE Refresh','view-list-details':'Detail View','view-list-icons':'Icon View','view-list-tree':'Folder View','view-preview':'Thumbnail View','view-split':'Folders open in new window','view-fullscreen':'Full Screen','view-restore':'Restore','zoom-in':'IE Enlarge Image','zoom-out':'IE Shrink Image',
 'system-file-manager':'Explorer','accessories-text-editor':'Notepad','utilities-terminal':'Command Prompt','accessories-calculator':'Calculator','accessories-character-map':'Charmap','accessories-screenshot-tool':'Take Picture','utilities-system-monitor':'Task Manager','accessories-archiver':'Zip folder','image-viewer':'Windows Picture and Fax Viewer','multimedia-player':'Windows Media Player 10','internet-mail':'Outlook Express','internet-web-browser':'Internet Explorer 6','preferences-system':'Control Panel','applications-office':'Wordpad','applications-development':'Command Prompt','applications-graphics':'Paint','applications-games':'Game Controller','applications-education':'Help and Support','applications-multimedia':'Windows Media Player 10','applications-internet':'Internet Explorer 6','applications-accessories':'Programs','applications-system':'Administrative Tools',
 'preferences-desktop-display':'Display Properties','preferences-desktop-theme':'Theme','preferences-desktop-color':'Appearance','preferences-desktop-font':'Fonts','preferences-desktop-keyboard':'Keyboard','preferences-desktop-mouse':'Mouse','preferences-desktop-sound':'Audio Devices','preferences-desktop-accessibility':'Accessibility','preferences-system-time':'Date and Time','preferences-system-network':'Network Connections','preferences-system-bluetooth':'Bluetooth Devices','preferences-system-firewall':'Firewall','preferences-system-power':'Power Options','preferences-system-users':'User Accounts','preferences-system-notifications':'Alert','preferences-system-windows':'Application Window','preferences-system-performance':'Performance','preferences-system-login':'User Accounts','preferences-system-privacy':'Privacy','preferences-system-security':'Security Center',
 'computer':'My Computer','computer-laptop':'Undock PC','drive-harddisk':'Local Disk','drive-removable-media':'Removable Media','drive-optical':'Optical Drive','drive-network':'Network Drive','media-optical':'CD-ROM','media-floppy':'Floppy Disk','media-flash':'SD Card','printer':'Printer','scanner':'Scanner','camera-photo':'Digital Camera','camera-video':'Camcorder','camera-web':'Webcam','input-keyboard':'Keyboard','input-mouse':'Mouse','input-gaming':'Game Controller','audio-speakers':'Volume','audio-headphones':'Audio Devices','audio-input-microphone':'Record','audio-volume-high':'Volume','audio-volume-muted':'Mute','battery':'Power Status','cpu':'Chip','phone':'Phone','network-wired':'Network Connection','network-wireless':'Wireless Network Connection','network-disconnected':'Disable Network Connection','network-bluetooth':'Bluetooth Devices','network-vpn':'VPN Connection',
 'dialog-information':'Information','dialog-warning':'Alert','dialog-error':'Critical','dialog-question':'Question','dialog-ok':'Success','dialog-cancel':'Prohibited','dialog-password':'Key','security-high':'Security - Ok','security-medium':'Security Alert','security-low':'Security Error','changes-prevent':'Padlock','changes-allow':'Key','emblem-favorite':'Favorites','emblem-important':'Important','emblem-symbolic-link':'Shortcut overlay','emblem-shared':'Sharing Hand','emblem-unreadable':'Restricted','emblem-locked':'Padlock','emblem-mail':'Email',
 'system-shutdown':'Power','system-reboot':'Restart','system-log-out':'Logout','system-lock-screen':'Lock','system-suspend':'Standby','system-hibernate':'Hibernate','system-run':'Run','system-software-update':'Windows Update','help-contents':'Help and Support','help-about':'Information','help-contextual':'Question','media-playback-start':'Play','media-playback-stop':'Stop','media-playback-pause':'Stop','window-minimize':'Minimize','window-maximize':'Maximize','media-skip-forward':'Setup Skip','media-skip-backward':'Setup back','media-record':'Record','media-eject':'Safely Remove Hardware',
 'text-x-generic':'Generic Text Document','application-x-executable':'Default','application-x-sharedlib':'DLL','application-x-archive':'Zip folder','application-pdf':'XPS document','application-certificate':'Certificate','application-json':'Java Script','application-xml':'XML','application-javascript':'Java Script','image-x-generic':'Bitmap','video-x-generic':'Generic Video','audio-x-generic':'Generic Audio','font-x-generic':'TrueType','application-msword':'Wordpad','application-spreadsheet':'Graph View','application-presentation':'Slideshow','application-script':'Script Component','application-database':'ODBC Data Sources',
 'mail-message-new':'OE Create Mail','mail-send':'OE Send','mail-reply-sender':'OE Reply','mail-reply-all':'OE Reply All','mail-forward':'OE Forward','mail-mark-read':'OE Mark as read','mail-mark-unread':'OE Mark as unread','mail-attachment':'OE Attatch','mail-folder-inbox':'OE Inbox','mail-folder-outbox':'OE Outbox','mail-folder-sent':'OE Sent Items','address-book':'Address Book','appointment':'Scheduled Tasks','notification':'Alert','internet-chat':'Windows Messenger','network-server':'Remote Desktop Server','user-identity':'User Accounts',
}
EXACT={
 'kate':'accessories-text-editor','kwrite':'accessories-text-editor','kedit':'accessories-text-editor','knotes':'accessories-text-editor','okular':'application-pdf','konsole':'utilities-terminal','yakuake':'utilities-terminal','kcalc':'accessories-calculator','kcharselect':'accessories-character-map','spectacle':'accessories-screenshot-tool','ksnapshot':'accessories-screenshot-tool','ark':'accessories-archiver','filelight':'drive-harddisk','partitionmanager':'drive-harddisk','kdf':'drive-harddisk','gwenview':'image-viewer','kolourpaint':'applications-graphics','krita':'applications-graphics','dolphin':'system-file-manager','konqueror':'internet-web-browser','firefox':'internet-web-browser','systemsettings':'preferences-system','kcontrol':'preferences-system','ksysguard':'utilities-system-monitor','plasma-systemmonitor':'utilities-system-monitor','systemmonitor':'utilities-system-monitor','kinfocenter':'preferences-system-performance','khelpcenter':'help-contents','discover':'system-software-update','plasmadiscover':'system-software-update','apper':'system-software-update','kmail':'internet-mail','kontact':'address-book','kaddressbook':'address-book','korganizer':'appointment','kalarm':'appointment','ktimer':'appointment','akregator':'internet-web-browser','kget':'folder-download','krdc':'network-server','krfb':'network-server','elisa':'multimedia-player','dragonplayer':'multimedia-player','amarok':'multimedia-player','juk':'multimedia-player','haruna':'multimedia-player','vlc':'multimedia-player','k3b':'drive-optical','kmix':'audio-speakers','skanpage':'scanner','skanlite':'scanner','bluedevil':'network-bluetooth','kdeconnect':'phone','kdeconnectindicator':'phone','kdeconnecttray':'phone','kwalletmanager':'changes-prevent','kwalletmanager5':'changes-prevent','ksecrets':'changes-prevent','kleopatra':'application-certificate','ksshaskpass':'changes-prevent','sweeper':'edit-clear','klipper':'edit-copy','krunner':'system-run','kdialog':'dialog-information','kmenuedit':'applications-accessories','khotkeys':'input-keyboard','kmousetool':'input-mouse','kmag':'zoom-in','kmouth':'preferences-desktop-accessibility','kaccess':'preferences-desktop-accessibility','kde':'applications-system','plasma':'applications-system','plasma-logo':'applications-system','start-here-kde':'applications-accessories','start-here':'applications-accessories',
 'configure':'preferences-system','configure-shortcuts':'input-keyboard','configure-toolbars':'preferences-system-windows','application-menu':'applications-accessories','hamburger-menu':'applications-accessories','overflow-menu':'applications-accessories','open-menu':'applications-accessories','window-close':'document-close','window-minimize':'window-minimize','window-maximize':'window-maximize','window-restore':'view-restore','preferences-desktop':'preferences-system','settings-configure':'preferences-system','insert-link':'emblem-symbolic-link','insert-image':'image-x-generic','tools-check-spelling':'dialog-ok','task-complete':'dialog-ok','tools-report-bug':'help-contextual','tools-wizard':'help-contents','list-refresh':'view-refresh','process-stop':'media-playback-stop','process-working':'view-refresh','edit-download':'folder-download','document-export':'document-save-as','document-import':'document-open','object-unlocked':'changes-allow','object-locked':'changes-prevent','snap':'camera-photo','settings':'preferences-system','user-home':'folder-home','desktop':'user-desktop','preferences':'preferences-system',
}
EXACT.update({'kdeconnect-tray':'phone','plasmavault':'changes-prevent','state-offline':'network-disconnected','state-ok':'dialog-ok','digikam':'image-viewer','showfoto':'image-viewer','debug-run':'system-run','kdeconnect.app':'phone','kdeconnect.nonplasma':'phone','kcm_networkmanagement':'preferences-system-network','kcm_kwindecoration':'preferences-desktop-theme','kcmkwindecoration':'preferences-desktop-theme'})
TRAY_ROLES={name:role for role,names in json.loads(Path(__file__).with_name('tray-icons.json').read_text()).items() for name in names}
EXACT.update(TRAY_ROLES)

def tray_variant(name):
 n=re.sub(r'(-symbolic|-rtl)+$','',name.lower())
 off=n in TRAY_ROLES and bool(re.search(r'-(off|disabled|inactive|blocked|unavailable|disconnected|unreachable|offline|paused|stop)$',n))
 record=n=='camera-on'
 badge={'input-caps-on':'A','input-num-on':'1','input-scroll-on':'S','input-combo-on':'A1'}.get(n,'')
 return off,record,badge

PATTERNS=[
 (r'(bluetooth).*(inactive|off|disabled|unavailable|blocked)','network-bluetooth'),(r'(wireless|network|connection).*(disconnect|offline|unavailable|disabled|off|failed)','network-disconnected'),
 (r'bluetooth','network-bluetooth'),(r'^battery','battery'),(r'(^audio.*(muted|off)|volume.*muted)','audio-volume-muted'),(r'(microphone|record)','audio-input-microphone'),(r'(^audio|volume|multimedia-volume|phonon)','audio-speakers'),
 (r'(firewall)','preferences-system-firewall'),(r'(^network|wireless|wifi|wlan|networkmanager|connection)','network-wireless'),(r'(vpn|proxy)','network-vpn'),
 (r'(notifications?|bell|alarm)','notification'),(r'(security|authentication|certificate|password|secret|wallet|lock)','changes-prevent'),
 (r'(accessibility|speech|screenreader)','preferences-desktop-accessibility'),(r'(brightness|nightlight|redshift|display|screen|monitor|kscreen|gamma|compositor)','preferences-desktop-display'),(r'(font|typography)','preferences-desktop-font'),(r'(theme|appearance|colors?|palette|plasma-style|window-decoration)','preferences-desktop-theme'),
 (r'(keyboard|keymap|shortcut|input-method|kcminput|key_bindings)','input-keyboard'),(r'(mouse|touchpad|cursor|pointer)','input-mouse'),(r'(power|energy|sleep|suspend|hibernate|powerdevil)','preferences-system-power'),(r'(date|time|clock|calendar|appointment)','appointment'),(r'(users?|account|login|sddm)','user-identity'),(r'(printer|print|cups)','printer'),(r'(scan)','scanner'),(r'(camera|photo-camera)','camera-photo'),(r'(removable|usb|flash|sd-card)','drive-removable-media'),(r'(drive|disk|storage|partition|mount|raid)','drive-harddisk'),
 (r'^folder.*(music|audio)','folder-music'),(r'^folder.*(photo|image|picture)','folder-pictures'),(r'^folder.*(movie|video)','folder-videos'),(r'^folder.*(network|remote|cloud)','folder-remote'),(r'^folder.*(open)','folder-open'),(r'^folder','folder'),
 (r'(trash|recycle)','user-trash'),(r'(delete|remove|discard|cancel|trash-empty)','edit-delete'),(r'(copy|duplicate|clone)','edit-copy'),(r'(paste)','edit-paste'),(r'(^|[-_.])cut([-_.]|$)','edit-cut'),(r'(rename)','edit-rename'),(r'(undo)','edit-undo'),(r'(redo)','edit-redo'),(r'(search|find|locate|lookup)','edit-find'),(r'(save|export|download)','document-save'),(r'(open|import|load-file)','document-open'),(r'(refresh|reload|sync)','view-refresh'),(r'(properties|preferences|settings|config|kcm)','preferences-system'),(r'(^|[-_.])(new|add|create|insert)([-_.]|$)','list-add'),
 (r'(back|previous|prev|left)','go-previous'),(r'(forward|next|right)','go-next'),(r'(^|[-_.])(up|parent)([-_.]|$)','go-up'),(r'(home)','go-home'),(r'(play)','media-playback-start'),(r'(stop|pause)','media-playback-stop'),(r'(eject|unmount)','media-eject'),(r'(zoom.*out|shrink)','zoom-out'),(r'(zoom|magnif)','zoom-in'),(r'(help|support|question)','help-contents'),(r'(error|fail|critical)','dialog-error'),(r'(warning|alert|important)','dialog-warning'),(r'(info|about|tip)','dialog-information'),(r'(success|accept|check|apply|approve)','dialog-ok'),
 (r'(favorite|bookmark|rating|star)','emblem-favorite'),(r'(link|shortcut)','emblem-symbolic-link'),(r'(email|mail|message)','internet-mail'),(r'(chat|messeng|conversation)','internet-chat'),(r'(web|www|browser|internet)','internet-web-browser'),(r'(games?|solitaire|mine|mahjong|chess|sudoku|cards)','applications-games'),
]

def role(name,context=''):
 n=re.sub(r'(-symbolic|-rtl)+$','',name.lower());n=re.sub(r'^org\.kde\.','',n);n=EXACT.get(n,n)
 if n in ROLES:return n,'exact'
 # File formats use their type family; no random gear substituted for documents.
 if context.lower()=='mimetypes':
  for pattern,value in [(r'(zip|archive|compress|tar|gzip|rar|7z|cab)','application-x-archive'),(r'(sheet|excel|csv)','application-spreadsheet'),(r'(presentation|powerpoint|odp)','application-presentation'),(r'(word|rtf|richtext|opendocument.text|document)','application-msword'),(r'(pdf|postscript|epub)','application-pdf'),(r'(certificate|pkcs|pkix|pgp)','application-certificate'),(r'(image|png|jpeg|gif|bitmap|tiff)','image-x-generic'),(r'(audio|music|ogg|midi|mp3|flac)','audio-x-generic'),(r'(video|movie|mp4|mpeg|wmv)','video-x-generic'),(r'(font|ttf|otf)','font-x-generic'),(r'(xml|xsl|html)','application-xml'),(r'(script|javascript|json|python|perl|shell|ruby)','application-script'),(r'(database|sql)','application-database'),(r'(sharedlib|library)','application-x-sharedlib'),(r'(executable|package|installer)','application-x-executable')]:
   if re.search(pattern,n):return value,'file-type'
  return 'text-x-generic','file-type'
 for pattern,value in PATTERNS:
  if re.search(pattern,n):return value,'semantic'
 if context in ('preferences','Categories','Preferences','applets','Applets'):return 'preferences-system','settings-category'
 if n.startswith(('preferences-','applications-')):return 'preferences-system','settings-category'
 return None,None

catalogue=json.loads(Path(__file__).with_name('catalogue.json').read_text())
ROLES.update(catalogue['roles']);EXACT.update(catalogue['exact'])

def build(pack,output):
 output=Path(output);output.mkdir(parents=True,exist_ok=True)
 if any(output.iterdir()):raise RuntimeError('Build into an empty directory to avoid stale icons')
 os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
 try:from PySide6 import QtGui
 except ImportError:from PyQt6 import QtGui
 application=QtGui.QGuiApplication.instance() or QtGui.QGuiApplication([])
 z=zipfile.ZipFile(pack);assets={Path(n).stem:n for n in z.namelist() if '/Windows XP Icons/' in n and n.endswith('.png') and '__MACOSX' not in n}
 for key,asset in ROLES.items():
  if asset not in assets:raise RuntimeError('Missing source artwork '+asset)
 candidates={name:('mimetypes' if name.startswith(('application-','text-','image-','video-','audio-x-','font-')) else 'categories' if name.startswith(('preferences-','applications-')) else 'actions') for name in ROLES}
 candidates.update({name:'apps' for name in EXACT})
 for name in TRAY_ROLES:
  candidates[name]='status';candidates[name+'-symbolic']='status'
 for name in ['kate','kwrite','konsole','spectacle','ark','gwenview','okular','kcalc','kdeconnect.app','kdeconnect.nonplasma','dolphin','plasma-systemmonitor','systemmonitor','kinfocenter','elisa','skanpage','skanlite']:
  candidates['org.kde.'+name]='apps'
 breeze_sources={}
 for p in Path('/usr/share/icons/breeze').rglob('*.svg'):
  if p.parts[-3].isdigit():context=p.parts[-4]
  else:context=p.parts[-3]
  candidates[p.stem]=context
  size=int(p.parts[-2]) if p.parts[-2].isdigit() else 0
  previous=breeze_sources.get(p.stem)
  if previous is None or size>previous[0]:breeze_sources[p.stem]=(size,p)
 # Desktop files add app IDs which do not exist in the Breeze theme itself.
 desktop_icons={}
 for root in [Path('/usr/share/applications'),Path.home()/'.local/share/applications']:
  for p in root.glob('*.desktop'):
   config=configparser.ConfigParser(interpolation=None,strict=False)
   try:config.read(p);section=config['Desktop Entry'];icon=section.get('Icon','')
   except (KeyError,configparser.Error):continue
   if icon and not icon.startswith('/'):
    candidates.setdefault(icon,'apps');desktop_icons[icon]=desktop_icons.get(icon,'')+section.get('Categories','')
 mappings={};missing=[]
 for name,context in sorted(candidates.items()):
  control=glyphs.control(name)
  if control:
   mappings[name]={'role':'xp-control','source':'XPlasma control: '+control[0],'reason':'control-glyph','context':context,'control':control}
   continue
  key,reason=role(name,context)
  if key is None and (name.lower().startswith('org.kde.') or name.lower() in EXACT):key,reason='applications-accessories','kde-application'
  if key is None and context=='apps' and name in desktop_icons:
   # KDE-specific application names can be shorter than their reverse-DNS ID.
   if 'KDE;' in desktop_icons[name]:key,reason='applications-accessories','kde-application'
  # Preserve precise diagrams, math, formatting and paint-tool operations.
  specialized=bool(re.match(r'^(align-|distribute-|format-|draw-|path-|node-|transform-|math-|paint-|edit-node-|debug-|code-|view-object-|insert-(?:table|text|math|object|formula|horizontal-rule|vertical-rule))',name))
  if name in breeze_sources and (key is None or specialized):
   original=breeze_sources[name][1]
   source_name=hashlib.sha256(original.read_bytes()).hexdigest()[:20]+'.svg'
   source_dir=output/'KDE-Breeze-SVG';source_dir.mkdir(exist_ok=True)
   shutil.copyfile(original,source_dir/source_name)
   mappings[name]={'role':'specialized-glyph','source':'KDE-Breeze-SVG/'+source_name,'reason':'xp-styled-kde-glyph','context':context,'original':str(original.relative_to('/usr/share/icons/breeze'))}
   continue
  if key is None:missing.append({'name':name,'context':context});continue
  mappings[name]={'role':key,'source':assets[ROLES[key]],'reason':'tray-state' if re.sub(r'(-symbolic|-rtl)+$','',name) in TRAY_ROLES else reason,'context':context}
 directories=[];rendered={}
 context_names={'actions':'Actions','apps':'Applications','applets':'Categories','preferences':'Categories','categories':'Categories','devices':'Devices','emblems':'Emblems','places':'Places','mimetypes':'MimeTypes','status':'Status','emotes':'Emotes','animations':'Animations'}
 for size in [16,22,24,32,48,64,128]:
  for name,item in mappings.items():
   context=context_names.get(item['context'].lower(),'Actions');folder=f'{size}x{size}/{context.lower()}';dest=output/folder/(name+'.png');dest.parent.mkdir(parents=True,exist_ok=True)
   if folder not in directories:directories.append(folder)
   if item['reason'] in {'control-glyph','xp-styled-kde-glyph'}:
    symbolic='-symbolic' in name
    cache=(item['source'],size,symbolic)
    canonical='xp-'+hashlib.sha256((item['source']+str(symbolic)).encode()).hexdigest()[:12]+'.png'
    path=dest.parent/canonical
    if cache not in rendered:
     svg=glyphs.control_svg(item['control']) if item['reason']=='control-glyph' else glyphs.xp_svg((output/item['source']).read_bytes())
     image=glyphs.render(svg,size,symbolic)
     rendered[cache]=image if symbolic else glyphs.bevel(image)
    if not path.exists():rendered[cache].save(path)
    dest.symlink_to(canonical)
    continue
   off,record,badge=tray_variant(name)
   off=off or bool(re.search(r'(bluetooth|notifications?|nightlight|redshift).*(inactive|off|disabled|blocked|inhibited|paused)',name))
   pause=item['role']=='media-playback-pause'
   cache=(item['source'],size,off,pause,record,badge)
   if cache not in rendered:
    with z.open(item['source']) as stream:image=Image.open(stream).convert('RGBA')
    image=image.resize((size,size),Image.Resampling.LANCZOS)
    if pause:
     image=Image.new('RGBA',(size,size));draw=ImageDraw.Draw(image)
     for left in (.22,.58):
      draw.rectangle((int(size*left),int(size*.17),int(size*(left+.19)),int(size*.82)),fill='#316ac5',outline='#003c74')
      draw.line((int(size*left)+1,int(size*.17)+1,int(size*left)+1,int(size*.82)-1),fill='#9dc7ef',width=max(1,size//16))
    if off:
     image.putalpha(image.getchannel('A').point(lambda a:int(a*.45)));draw=ImageDraw.Draw(image);draw.line((size*.2,size*.8,size*.8,size*.2),fill='#d40000',width=max(1,size//12))
    if record:
     draw=ImageDraw.Draw(image);draw.ellipse((size*.55,size*.55,size*.95,size*.95),fill='#e52525',outline='white',width=max(1,size//16))
    if badge:
     draw=ImageDraw.Draw(image);height=max(9,round(size*.55));width=max(9,round(height*(1.35 if len(badge)>1 else 1)));draw.rectangle((size-width,size-height,size-1,size-1),fill='#ffffe1',outline='#316ac5');draw.text((size-width+2,size-height),badge,fill='#003399',font_size=max(7,height-3))
    canonical='xp-'+hashlib.sha256((item['source']+str(off)+str(pause)+str(record)+badge).encode()).hexdigest()[:12]+'.png';path=dest.parent/canonical
    if not path.exists():image.save(path)
    rendered[cache]=image
   canonical='xp-'+hashlib.sha256((item['source']+str(off)+str(pause)+str(record)+badge).encode()).hexdigest()[:12]+'.png';path=dest.parent/canonical
   if not path.exists():rendered[cache].save(path)
   if dest.exists() or dest.is_symlink():dest.unlink()
   dest.symlink_to(canonical)
 index='[Icon Theme]\nName=XPlasma XP Extended\nComment=XP recreations by marchmountain; XPlasma Linux name mappings\nInherits=hicolor\nDirectories='+','.join(sorted(directories))+'\n'
 for folder in sorted(directories):
  size=folder.split('x')[0];context=next((v for v in context_names.values() if v.lower()==folder.split('/')[1]),'Actions');index+=f'\n[{folder}]\nSize={size}\nContext={context}\nType=Fixed\n'
 (output/'index.theme').write_text(index)
 (output/'ALIAS-PROVENANCE.json').write_text(json.dumps(mappings,indent=2)+'\n')
 (output/'UNMAPPED.json').write_text(json.dumps(missing,indent=2)+'\n')
 credits=Path(__file__).with_name('CREDITS.md');shutil.copy2(credits,output/credits.name)
 shutil.copy2(Path(__file__).with_name('LICENSE-CC0.txt'),output/'LICENSE-CC0.txt')
 license_file=Path('/usr/share/licenses/breeze-icon-theme/COPYING-ICONS')
 if not license_file.exists():raise RuntimeError('KDE artwork license notice missing')
 shutil.copy2(license_file,output/'COPYING-KDE-ICONS')
 shutil.copytree(Path(__file__).with_name('LICENSES'),output/'LICENSES')
 (output/'COVERAGE.json').write_text(json.dumps({'mapped':len(mappings),'unmapped':len(missing),'reasons':{reason:sum(item['reason']==reason for item in mappings.values()) for reason in sorted({item['reason'] for item in mappings.values()})}},indent=2)+'\n')
 print('Mapped',len(mappings),'names; remaining',len(missing))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('pack');p.add_argument('output');a=p.parse_args();build(a.pack,a.output)
