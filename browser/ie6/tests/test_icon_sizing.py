"""Artwork sizing checks; set IE6_TEST_FIREFOX for isolated Gecko render tests."""
import base64
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile
import time
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
ICONS = ROOT / 'chrome/ie6-icons'


class ArtworkTests(unittest.TestCase):
    def test_wrappers_keep_original_pixels_and_small_intrinsic_size(self):
        spec = importlib.util.spec_from_file_location('icon_wrappers', ROOT/'build-icon-wrappers.py')
        generator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(generator)
        for path, expected in generator.wrappers():
            with self.subTest(icon=path.stem):
                self.assertEqual(path.read_text(), expected)
                svg = ET.fromstring(expected)
                self.assertEqual((svg.get('width'), svg.get('height')), ('16', '16'))
                data = svg[0].get('{http://www.w3.org/1999/xlink}href')
                self.assertEqual(base64.b64decode(data.split(',', 1)[1]), (ICONS/(path.stem+'.png')).read_bytes())
        self.assertFalse((ICONS/'sized/application-ie.svg').exists())
        self.assertFalse((ICONS/'sized/windows-loading.svg').exists())

    def test_all_bundled_static_replacements_use_wrappers(self):
        names = {p.stem for p in (ICONS/'sized').glob('*.svg')}
        for path in [*ROOT.glob('chrome/*.css'), ROOT/'runtime/ie6-ui.cfg']:
            for name in re.findall(r'([\w-]+)\.png', path.read_text()):
                with self.subTest(file=path.name, icon=name):
                    self.assertNotIn(name, names, 'Use the intrinsically sized SVG rather than the full-size PNG')
        for path in ROOT.glob('chrome/*.css'):
            for name in re.findall(r'ie6-icons/(sized/[\w-]+\.svg)', path.read_text()):
                self.assertTrue((ICONS/name).is_file(), name)


class Marionette:
    def __init__(self, port):
        self.socket = socket.create_connection(('127.0.0.1', port), timeout=1)
        self.socket.settimeout(30)
        self.buffer = b''
        self.serial = 0
        self.receive()

    def receive(self):
        while b':' not in self.buffer:
            self.buffer += self.socket.recv(65536)
        prefix, rest = self.buffer.split(b':', 1)
        size = int(prefix)
        while len(rest) < size:
            rest += self.socket.recv(65536)
        self.buffer = rest[size:]
        return json.loads(rest[:size])

    def command(self, name, params=None):
        self.serial += 1
        packet = json.dumps([0, self.serial, name, params or {}]).encode()
        self.socket.sendall(str(len(packet)).encode()+b':'+packet)
        response = self.receive()
        if response[2]:
            raise RuntimeError(response[2])
        return response[3]

    def script(self, script, args=None, asynchronous=False):
        result = self.command('WebDriver:ExecuteAsyncScript' if asynchronous else 'WebDriver:ExecuteScript',
                              {'script': script, 'args': args or [], 'newSandbox': False,
                               'sandbox': 'default', 'scriptTimeout': 20000})
        return result.get('value', result)


