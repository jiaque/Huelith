from pathlib import Path
exec(Path('tools/m3_geometry_validate.py').read_text(encoding='utf-8').split('with tempfile.TemporaryDirectory() as tmp:')[0])
O=R/'docs/test-evidence/M3/interaction-optimization';O.mkdir(parents=True,exist_ok=True)
base=json.loads((R/'docs/test-evidence/M3/quality/portrait-highres-recipe.json').read_text())
source=R/'docs/test-evidence/M3/quality/portrait-highres-source.png'
def wait_preview():
 start=time.monotonic()
 while time.monotonic()-start<40:
  checked(op='pump',waitMs=20);s=checked(op='portrait_state')
  if not s['previewBusy']:return s
 raise AssertionError('preview timeout')
with tempfile.TemporaryDirectory() as tmp:
 checked(op='init',settings=tmp)
 checked(op='portrait_window',path=str(source),recipe=base)
 checked(op='portrait_state',select=0,panel=1)
 before=wait_preview();h=before['history'];params=before['recipe']['people'][0]['settings'].get('parameters',{})
 r=checked(op='portrait_state',dragParameter='feature.eyeSize',dragValues=list(range(0,61))+list(range(60,19,-1)),stepMs=30,dragCapture=str(O/'drag.png'))
 frames=r['dragFrames'];assert len({x['frames'] for x in frames})>3,frames
 assert all(x['history']==h for x in frames),frames
 r=wait_preview();assert r['history']==h+1,r['history'];assert r['displayedRevision']==r['previewRevision']
 checked(op='portrait_state',capture=str(O/'final.png'))
 undo=checked(op='portrait_state',history=-1);assert undo['recipe']['people'][0]['settings'].get('parameters',{})==params
 checked(op='portrait_state',history=1);wait_preview()
 (O/'preview.json').write_text(json.dumps({'passed':True,'samples':frames,'finalRevision':r['displayedRevision']},indent=2))
 print('Continuous drag + one-step undo PASS; displayed frames:',len({x['frames'] for x in frames}),flush=True)
 # Groups, reset, side display and non-modal inline editing.
 assert checked(op='portrait_state',control='portraitGroup_1_眼睛')['control']['checked']
 checked(op='portrait_state',click='portraitGroup_1_眼睛')
 assert not checked(op='portrait_state',control='portraitParam_feature.eyeSize')['control']['visible']
 checked(op='portrait_state',click='portraitGroup_1_眼睛',doubleClickParameter='feature.eyeSize')
 assert checked(op='portrait_state',control='portraitParam_feature.eyeSize')['control']['value']==0
 checked(op='portrait_state',history=-1)
 assert checked(op='portrait_state',control='portraitParam_feature.eyeSize')['control']['value']==20
 h=checked(op='portrait_state')['history']
 checked(op='portrait_state',linked=False,side=1)
 assert checked(op='portrait_state')['history']==h
 r=checked(op='portrait_state',liquify=True);assert not r['modal'] and r['windowIcon']
 assert r['recipe']['people'][0]['settings']['liquify']
 wait_preview();checked(op='portrait_state',capture=str(O/'liquify.png'))
 checked(op='portrait_state',panel=15)
 before_preset=checked(op='portrait_state')['recipe']
 checked(op='portrait_state',click='portraitPreviewPreset');r=wait_preview();assert not r['modal']
 checked(op='portrait_state',click='portraitPresetDiscard')
 assert checked(op='portrait_state')['recipe']==before_preset
 checked(op='portrait_state',click='portraitPreviewPreset');wait_preview()
 checked(op='portrait_state',click='portraitPresetApply');wait_preview()
 checked(op='portrait_state',history=-1)
 assert checked(op='portrait_state')['recipe']==before_preset
 print('Groups/reset/side state + inline liquify/preset + logo PASS',flush=True)
 checked(op='portrait_close')
 checked(op='window')
 checked(op='window_scene',width=320,height=240,layers=[{'name':'底层','color':'#c07050'},{'name':'中层','color':'#607050'},{'name':'顶层','color':'#405080'}])
 assert checked(op='window_state')['visibility']==[True,True,True]
 checked(op='window_select',index=0,offset=10,modifiers=0x08000000)
 assert checked(op='window_state')['visibility']==[False,False,True]
 checked(op='window_select',index=0,offset=10,modifiers=0x08000000)
 assert checked(op='window_state')['visibility']==[True,True,True]
 checked(op='window_select',index=0,offset=10,visibilityDragTo=2)
 assert checked(op='window_state')['visibility']==[False,False,False]
 checked(op='window_select',index=0,offset=10,visibilityDragTo=2)
 assert checked(op='window_state')['visibility']==[True,True,True]
 checked(op='window_capture',path=str(O/'layers.png'))
 windows=[]
 for kind in ['portrait','enhance','merge']:
  r=checked(op='work_window_probe',kind=kind,source=str(source))
  assert all(r.get(k) for k in ['windowIcon','nativeIcon','nonModal','unowned','mainEnabled','mainNotMinimized','mainEdited','childRestored','mainMinimizedIndependently']),r
  windows.append(r)
 r=checked(op='work_window_probe',kind='portraitSession',source=str(source),recipe=base,accept=True,concurrentEdit=True)
 assert r['copyHasPortrait'] and r['newDocuments']==1 and r['originalName']=='Concurrent edit retained',r
 (O/'windows.json').write_text(json.dumps({'windows':windows,'conflict':r},indent=2))
 print('Layer eye/solo/drag + Windows native icons/minimize + conflict copy PASS',flush=True)
 checked(op='shutdown')
