"""M2 regression through the existing production C++ DLL; no test executable."""
import argparse,ctypes,json,os,tempfile,traceback
from pathlib import Path
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1]
ALT=0x08000000

def main():
 parser=argparse.ArgumentParser();parser.add_argument('--build',default='windows-release');parser.add_argument('--platform',default='windows');args=parser.parse_args()
 qt=Path(r'D:\Program Files\Qt\6.10.3\msvc2022_64');handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build'/args.build))]
 os.environ['QT_PLUGIN_PATH']=str(qt/'plugins');os.environ['QT_QPA_PLATFORM']=args.platform
 lib=ctypes.CDLL(str(ROOT/'build'/args.build/'huelith_python_driver.dll'));fn=lib.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
 results=[];commands=[];out=ROOT/'docs/test-evidence/M2'/args.platform;out.mkdir(parents=True,exist_ok=True)
 def call(op,**kw):
  request=dict(op=op,**kw);result=json.loads(fn(json.dumps(request,ensure_ascii=False).encode()));commands.append(dict(request=request,result=result))
  if 'error' in result:raise RuntimeError(result['error'])
  return result
 def key(value):call('pump',waitMs=30);return call('window_key',key=value)
 def mouse(phase,x,y,modifiers=0):return call('window_mouse',phase=phase,x=x,y=y,modifiers=modifiers)
 def click(x,y,modifiers=0):mouse('press',x,y,modifiers);mouse('release',x,y,modifiers)
 def drag(x,y,ex,ey):mouse('press',x,y);mouse('move',ex,ey);mouse('release',ex,ey)
 def pixel(x,y):return call('window_probe',x=x,y=y)['rgba']
 def state():return call('window_interaction_state')
 def control(name,value):return call('window_control',name=name,value=value)
 def tool(index):return call('window_chrome',click=index)
 def test(name,body):
  print('RUN',name,flush=True)
  try:body();results.append(dict(name=name,passed=True))
  except Exception:results.append(dict(name=name,passed=False,error=traceback.format_exc()));print(results[-1]['error'],flush=True)
 with tempfile.TemporaryDirectory(prefix='huelith-m2-') as folder:
  folder=Path(folder);call('init',settings=str(folder));call('window')
  sample=Image.new('RGB',(240,180),'white');d=ImageDraw.Draw(sample);d.rectangle((20,30,59,79),fill='red');d.rectangle((140,30,179,79),fill='red');path=folder/'sample.png';sample.save(path)
  def scene():
   call('window_scene',width=240,height=180,layers=[dict(name='pixels',path=str(path))]);key('D');control('brushHardness',100);control('brushOpacity',100);control('brushFlow',100);control('brushRadius',6)
  def palette():
   scene();buttons=call('window_chrome')['buttons'];assert len(buttons)==25 and len({b['x'] for b in buttons})==2
   for index in range(25):tool(index);assert state()['tool']==index and state()['checkedTools']==1
  test('palette_25_tools_mutually_exclusive',palette)
  def bucket():
   scene();key('G');control('rangeTolerance',0);control('rangeContiguous',True);before=state()['revision'];click(30,50);assert pixel(30,50)[:3]==[0,0,0] and pixel(150,50)[:3]==[255,0,0];assert state()['revision']==before+1;key('Ctrl+Z');assert pixel(30,50)[:3]==[255,0,0]
   control('rangeContiguous',False);click(30,50);assert pixel(150,50)[:3]==[0,0,0]
  test('bucket_contiguous_global_undo',bucket)
  def constrained():
   scene();key('M');drag(20,30,40,60);key('G');control('rangeContiguous',False);click(30,50);assert pixel(30,50)[:3]==[0,0,0] and pixel(50,50)[:3]==[255,0,0] and pixel(150,50)[:3]==[255,0,0]
  test('bucket_respects_selection',constrained)
  def eyedropper():
   scene();key('I');click(30,50);assert state()['foreground']=='#ff0000';click(100,100,ALT);assert state()['background']=='#ffffff'
  test('eyedropper_foreground_alt_background',eyedropper)
  def tone_dialog():
   scene();before=state()['revision'];original=pixel(30,50)
   call('window_action',text='色彩平衡…',dialog=dict(toneMonochrome=True,accept=False))
   assert state()['revision']==before and pixel(30,50)==original
   call('window_action',text='色彩平衡…',dialog=dict(toneMonochrome=True,accept=True))
   changed=pixel(30,50);assert max(changed[:3])-min(changed[:3])<=1,changed
   assert state()['revision']==before+1
   key('Ctrl+Z');assert pixel(30,50)==original
  test('tone_dialog_cancel_commit_single_undo',tone_dialog)
  def grading_dialog():
   grey=folder/'grade.png';Image.new('RGB',(240,180),(100,100,100)).save(grey)
   call('window_scene',width=240,height=180,layers=[dict(name='grey',path=str(grey))])
   call('window_action',text='色彩分级…',dialog=dict(toneGradingHue=0,toneGradingSaturation=100,accept=True))
   value=pixel(100,100);assert value[0]>value[1] and value[0]>value[2],value
   key('Ctrl+Z');assert pixel(100,100)[:3]==[100,100,100]
  test('grading_menu_controls_affect_pixels_undo',grading_dialog)
  def clone():
   scene();call('window_action',text='绘画修复');key('S');control('photoSample',1);click(30,50,ALT);before=state()['revision'];click(90,110);assert pixel(90,110)[:3]==[255,0,0];assert state()['revision']==before+1;key('Ctrl+Z');assert pixel(90,110)[:3]==[255,255,255];key('Ctrl+Shift+Z');assert pixel(90,110)[:3]==[255,0,0]
  test('clone_alt_sample_separate_layer_one_undo',clone)
  def light():
   grey=folder/'grey.png';Image.new('RGB',(240,180),(100,100,100)).save(grey)
   call('window_scene',width=240,height=180,layers=[dict(name='grey',path=str(grey))]);control('brushOpacity',100);control('brushHardness',100);tool(21);click(100,100);assert pixel(100,100)[0]>100 and pixel(50,50)[0]==100;key('Ctrl+Z');tool(22);click(100,100);assert pixel(100,100)[0]<100
  test('dodge_burn_local_undo',light)
  def sponge():
   scene();tool(23);click(30,50);p=pixel(30,50);assert max(p[:3])-min(p[:3])<=1
  test('sponge_desaturates_selected_stroke',sponge)
  def crop():
   scene();key('C');drag(10,20,200,150);before=state()['revision'];key('Esc');assert state()['revision']==before;drag(10,20,200,150);key('Return');layers=call('window_probe')['layers'];assert layers[0]['tx']==-10 and layers[0]['ty']==-20;key('Ctrl+Z');assert call('window_probe')['layers'][0]['tx']==0
  test('crop_preview_cancel_commit_undo',crop)
  def quick_mask():
   scene();key('Q');assert state()['quickMask'];key('B');click(100,100);assert call('window_probe',x=100,y=100)['range']==0 and pixel(100,100)[:3]==[255,255,255];key('X');click(100,100);assert call('window_probe',x=100,y=100)['range']==255;key('Ctrl+Z');assert call('window_probe',x=100,y=100)['range']==0;key('Q');assert not state()['quickMask'];key('Ctrl+D')
  test('quick_mask_paint_selection_not_pixels_undo',quick_mask)
  def screen():
   scene();key('F');assert state()['screenMode']==1;key('F');assert state()['screenMode']==2;key('F');assert state()['screenMode']==0
  test('screen_modes_three_way_restore',screen)
  def capture():
   scene();key('S');call('window_capture',path=str(out/f'{args.build}-workspace.png'))
  test('native_window_capture',capture)
  call('shutdown')
 report=dict(build=args.build,platform=args.platform,passed=sum(t['passed'] for t in results),total=len(results),tests=results)
 (out/f'{args.build}-results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');(out/f'{args.build}-commands.json').write_text(json.dumps(commands,ensure_ascii=False,indent=2),encoding='utf-8')
 print(json.dumps(report,ensure_ascii=False,indent=2));return int(report['passed']!=report['total'])
if __name__=='__main__':raise SystemExit(main())
