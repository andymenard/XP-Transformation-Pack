import importlib.util,json,subprocess,sys,tempfile,unittest
from pathlib import Path
BASE=Path(__file__).resolve().parents[1]
ROOT=BASE.parent if BASE.name=='installer' else BASE
spec=importlib.util.spec_from_file_location('screen_sync',BASE/'sync-payload.py');sync=importlib.util.module_from_spec(spec);spec.loader.exec_module(sync)
class SessionSpriteMask(unittest.TestCase):
 def test_only_exact_xp_transparency_key_is_removed(self):
  from PIL import Image
  with tempfile.TemporaryDirectory() as tmp:
   path=Path(tmp)/'20150.png';image=Image.new('RGBA',(3,1));colors=[(255,0,255,255),(250,172,159,255),(40,60,70,128)];image.putdata(colors);image.save(path)
   sync.normalize_session_sprites(tmp)
   self.assertEqual(list(Image.open(path).getdata()),[(255,0,255,0),*colors[1:]])
   first=path.read_bytes();sync.normalize_session_sprites(tmp);self.assertEqual(path.read_bytes(),first)
class BootGeometry(unittest.TestCase):
 def test_artwork_and_blocks_share_a_fitted_frame(self):
  path=ROOT/'themes/xp-boot/xp-boot.script'
  if not path.is_file():path=BASE/'payload/templates/boot/xp-boot.script'
  script=path.read_text().replace('fun ','function ')
  harness=r'''
function assert(ok, message){if(!ok)throw new Error(message||'geometry assertion');}
for(const [w,h] of [[640,480],[800,600],[1024,768],[1280,800],[1366,768],[1920,1080],[3840,2160]]){
 Math.Int=Math.floor;
 const ctx={Math:Math,Window:{GetWidth:()=>w,GetHeight:()=>h,SetBackgroundTopColor(){},SetBackgroundBottomColor(){}},Plymouth:{SetRefreshFunction(f){this.refresh=f},SetDisplayPasswordFunction(){},SetDisplayQuestionFunction(){},SetDisplayNormalFunction(){},SetMessageFunction(){}}};
 ctx.Image=function(name){return {w:name==='boot.png'?640:22,h:name==='boot.png'?480:9,Scale(w,h){this.w=Math.floor(w);this.h=Math.floor(h);return this},GetWidth(){return this.w},GetHeight(){return this.h}}};
 ctx.Sprite=function(){return {SetZ(){},SetPosition(x,y){this.x=x;this.y=y},SetOpacity(){}}};
 const run=new Function('Window','Image','Sprite','Plymouth','Math', 'var scale,canvas,base,origin_x,origin_y,segment_image,segment,phase,prompting,message_sprite,input_sprite;'+SCRIPT+';return {canvas:canvas,origin_x:origin_x,origin_y:origin_y,scale:scale,segment:segment};');
 Object.assign(ctx,run(ctx.Window,ctx.Image,ctx.Sprite,ctx.Plymouth,Math));
 assert(ctx.canvas.w<=w&&ctx.canvas.h<=h);
 assert(Math.abs(ctx.canvas.w-w)<=1||Math.abs(ctx.canvas.h-h)<=1,'frame underfills screen');
 assert(Math.abs(ctx.origin_x*2+ctx.canvas.w-w)<=1);
 assert(Math.abs(ctx.origin_y*2+ctx.canvas.h-h)<=1);
 for(let i=0;i<100;i++){
  ctx.Plymouth.refresh();const x=(ctx.segment.x-ctx.origin_x)/ctx.scale,y=(ctx.segment.y-ctx.origin_y)/ctx.scale;
  assert(x>=258&&x+22<=379&&y>=352&&y<=353.01,'segment outside original track');
 }
}
'''.replace('SCRIPT',json.dumps(script))
  runner='from PyQt6.QtCore import QCoreApplication\nfrom PyQt6.QtQml import QJSEngine\nimport sys\napp=QCoreApplication([]); engine=QJSEngine(); result=engine.evaluate(sys.stdin.read())\nif result.isError(): raise RuntimeError(result.toString())\n'
  result=subprocess.run([sys.executable,'-c',runner],input=harness,capture_output=True,text=True,timeout=20)
  self.assertEqual(result.returncode,0,result.stderr)
if __name__=='__main__':unittest.main()
