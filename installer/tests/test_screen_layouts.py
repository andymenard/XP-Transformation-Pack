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
 function image(w,h){return {w:w,h:h,Scale(w,h){return image(Math.floor(w),Math.floor(h))},Crop(x,y,w,h){assert(x>=0&&y>=0&&x+w<=this.w&&y+h<=this.h,'invalid crop');return image(w,h)},GetWidth(){return this.w},GetHeight(){return this.h}}}; ctx.Image=name=>image(name==='boot.png'?640:22,name==='boot.png'?480:9);
 ctx.Sprite=function(image){return {image:image,opacity:1,SetImage(image){this.image=image},SetZ(){},SetPosition(x,y){this.x=x;this.y=y},SetOpacity(v){this.opacity=v}}};
 const run=new Function('Window','Image','Sprite','Plymouth','Math', 'var scale,canvas,base,origin_x,origin_y,segment_image,segment,phase,prompting,message_sprite,input_sprite,wrapped_segment,track_left,track_right,track_width,offset;'+SCRIPT+';return {canvas:canvas,origin_x:origin_x,origin_y:origin_y,scale:scale,segment:segment,wrapped:wrapped_segment,track_left:track_left,track_right:track_right};');
 Object.assign(ctx,run(ctx.Window,ctx.Image,ctx.Sprite,ctx.Plymouth,Math));
 assert(ctx.canvas.w<=w&&ctx.canvas.h<=h);
 assert(Math.abs(ctx.canvas.w-w)<=1||Math.abs(ctx.canvas.h-h)<=1,'frame underfills screen');
 assert(Math.abs(ctx.origin_x*2+ctx.canvas.w-w)<=1);
 assert(Math.abs(ctx.origin_y*2+ctx.canvas.h-h)<=1);
 let split=false,wrapped=false,last;
 for(let i=0;i<240;i++){
  ctx.Plymouth.refresh();let width=0,visible=0;
  for(const sprite of [ctx.segment,ctx.wrapped]){
   if(!sprite.opacity)continue;
   assert(sprite.x>=ctx.track_left&&sprite.x+sprite.image.w<=ctx.track_right,'segment outside original track');
   assert(sprite.y===ctx.origin_y+Math.floor(353*ctx.scale));
   width+=sprite.image.w;visible++;
  }
  assert(width===Math.floor(22*ctx.scale),'blocks vanish at wrap boundary');
  if(visible===2)split=true;
  if(last!==undefined&&ctx.segment.x<last)wrapped=true;
  last=ctx.segment.x;
 }
 assert(split&&wrapped,'animation never tested a split wrap');
}
'''.replace('SCRIPT',json.dumps(script))
  runner='from PyQt6.QtCore import QCoreApplication\nfrom PyQt6.QtQml import QJSEngine\nimport sys\napp=QCoreApplication([]); engine=QJSEngine(); result=engine.evaluate(sys.stdin.read())\nif result.isError(): raise RuntimeError(result.toString())\n'
  result=subprocess.run([sys.executable,'-c',runner],input=harness,capture_output=True,text=True,timeout=20)
  self.assertEqual(result.returncode,0,result.stderr)
if __name__=='__main__':unittest.main()
