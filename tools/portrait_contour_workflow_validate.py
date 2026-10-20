"""Known detections isolate contour UI/render/save behaviour from model startup."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
call(op='init',settings=str(OUT/'workflow-settings'))
source=ROOT/'docs/test-evidence/M3/quality/portrait-highres-source.png'
base=json.loads((ROOT/'docs/test-evidence/M3/quality/portrait-highres-recipe.json').read_text())
def wait():
    end=time.monotonic()+45
    while time.monotonic()<end:
        call(op='pump',waitMs=20);s=call(op='portrait_state')
        if s['ready'] and not s['previewBusy']:return s
    raise AssertionError(s['status'])
call(op='portrait_window',path=str(source),recipe=base)
wait();call(op='portrait_state',select=0,panel=2);wait()
call(op='portrait_state',parameter='head.midface',value=10);standalone=wait()
assert standalone['recipe']['people'][0]['settings'].get('eyeGeometryVersion',1)==base['people'][0]['settings'].get('eyeGeometryVersion',1)
call(op='portrait_state',history=-1);wait()
call(op='portrait_state',panel=0);before=wait()
r=call(op='portrait_state',dragParameter='face.width',dragValues=list(range(0,51,2))+list(range(50,-31,-2)),stepMs=25)
after=wait();frames=r['dragFrames']
assert after['recipe']['people'][0]['settings']['contourVersion']==2
assert after['history']==before['history']+1
assert len({str(f['viewport']) for f in frames})==1
assert len({f['frames'] for f in frames})>3
assert after['displayedRevision']==after['previewRevision']
saved=call(op='portrait_state',source=str(source),render=str(OUT/'workflow-edited.png'),checkRegion=True,save=str(OUT/'contour.huelith'))
assert saved['roundtrip'] and saved['regionError']<2e-6
recipe=after['recipe']
call(op='portrait_state',history=-1);old=wait()
assert old['recipe']['people'][0]['settings'].get('contourVersion',1)==1
call(op='portrait_state',history=1);assert wait()['recipe']==recipe
call(op='portrait_state',doubleClickParameter='face.width');zero=wait()
assert zero['recipe']['people'][0]['settings']['parameters']['face.width']==0
for key in ['face.small','face.vShape']:
    call(op='portrait_state',parameter=key,value=40);state=wait()
    assert state['recipe']['people'][0]['settings']['parameters'][key]==40
    assert state['recipe']['people'][0]['settings']['parameters']['face.width']==0
    call(op='portrait_state',doubleClickParameter=key);state=wait()
    assert state['recipe']['people'][0]['settings']['parameters'][key]==0
call(op='portrait_state',parameter='face.small',value=20);wait()
call(op='portrait_state',parameter='face.vShape',value=20);wait()
for key,value in [('face.jawAngle',20),('face.chinWidth',-20),('face.smoothContour',35)]:
    call(op='portrait_state',parameter=key,value=value);wait()
call(op='portrait_state',panel=1,linked=False,side=0);eyeBefore=wait()
eyeDrag=call(op='portrait_state',dragParameter='feature.eyeSize',dragValues=list(range(0,51,2))+list(range(50,19,-2)),stepMs=25)
eyeAfter=wait();eyeSettings=eyeAfter['recipe']['people'][0]['settings']
assert eyeSettings['eyeGeometryVersion']==2 and 'eyeglassFrame' in eyeSettings['automaticMasks']
assert eyeSettings['parameters']['feature.eyeSize.left']==20
assert eyeSettings['parameters'].get('feature.eyeSize.right',0)==0
assert eyeAfter['history']==eyeBefore['history']+1
assert len({f['frames'] for f in eyeDrag['dragFrames']})>3
eyeSaved=call(op='portrait_state',source=str(source),render=str(OUT/'workflow-eye.png'),checkRegion=True,save=str(OUT/'eye.huelith'))
assert eyeSaved['roundtrip'] and eyeSaved['regionError']<2e-6
for key,value in [('eyeLength',25),('eyeAperture',20),('eyeInnerCorner',10),('eyeOuterCorner',10),('eyeUpperLift',15),('eyeLowerLid',15),('eyeOuterLift',20)]:
    call(op='portrait_state',parameter='feature.'+key,value=value);state=wait()
    assert state['recipe']['people'][0]['settings']['parameters']['feature.'+key+'.left']==value
    assert state['recipe']['people'][0]['settings']['parameters'].get('feature.'+key+'.right',0)==0
for key,value in [('feature.noseRootWidth',15),('feature.noseBridgeWidth',15),('feature.noseTipWidth',15),('feature.noseSize',10),('feature.noseWidth',35),('feature.noseLength',-20),('feature.lipSize',20),('feature.lipThickness',30),('feature.smile',20)]:
    call(op='portrait_state',parameter=key,value=value);wait()
combined=wait()['recipe'];settings=combined['people'][0]['settings']
assert settings['noseGeometryVersion']==2 and settings['mouthGeometryVersion']==2 and settings['mouthTopologyVersion']==2
call(op='portrait_state',linked=False,side=0)
browBefore=wait()
call(op='portrait_state',parameter='feature.browArchHeight',value=30)
browAfter=wait()
assert browAfter['recipe']['people'][0]['settings']['browGeometryVersion']==2
assert browAfter['recipe']['people'][0]['settings']['parameters']['feature.browArchHeight.left']==30
assert browAfter['recipe']['people'][0]['settings']['parameters'].get('feature.browArchHeight.right',0)==0
call(op='portrait_state',history=-1)
assert wait()['recipe']==browBefore['recipe']
call(op='portrait_state',history=1);wait()
call(op='portrait_state',doubleClickParameter='feature.browArchHeight');wait()
assert wait()['recipe']['people'][0]['settings']['parameters']['feature.browArchHeight.left']==0
for key,value in [('feature.browLength',20),('feature.browHeadSpace',15),('feature.browArchPosition',20),('feature.browThickness',25)]:
    call(op='portrait_state',parameter=key,value=value);wait()
for key in ['lipWidth','lipHeight','lipOffset','lipTilt','lipUpper','lipLower','lipCupid']:
    call(op='portrait_state',parameter='feature.'+key,value=15);state=wait()
    assert state['recipe']['people'][0]['settings']['parameters']['feature.'+key]==15
call(op='portrait_state',panel=2);wait()
for key in ['midface','philtrum','lowerface']:
    call(op='portrait_state',parameter='head.'+key,value=15);state=wait()
    assert state['recipe']['people'][0]['settings']['proportionGeometryVersion']==2
call(op='portrait_state',parameter='head.roll',value=20);wait()
call(op='portrait_state',parameter='head.size',value=-20);state=wait()
assert state['recipe']['people'][0]['settings']['headGeometryVersion']==2
assert 'head' in state['recipe']['people'][0]['settings']['automaticMasks']
call(op='portrait_state',panel=3);wait()
call(op='portrait_state',parameter='head.size',value=-25);state=wait()
assert state['recipe']['people'][0]['settings']['parameters']['head.size']==-25
assert 'head.proportion' not in state['recipe']['people'][0]['settings']['parameters']
call(op='portrait_state',workspaceTask=3,panel=2);wait()
assert call(op='portrait_state',control='portraitParam_face.forehead')['control']['visible']
call(op='portrait_state',parameter='face.forehead',value=20);state=wait()
assert state['recipe']['people'][0]['settings']['parameters']['face.forehead']==20
assert state['recipe']['people'][0]['settings']['scalpGeometryVersion']==2
call(op='portrait_state',panel=2);wait()
for key,value in [('head.crown',15),('head.hairline',-15),('head.hairVolume',25)]:
    call(op='portrait_state',parameter=key,value=value);state=wait()
    assert state['recipe']['people'][0]['settings']['parameters'][key]==value
combined=wait()['recipe']
combinedSaved=call(op='portrait_state',source=str(source),render=str(OUT/'workflow-combined.png'),checkRegion=True,save=str(OUT/'combined.huelith'))
assert combinedSaved['roundtrip'] and combinedSaved['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=combined);wait()
call(op='portrait_state',source=str(source),render=str(OUT/'workflow-combined-reopened.png'))
assert np.array_equal(np.asarray(Image.open(OUT/'workflow-combined.png')),np.asarray(Image.open(OUT/'workflow-combined-reopened.png')))
call(op='portrait_close')
call(op='portrait_window',path=str(source),recipe=recipe);wait()
call(op='portrait_state',source=str(source),render=str(OUT/'workflow-reopened.png'))
assert np.array_equal(np.asarray(Image.open(OUT/'workflow-edited.png')),np.asarray(Image.open(OUT/'workflow-reopened.png')))
(OUT/'workflow.json').write_text(json.dumps({'frames':frames,'eyeFrames':eyeDrag['dragFrames'],'passed':True},indent=2))
call(op='portrait_close');call(op='shutdown')
print('Contour live preview, stable layout, undo/version, reset and reopen passed.')
