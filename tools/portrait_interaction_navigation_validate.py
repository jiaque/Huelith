"""Mixed tool transitions and sustained slider rendering."""
from pathlib import Path
exec(compile(Path('tools/portrait_interaction_review_validate.py').read_text().split("try:\n # Deformation")[0],__file__,'exec'))
try:
 open_window();call(op='portrait_state',workspaceTask=8,bodyMode=2);wait()
 call(op='portrait_state',click='portraitBodyMaterial_collarbone');s=wait();collar=s['recipe']['people'][0]['settings']['bodyLinesQuad']
 call(op='portrait_state',click='portraitBodyMaterial_waist');s=wait();waist=s['recipe']['people'][0]['settings']['bodyLinesQuad'];assert waist!=collar
 call(op='portrait_state',click='portraitBodyMaterial_collarbone');s=wait();assert s['recipe']['people'][0]['settings']['bodyLinesQuad']==collar
 call(op='portrait_state',click='portraitBodyMaterial_abdomen');s=wait();assert s['recipe']['people'][0]['settings']['bodyLinesQuad']!=collar
 call(op='portrait_state',workspaceTask=3,panel=1);wait()
 call(op='portrait_state',dragParameter='feature.eyeSize',dragValues=list(range(0,31,2))+list(range(30,-1,-2)),stepMs=60)
 s=wait();frames=s['dragFrames'];assert len({f['frames'] for f in frames})>2,frames
 assert len({tuple(f['viewport']) for f in frames})==1
 print('Slider frames and timings',len({f['frames'] for f in frames}),s['previewTimings'],flush=True)
 call(op='portrait_state',workspaceTask=2);wait();group(5)
 call(op='portrait_state',parameter='makeup.mole',value=50);wait()
 call(op='portrait_state',click='portraitCanvasHand');wait();call(op='portrait_state',click='portraitCanvasBrush');wait()
 call(op='portrait_state',workspaceTask=5);wait();call(op='portrait_state',workspaceTask=9);wait()
 call(op='portrait_state',click='portraitCanvasFreeze0',brushSize=60,paintPath=[[.5,.5],[.51,.5]]);s=wait();assert s['recipe']['people'][0]['settings'].get('liquify')
 call(op='portrait_state',workspaceTask=1,hairPage=2);wait()
 call(op='portrait_state',click='portraitHairColor_402b23');wait();assert call(op='portrait_state',control='portraitHairColor_402b23')['control']['checked']
 call(op='portrait_state',click='portraitHairColor_6b4034');wait();assert call(op='portrait_state',control='portraitHairColor_6b4034')['control']['checked'];assert not call(op='portrait_state',control='portraitHairColor_402b23')['control']['checked']
 call(op='portrait_state',workspaceTask=12,paintType=1);wait();call(op='portrait_state',click='portraitPaintColor0');wait();assert call(op='portrait_state',control='portraitPaintColor0')['control']['checked']
 for amount in [-100,100]:
  rr=copy.deepcopy(base);ss=rr['people'][0]['settings'];ss['browGeometryVersion']=2;ss['parameters']={'feature.browTilt.left':amount,'feature.browTilt.right':amount,'feature.eyeSize.left':10,'feature.eyeSize.right':10}
  call(op='portrait_geometry',recipe=rr,source=str(source),size=size,render=str(OUT/f'brow-combined-{amount}.png'))
 print('Body material positions, stable dragging and cross-tool continuation passed',flush=True)
finally:call(op='shutdown')
