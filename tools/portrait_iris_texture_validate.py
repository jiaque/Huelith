from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-iris-texture';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());p=recipe['people'][0];s=p['settings'];s['irisMaterials']=call(op='portrait_geometry',repairAnalysis=True,source=str(source),person=p)['irisMaterials'];s['irisMaterialVersion']=2
size=list(Image.open(source).size);renders=[]
for value in [0,100]:
    s['parameters']={'makeup.irisTexture.right':value};out=OUT/f'texture-{value}';call(op='portrait_geometry',source=str(source),recipe=recipe,size=size,render=str(out.with_suffix('.png')),linearRender=str(out.with_suffix('.bin')));renders.append(np.fromfile(out.with_suffix('.bin'),np.float32).reshape(size[1],size[0],4))
a,b=renders;delta=np.max(abs(a-b),axis=2);assert delta.max()>1e-4
# Texture changes brightness, never color ratios or alpha. It works with pigment at zero.
valid=a[:,:,1]>.001;assert np.max(abs((a[:,:,0]/np.maximum(a[:,:,1],.001)-b[:,:,0]/np.maximum(b[:,:,1],.001))[valid]))<2e-5
assert np.array_equal(a[:,:,3],b[:,:,3])
mask=OUT/'scope.png';ms=[round(v*768/max(size)) for v in size];call(op='portrait_geometry',recipe=recipe,size=ms,styleMask='makeup.irisTexture.right',mask=str(mask));small=np.asarray(Image.open(mask));m=small[(np.arange(size[1])*ms[1]/size[1]).astype(int)[:,None],(np.arange(size[0])*ms[0]/size[0]).astype(int)[None,:]];assert np.max(delta[m==0],initial=0)<2e-6

def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=20);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])
s['parameters']={};call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=9);call(op='portrait_state',styleSection=1);before=wait()['recipe']
call(op='portrait_state',parameter='makeup.irisTexture',value=75);chosen=wait()['recipe'];assert chosen['people'][0]['settings']['parameters'].get('makeup.iris.left',0)==0
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);assert wait()['recipe']==chosen
r=call(op='portrait_state',source=str(source),render=str(OUT/'combined.png'),checkRegion=True,save=str(OUT/'texture.huelith'));assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=chosen);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'combined.png')),np.array(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Independent iris texture preserves chroma, outside area and alpha; undo, ROI and reopening passed')
