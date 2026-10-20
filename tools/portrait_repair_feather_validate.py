from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-repair-feather';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());p=recipe['people'][0];p['settings']['parameters']={};x,y,w,h=p['face'];target=[x+w*.5,y+h*.25]
p['settings']['repairs']=[dict(version=1,kind='heal',id=str(i),points=[[target[0],target[1]+i*.02]],offsetX=.025,offsetY=0,radius=.004,feather=.5,opacity=.7) for i in range(2)]
def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=20);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=7);wait();call(op='portrait_state',repairSelect=0);before=wait()['recipe'];call(op='portrait_state',capture=str(OUT/'source-overlay.png'),captureWidget='portraitView')
call(op='portrait_state',source=str(source),render=str(OUT/'before.png'))
call(op='portrait_state',repairFeather=90);after=wait()['recipe'];a=after['people'][0]['settings']['repairs'];b=before['people'][0]['settings']['repairs'];assert a[0]['feather']==.9 and a[1]==b[1]
expected=dict(b[0]);expected['feather']=.9;assert a[0]==expected
call(op='portrait_state',source=str(source),render=str(OUT/'after.png'));aimg=np.array(Image.open(OUT/'before.png')).astype(float);bimg=np.array(Image.open(OUT/'after.png')).astype(float);assert np.max(abs(aimg-bimg))>0
yy,xx=np.mgrid[:aimg.shape[0],:aimg.shape[1]];outside=np.hypot(xx-target[0]*aimg.shape[1],yy-target[1]*aimg.shape[0])>b[0]['radius']*min(aimg.shape[:2])+2;assert np.max(abs(aimg-bimg)[outside])==0
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);assert wait()['recipe']==after
r=call(op='portrait_state',source=str(source),render=str(OUT/'feather.png'),checkRegion=True,save=str(OUT/'feather.huelith'));assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='shutdown');print('Selected feather updates only selected stroke; undo, ROI and save passed.')
