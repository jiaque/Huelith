import ctypes,json,os,tempfile,time,traceback
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
os.environ['QT_PLUGIN_PATH']=str(qt/'plugins');os.environ['QT_QPA_PLATFORM']='windows'
dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
out=ROOT/'docs/test-evidence/M2/navigation';out.mkdir(parents=True,exist_ok=True);results=[]
def call(op,**kw):
 r=json.loads(fn(json.dumps(dict(op=op,**kw),ensure_ascii=False).encode()))
 if 'error' in r:raise RuntimeError(r['error'])
 return r
def test(name,body):
 try:results.append(dict(name=name,passed=True,detail=body()))
 except Exception:results.append(dict(name=name,passed=False,error=traceback.format_exc()))
 print(results[-1],flush=True)
def state():return call('window_interaction_state')
def control(name,value=True):return call('window_control',name=name,value=value)
def click(modifiers=0):
 for phase in ('press','release'):call('window_mouse',phase=phase,space='widget',x=350,y=250,modifiers=modifiers)
def key(key,**kw):return call('window_key',key=key,**kw)
def pref(enabled,accept=True,center=False):
 return call('window_action',text='工具与画面操控…',dialog=dict(wheelZoomPreference=enabled,centerZoomPreference=center,accept=accept,capture=str(out/'preferences.png')))
with tempfile.TemporaryDirectory(prefix='huelith-navigation-') as folder:
 call('init',settings=folder);call('window');call('window_scene',width=6000,height=4000,layers=[dict(name='24MP source',color='#546e82',width=6000,height=4000)])
 revision=state()['revision'];call('window_chrome',click=11)
 def directions():
  call('window_view',zoom=1);control('zoomOutMode');click();assert state()['zoom']==.5
  click(0x08000000);assert state()['zoom']==1
  control('zoomInMode');click();assert state()['zoom']==2
  click(0x08000000);assert state()['zoom']==1
  options=call('window_chrome')['options'];assert next(x for x in options if x['name']=='zoomPercent')['value']==100
  call('window_capture',path=str(out/'zoom-options.png'))
 test('zoom_in_out_modes_alt_reverse_percent',directions)
 def wheels():
  pref(False);call('window_view',zoom=1);call('window_wheel');assert state()['zoom']==1
  call('window_wheel',modifiers=0x08000000);assert abs(state()['zoom']-1.2)<1e-8
  pref(True,accept=False);call('window_view',zoom=1);call('window_wheel');assert state()['zoom']==1
  pref(True);call('window_wheel');assert abs(state()['zoom']-1.2)<1e-8
  call('window_wheel',delta=-120);assert abs(state()['zoom']-1)<1e-8
  call('window_wheel',modifiers=0x08000000);assert state()['zoom']==1
 test('wheel_preference_commit_cancel_both_directions_alt_pan',wheels)
 def hand():
  control('zoomInMode');old=state()['zoom'];key('Space')
  for phase,x in (('press',300),('move',370),('release',370)):call('window_mouse',phase=phase,space='widget',x=x,y=250)
  key('Space',release=True);assert state()['zoom']==old
  key('Ctrl+0');assert state()['zoom']<1
  key('Ctrl+1');assert state()['zoom']==1
 test('space_pan_while_zoom_selected_and_fit_actual_shortcuts',hand)
 def perf():
  r=call('window_navigation_frames',zooms=[1,2,4,8,16,32]*6)
  assert max(r['frameMs'])<500,r
  assert state()['revision']==revision
  for _ in range(12):click()
  assert state()['zoom']==32
  control('zoomPercent',100);assert state()['zoom']==1
  return dict(frames=len(r['frameMs']),maximum_ms=max(r['frameMs']),mean_ms=sum(r['frameMs'])/len(r['frameMs']),limits='100%-3200%',frame_ms=r['frameMs'])
 test('24mp_36_repaints_repeated_clicks_bound_zoom_no_document_edits',perf)
 def actual_raw():
  preview=Path(folder)/'r10.png'
  developed=call('raw_develop',path=str(ROOT/'research/m0/samples/r10-5871.CR3'),denoise=False,preview=str(preview))
  assert (developed['width'],developed['height'])==(6000,4000)
  call('window_scene',width=6000,height=4000,layers=[dict(name='R10 5871',path=str(preview))]);call('window_chrome',click=11)
  result=perf()
  call('window_view',zoom=32,rotation=30)
  call('window_capture',path=str(out/'r10-3200-rotated.png'))
  key('Ctrl+0')
  assert call('window_navigation_frames',zooms=[])['fits']
  call('window_capture',path=str(out/'r10-fit.png'))
  return result
 test('r10_real_24mp_display_zoom_rotation_and_fit',actual_raw)
 pref(False);call('shutdown')
(out/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
raise SystemExit(not all(r['passed'] for r in results))