@unittest.skipUnless(os.environ.get('IE6_TEST_FIREFOX'), 'Set IE6_TEST_FIREFOX to a dedicated Firefox binary')
class GeckoSizingTests(unittest.TestCase):
    def test_all_artwork_and_toolbar_sizes_at_normal_and_fractional_scales(self):
        for scale in [1, 1.25, 1.5, 2]:
            with self.subTest(scale=scale), tempfile.TemporaryDirectory(prefix='ie6-icons-test-') as temporary:
                profile = Path(temporary)
                shutil.copytree(ROOT/'chrome', profile/'chrome')
                with socket.socket() as available:
                    available.bind(('127.0.0.1', 0))
                    port = available.getsockname()[1]
                (profile/'user.js').write_text('\n'.join([
                    'user_pref("toolkit.legacyUserProfileCustomizations.stylesheets",true);',
                    f'user_pref("marionette.port",{port});',
                    f'user_pref("layout.css.devPixelsPerPx","{scale}");',
                    'user_pref("browser.startup.homepage_override.mstone","ignore");',
                    'user_pref("browser.shell.checkDefaultBrowser",false);',
                    'user_pref("datareporting.policy.dataSubmissionEnabled",false);',
                ]))
                env = dict(os.environ, HOME=temporary, MOZ_HEADLESS='1', MOZ_CRASHREPORTER_DISABLE='1')
                with (profile/'browser.log').open('w+') as log:
                    process = subprocess.Popen([os.environ['IE6_TEST_FIREFOX'], '--headless', '--no-remote',
                        '--offline', '--profile', temporary, '--marionette', '--remote-allow-system-access',
                        'about:blank'], env=env, stdout=log, stderr=log)
                    client = None
                    try:
                        for attempt in range(60):
                            try:
                                client = Marionette(port)
                                break
                            except OSError:
                                if process.poll() is not None:
                                    log.seek(0)
                                    self.fail(log.read())
                                time.sleep(.5)
                        self.assertIsNotNone(client, 'Firefox Marionette failed to start')
                        client.command('WebDriver:NewSession', {'capabilities': {'alwaysMatch': {}}})
                        client.command('Marionette:SetContext', {'value': 'chrome'})
                        # Parse the changed autoconfig with the actual ESR JavaScript engine.
                        client.script('return true;\n' + (ROOT/'runtime/ie6-ui.cfg').read_text())
                        result = client.script('''
                            const done=arguments[arguments.length-1], names=arguments[0];
                            const root=document.createElementNS('http://www.w3.org/1999/xhtml','div');
                            document.documentElement.append(root);
                            const base=Services.io.newFileURI(Services.dirsvc.get('ProfD',Ci.nsIFile)).spec;
                            Promise.all(names.flatMap(name=>[null,24,32].map(async size=>{
                                const img=document.createElementNS('http://www.w3.org/1999/xhtml','img');
                                img.src=base+'/chrome/ie6-icons/sized/'+name+'.svg';
                                if(size){img.style.width=size+'px';img.style.height=size+'px';}
                                root.append(img);await img.decode();
                                const r=img.getBoundingClientRect();
                                const canvas=document.createElementNS('http://www.w3.org/1999/xhtml','canvas');
                                canvas.width=canvas.height=32;const ctx=canvas.getContext('2d');
                                ctx.drawImage(img,0,0,32,32);
                                const pixels=ctx.getImageData(0,0,32,32).data;
                                return {name,size,natural:img.naturalWidth,width:r.width,height:r.height,
                                    visible:pixels.some((v,i)=>i%4===3&&v>0)};
                            }))).then(rows=>{root.remove();done({dpr:devicePixelRatio,rows});},
                                error=>done({error:String(error)}));
                        ''', [[p.stem for p in sorted((ICONS/'sized').glob('*.svg'))]], asynchronous=True)
                        self.assertNotIn('error', result)
                        self.assertAlmostEqual(result['dpr'], scale, places=2)
                        for row in result['rows']:
                            self.assertEqual(row['natural'], 16, row)
                            self.assertAlmostEqual(row['width'], row['size'] or 16, delta=.05, msg=str(row))
                            self.assertAlmostEqual(row['height'], row['size'] or 16, delta=.05, msg=str(row))
                            self.assertTrue(row['visible'], row)
                        main = client.script('''return Array.from(document.querySelectorAll(
                            '#nav-bar :is(#back-button,#forward-button) .toolbarbutton-icon'
                            )).map(n=>n.getBoundingClientRect().width);''')
                        self.assertEqual(main, [24, 24])
                        # The custom status bar is mounted by autoconfig in #browser.
                        # Exercise its real chrome layout, including the reserved gap,
                        # for browser F11 and page/video full-screen state flags.
                        fullscreen = client.script('''
                            const root=document.documentElement, browser=document.getElementById('browser');
                            const status=document.createXULElement('hbox');status.id='ie6-statusbar';browser.append(status);
                            // Newer ESR builds omit this legacy container.
                            const existingBottom=document.getElementById('browser-bottombox');
                            const bottom=existingBottom||document.createXULElement('hbox');
                            if(!existingBottom){bottom.id='browser-bottombox';root.append(bottom);}
                            const snapshot=()=>({status:getComputedStyle(status).display,
                                bottom:getComputedStyle(bottom).display,
                                padding:parseFloat(getComputedStyle(browser).paddingBottom)});
                            const rows=[];
                            for(const dark of [false,true]) {
                                if(dark)root.setAttribute('xplasma-dark','true');else root.removeAttribute('xplasma-dark');
                                rows.push({mode:'normal',dark,...snapshot()});
                                root.setAttribute('sizemode','maximized');
                                rows.push({mode:'maximized',dark,...snapshot()});
                                root.removeAttribute('sizemode');
                                for(const flag of ['inFullscreen','inDOMFullscreen']) {
                                    root.setAttribute(flag,'true');rows.push({mode:flag,dark,...snapshot()});
                                    root.removeAttribute(flag);rows.push({mode:'restored',dark,...snapshot()});
                                }
                            }
                            root.removeAttribute('xplasma-dark');status.remove();
                            if(!existingBottom)bottom.remove();return rows;
                        ''')
                        for row in fullscreen:
                            hidden = row['mode'] in ('inFullscreen', 'inDOMFullscreen')
                            self.assertEqual(row['status'] == 'none', hidden, row)
                            self.assertEqual(row['bottom'] == 'none', hidden, row)
                            self.assertEqual(row['padding'], 0 if hidden else 21, row)
                        library = client.script('''
                            const done=arguments[arguments.length-1];
                            const win=Services.ww.openWindow(null,
                                'chrome://browser/content/places/places.xhtml','','chrome,dialog=no',null);
                            win.addEventListener('load',()=>{
                                // Some ESR/headless configurations hide this toolbar.
                                // Expose it to exercise the same unsized XUL controls.
                                win.document.getElementById('placesToolbar').style.setProperty('display','flex','important');
                                win.setTimeout(()=>{
                                    const widths=Array.from(win.document.querySelectorAll(
                                        ':is(#back-button,#forward-button) .toolbarbutton-icon'
                                    )).map(n=>n.getBoundingClientRect().width);
                                    win.close();done(widths);
                                },100);
                            },{once:true});
                        ''', asynchronous=True)
                        self.assertEqual(library, [16, 16])
                    finally:
                        if client:
                            client.socket.close()
                        process.terminate()
                        try:
                            process.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()


if __name__ == '__main__':
    unittest.main()
