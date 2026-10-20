"""Natural UI brush entry: measure the target, not merely saved strokes."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,hashlib
OUT=ROOT/'docs/test-evidence/M3/brush-target';OUT.mkdir(exist_ok=True)
BASE=ROOT/'docs/test-evidence/M3/quality';source=BASE/'leroy-chiao-source.png'
base=json.loads((BASE/'leroy-chiao-recipe.json').read_text());size=np.array(Image.open(source).size)
for p in base['people']:
    p['settings']['parameters']={};p['settings'].pop('liquify',None);p['settings']['repairs']=[]
face=base['people'][0]['face'];fx,fy,fw,fh=face
def point(x,y):return [fx+fw*x,fy+fh*y]
def wait():
    until=time.monotonic()+60
    while time.monotonic()<until:
        call(op='pump',waitMs=20);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:
            assert r['displayedRevision']==r['previewRevision'],r['status']
            return r
    raise AssertionError(r['status'])
call(op='init',settings=str(OUT/'settings'));call(op='portrait_window',path=str(source),recipe=base);wait()
call(op='portrait_state',select=0,panel=4);wait()
path=[point(.3+i*.04,.65) for i in range(6)]
call(op='portrait_state',paintPath=path);r=wait()
result=call(op='portrait_geometry',size=size.tolist(),recipe=r['recipe'],field=str(OUT/'liquify-field.bin'))
w,h=result['meshSize'];f=np.fromfile(OUT/'liquify-field.bin',np.float32).reshape(h,w,4)[:,:,:2]
probe=np.array([point(.4,.65)]);motion=float(np.linalg.norm((forward_points(f,probe)-probe)*(size-1)))
assert motion>2,('liquify missed target',motion)
call(op='portrait_state',source=str(source),render=str(OUT/'liquify.png'))
call(op='portrait_state',history=-1);wait()
def render(name):
    call(op='portrait_state',source=str(source),render=str(OUT/(name+'.png')))
    return np.asarray(Image.open(OUT/(name+'.png'))).astype(float)
def difference(a,b,target):
    x,y=(np.array(target)*size).astype(int);return float(np.abs(a[y-3:y+4,x-3:x+4,:3]-b[y-3:y+4,x-3:x+4,:3]).mean())
# Directly enter repair; no hidden activation shortcut.
call(op='portrait_state',panel=7);wait();before=render('repair-before')
call(op='portrait_state',paint=point(.25,.5),alt=True)
target=point(.65,.55);call(op='portrait_state',paintPath=[target,point(.67,.55),point(.69,.55)])
r=wait();after=render('repair-after');repair_delta=difference(before,after,target)
assert repair_delta>.1,('repair missed target',repair_delta)
strokes=copy.deepcopy(r['recipe']['people'][0]['settings']['repairs'])
call(op='portrait_state',paint=point(.3,.45),alt=True);r=wait()
assert r['recipe']['people'][0]['settings']['repairs']==strokes,'Alt unexpectedly edited previous stroke'
call(op='portrait_state',paintPath=[point(.6,.65),point(.64,.65)]);r=wait()
assert len(r['recipe']['people'][0]['settings']['repairs'])==len(strokes)+1
# Losing mouse capture commits the stroke but must keep the next Alt sample
# independent of already applied strokes, just like an ordinary release.
call(op='portrait_state',paintPath=[point(.55,.6),point(.59,.6)],paintEnd='ungrab');r=wait()
strokes=copy.deepcopy(r['recipe']['people'][0]['settings']['repairs'])
call(op='portrait_state',paint=point(.3,.45),alt=True);r=wait()
assert r['recipe']['people'][0]['settings']['repairs']==strokes,'Alt edited interrupted stroke'
call(op='portrait_state',paintPath=[point(.6,.65),point(.64,.65)]);r=wait()
assert len(r['recipe']['people'][0]['settings']['repairs'])==len(strokes)+1
# Missing-hair target is bare forehead; clone must not be clipped to existing hair.
call(op='portrait_state',panel=12);wait();call(op='portrait_state',styleTab=2);wait()
before=render('hair-before');call(op='portrait_state',paint=point(.5,-.05),alt=True)
target=point(.5,.22);call(op='portrait_state',paintPath=[target,point(.53,.22),point(.56,.22)])
r=wait();after=render('hair-after');hair_delta=difference(before,after,target)
assert hair_delta>1,('hair clone missed bare target',hair_delta)
# Repeating an opaque clone must replace pixels, never add the same negative
# color delta again. Compare the fully covered center, plus the sampled hair.
single_recipe=copy.deepcopy(r['recipe'])
single_recipe['people'][0]['settings']['repairs'][-1]['opacity']=1
call(op='portrait_geometry',size=size.tolist(),recipe=single_recipe,source=str(source),render=str(OUT/'hair-opaque.png'))
opaque=np.asarray(Image.open(OUT/'hair-opaque.png')).astype(float)
for repeats in (1, 4):
    repeated=copy.deepcopy(single_recipe)
    strokes=repeated['people'][0]['settings']['repairs']
    strokes.extend([copy.deepcopy(strokes[-1]) for _ in range(repeats)])
    path=OUT/f'hair-repeat-{repeats}.png'
    call(op='portrait_geometry',size=size.tolist(),recipe=repeated,source=str(source),render=str(path))
    pixels=np.asarray(Image.open(path)).astype(float)
    assert difference(opaque,pixels,target)<1,('overlapping clone darkens target',repeats)
sample_point=point(.5,-.05)
assert difference(before,after,target)>1
tx,ty=(np.array(target)*size).astype(int);sx,sy=(np.array(sample_point)*size).astype(int)
assert np.max(abs(opaque[ty,tx,:3]-before[sy,sx,:3]))<8,'clone must match sampled hair color'
count=len(r['recipe']['people'][0]['settings']['repairs'])
call(op='portrait_state',panel=0);wait();call(op='portrait_state',panel=12);wait()
call(op='portrait_state',paint=point(.45,-.05),alt=True)
call(op='portrait_state',paintPath=[point(.65,.22),point(.68,.22)]);r=wait()
assert len(r['recipe']['people'][0]['settings']['repairs'])==count+1,'reentering hair lost brush activation'
saved=call(op='portrait_state',source=str(source),render=str(OUT/'final.png'),checkRegion=True,save=str(OUT/'brushes.huelith'))
assert saved['roundtrip'] and saved['regionError']<2e-6
call(op='portrait_close')
# Automatic repair must work without first teaching a source using Alt.
test_image=Image.open(source).convert('RGBA');draw=ImageDraw.Draw(test_image)
target=point(.68,.58);x,y=(np.array(target)*size).astype(int)
draw.ellipse((x-4,y-4,x+4,y+4),fill=(32,20,16,255))
blemish=OUT/'automatic-blemish-source.png';test_image.save(blemish)
call(op='portrait_window',path=str(blemish));imported=wait();call(op='portrait_close')
recipe=copy.deepcopy(base);recipe['sourceHash']=imported['recipe']['sourceHash']
call(op='portrait_window',path=str(blemish),recipe=recipe);wait();call(op='portrait_state',select=0,panel=7);wait()
call(op='portrait_state',brushSize=28)
call(op='portrait_state',paintPath=[target,point(.69,.58)])
r=wait();assert len(r['recipe']['people'][0]['settings']['repairs'])==1,r['status']
call(op='portrait_state',source=str(blemish),render=str(OUT/'automatic-blemish-after.png'))
automatic=np.asarray(Image.open(OUT/'automatic-blemish-after.png')).astype(float)
automatic_delta=difference(np.asarray(test_image).astype(float),automatic,target)
assert automatic_delta>5,('automatic repair failed to change dark spot',automatic_delta)
(OUT/'results.json').write_text(json.dumps(dict(liquifyTargetPixels=motion,repairTargetMeanDifference=repair_delta,hairTargetMeanDifference=hair_delta,automaticBlemishMeanDifference=automatic_delta),indent=2))
call(op='portrait_close');call(op='shutdown');print('Natural brush entry, target effects, Alt resampling and repeated hair entry passed.')
