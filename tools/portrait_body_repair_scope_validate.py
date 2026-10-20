from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-body-smooth';call(op='init',settings=str(OUT/'repair-settings'));source=ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose.jpg';size=list(Image.open(source).size);recipe=json.loads((OUT/'recipe.json').read_text());p=recipe['people'][0];p['settings']['parameters']={}
for kind in ['heal','wrinkle']:
    p['settings']['repairs']=[dict(version=1,id='body-regression',kind=kind,clone=True,enabled=True,points=[[.33,.47],[.36,.47]],offsetX=0,offsetY=.006,radius=.009,feather=.5,opacity=1)]
    renders=[]
    for version in [1,2]:
        p['settings']['smoothScopeVersion']=version;path=OUT/f'body-{kind}-scope{version}.png';r=call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),render=str(path),colorMetrics=True);assert r['changedPixels']>20,r;renders.append(np.asarray(Image.open(path)))
    assert np.array_equal(*renders),kind
call(op='shutdown');print('Body healing and wrinkle strokes remain independent of face smoothing scope.')
