"""Numeric brush controls must drive the tool and survive module changes."""
from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],str(Path('tools/portrait_freeze_workflow_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/liquify-controls';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'settings'))
call(op='portrait_window',path=str(source),recipe=recipe);wait()
call(op='portrait_state',select=0,panel=4,portraitLiquifySize=150,portraitLiquifyHardness=35,portraitLiquifyPressure=25)
call(op='portrait_state',panel=0);call(op='portrait_state',panel=4)
r=call(op='portrait_state',brushControls=True)
for key,value in [('Size',150),('Hardness',35),('Pressure',25)]:assert r['brushControls']['portraitLiquify'+key]==[value,value],r['brushControls']
for key in ['Size','Hardness','Pressure']:assert r['brushControls']['portraitLiquify'+key+'Count']==1
fx,fy,fw,fh=recipe['people'][0]['face'];center=np.array([fx+fw*.5,fy+fh*.6])
records=[];zoom_fields=[]
for focus in [0,3]:
 call(op='portrait_state',click='portraitFocus'+str(focus));wait()
 r=call(op='portrait_state',hover=center.tolist(),brushControls=True)
 expected=150*r['previewWidth']/size[0]
 assert abs(r['brushCircleSceneDiameter']-expected)<1e-6,(expected,r)
 records.append(dict(focus=focus,zoom=r['zoom'],sceneDiameter=r['brushCircleSceneDiameter'],screenDiameter=r['brushCircleScreenDiameter']))
 call(op='portrait_state',liquifyTool=5,paint=center.tolist());zoom_fields.append(field('zoom-'+str(focus)))
 call(op='portrait_state',history=-1);wait()
zoom_error=float(np.max(np.linalg.norm((zoom_fields[0]-zoom_fields[1])*(size-1),axis=2)))
assert zoom_error<.5,zoom_error
call(op='portrait_state',click='portraitFocus0');wait()
fields={}
for pressure,hardness in [(0,0),(25,0),(75,0),(25,100)]:
 call(op='portrait_state',portraitLiquifyPressure=pressure,portraitLiquifyHardness=hardness,liquifyTool=5)
 before=wait();call(op='portrait_state',paint=center.tolist());state=wait()
 fields[(pressure,hardness)]=field(f'pressure-{pressure}-hardness-{hardness}')
 if not pressure:assert state['recipe']==before['recipe'] and state['history']==before['history']
 else:call(op='portrait_state',history=-1);wait()
ratio_error=float(np.max(np.linalg.norm((fields[(75,0)]-3*fields[(25,0)])*(size-1),axis=2)))
assert ratio_error<.01,ratio_error
probe=center+np.array([50/size[0],0])
soft=float(np.linalg.norm(sample(fields[(25,0)],probe[None,:])*(size-1)))
hard=float(np.linalg.norm(sample(fields[(25,100)],probe[None,:])*(size-1)))
assert hard>soft*2 and soft>0,(soft,hard)
for key,default in [('Size',max(80,min(500,int(size[0]/12)))),('Hardness',40),('Pressure',50)]:
 r=call(op='portrait_state',doubleClickControl='portraitLiquify'+key,brushControls=True)
 assert r['brushControls']['portraitLiquify'+key]==[default,default]
call(op='portrait_state',capture=str(OUT/'controls.png'))
result=dict(zoom=records,zoomFieldDifferencePixels=zoom_error,pressureRatioErrorPixels=ratio_error,softFringePixels=soft,hardFringePixels=hard)
(OUT/'results.json').write_text(json.dumps(result,indent=2));call(op='portrait_close');call(op='shutdown');print(result)
