from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-repair-source-drag-geometry';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());p=recipe['people'][0];p['settings']['parameters']={};x,y,w,h=p['face'];target=[x+w*.5,y+h*.25]
p['settings']['repairs']=[dict(version=1,kind='heal',id=str(i),points=[[target[0],target[1]+i*.02]],offsetX=.025,offsetY=0,radius=.004,feather=.5,opacity=.7) for i in range(2)]
p['settings']['repairs'][0]['sourceOffsets']=[[.025,0],[.0255,0]]
p['settings']['repairs'][0]['points'].append([target[0]+.003,target[1]])

def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=20);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])
recipe=call(op='portrait_geometry',headAnalysis=True,recipe=recipe)['recipe'];recipe['people'][0]['settings']['headGeometryVersion']=2;recipe['people'][0]['settings']['parameters']={'head.roll':70}
size=list(Image.open(source).size);r=call(op='portrait_geometry',recipe=recipe,size=size,field=str(OUT/'field.bin'));field=np.fromfile(OUT/'field.bin',np.float32).reshape(r['meshSize'][1],r['meshSize'][0],4)[:,:,:2]
call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=7);wait();call(op='portrait_state',repairSelect=0);before=wait()['recipe'];call(op='portrait_state',capture=str(OUT/'source-overlay.png'),captureWidget='portraitView')
path=[[target[0]+.025+i*.001,target[1]+i*.0005] for i in range(11)];call(op='portrait_state',paintPath=forward_points(field,np.array(path)).tolist());after=wait()['recipe'];a=after['people'][0]['settings']['repairs'];b=before['people'][0]['settings']['repairs'];assert len(a)==2 and a[1]==b[1] and abs(a[0]['offsetX']-.035)<.002,(a,b)
assert abs(a[0]['sourceOffsets'][1][0]-a[0]['sourceOffsets'][0][0]-.0005)<1e-10
assert abs(a[0]['sourceOffsets'][0][0]-a[0]['offsetX'])<1e-10
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);wait();r=call(op='portrait_state',source=str(source),render=str(OUT/'result.png'),checkRegion=True,save=str(OUT/'source.huelith'));assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='shutdown');print('Source drag changes selected stroke only; one undo, saved source, ROI passed.')
