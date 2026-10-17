"""The OS-family clone must preserve glyph, metrics, hinting and license tables."""
import importlib.util, os, struct, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('tahoma_os',ROOT/'native/src/app/data/fonts/tahoma_os.py')
fonts=importlib.util.module_from_spec(spec);spec.loader.exec_module(fonts)
class TahomaOS(unittest.TestCase):
 def test_installer_generator_matches_native(self):
  self.assertEqual((ROOT/'installer/tahoma_os.py').read_bytes(),(ROOT/'native/src/app/data/fonts/tahoma_os.py').read_bytes())
 def test_original_tables_and_notices_survive_renaming(self):
  folder=Path(os.environ.get('XPLASMA_TAHOMA_SOURCE',str(Path.home()/'.local/share/fonts/xp')))
  if not (folder/'tahoma.ttf').is_file():self.skipTest('Original Tahoma faces are not supplied')
  for filename in ['tahoma.ttf','tahomabd.ttf']:
   data=(folder/filename).read_bytes();clone=fonts.renamed(data)
   original=fonts.tables(data);renamed=fonts.tables(clone)
   self.assertEqual(fonts.checksum(clone),0xB1B0AFBA)
   self.assertIn('Tahoma OS'.encode('utf-16-be'),renamed[b'name'])
   for tag,table in original.items():
    if tag==b'name' or tag==b'DSIG':continue
    if tag==b'head':self.assertEqual(table[:8]+table[12:],renamed[tag][:8]+renamed[tag][12:])
    else:self.assertEqual(table,renamed[tag],tag)
   # Copyright, trademark and license name records are preserved byte-for-byte.
   def notices(table):
    form,count,offset=struct.unpack_from('>HHH',table);result=[]
    for i in range(count):
     platform,encoding,language,kind,length,start=struct.unpack_from('>6H',table,6+12*i)
     if kind in [0,7,8,9,11,12,13,14]:result.append((platform,encoding,language,kind,table[offset+start:offset+start+length]))
    return result
   self.assertEqual(notices(original[b'name']),notices(renamed[b'name']))
 def test_missing_source_fails_without_substituting_a_font(self):
  with tempfile.TemporaryDirectory() as directory:
   with self.assertRaises(FileNotFoundError):fonts.generate(Path(directory),Path(directory)/'out')
if __name__=='__main__':unittest.main()
