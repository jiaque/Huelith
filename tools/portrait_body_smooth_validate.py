import os
from pathlib import Path
ROOT=Path.cwd();temp=ROOT/'build/portrait-ai-tmp';temp.mkdir(exist_ok=True)
os.environ['TMP']=os.environ['TEMP']=str(temp);os.environ['HUELITH_AI_WORKER_EXE']=str(ROOT/'build/windows-release/Huelith.exe')
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-body-smooth';OUT.mkdir(parents=True,exist_ok=True);call(op='init',settings=str(OUT/'settings'))
source=ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose.jpg';size=list(Image.open(source).size)
def wait():
    end=time.monotonic()+90
    while time.monotonic()<end:
        call(op='pump',waitMs=30);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy'] and r['count']>0:return r
    raise AssertionError(r['status'])
call(op='portrait_window',path=str(source),models=str(ROOT/'build/windows-release/models'));state=wait();recipe=state['recipe'];p=recipe['people'][0]
assert 'faceSkin' in p['settings']['automaticMasks'] and 'bodySkin' in p['settings']['automaticMasks']
(OUT/'recipe.json').write_text(json.dumps(recipe))
call(op='portrait_state',select=0,panel=5);wait()
for key in ['skin.smooth','skin.bodySmooth']:
    before=wait()['recipe'];call(op='portrait_state',parameter=key,value=45);state=wait()
    if key=='skin.smooth':assert state['recipe']['people'][0]['settings']['smoothScopeVersion']==2
    call(op='portrait_state',history=-1);assert wait()['recipe']==before
    call(op='portrait_state',history=1);wait();call(op='portrait_state',doubleClickParameter=key);wait()
call(op='portrait_state',parameter='skin.smooth',value=60);wait();call(op='portrait_state',parameter='skin.bodySmooth',value=70);state=wait()
saved=call(op='portrait_state',source=str(source),render=str(OUT/'combined.png'),checkRegion=True,save=str(OUT/'smooth.huelith'));assert saved['roundtrip'] and saved['regionError']<2e-6
recipe=state['recipe'];p=recipe['people'][0];call(op='portrait_close')
masks={}
for key in ['skin.smooth','skin.bodySmooth']:
    path=OUT/(key+'-mask.png');scale=768/max(size);mask_size=[round(size[0]*scale),round(size[1]*scale)];call(op='portrait_geometry',recipe=recipe,size=mask_size,styleMask=key,mask=str(path));small=np.asarray(Image.open(path));masks[key]=small[(np.arange(size[1])*mask_size[1]/size[1]).astype(int)[:,None],(np.arange(size[0])*mask_size[0]/size[0]).astype(int)[None,:]]
assert (masks['skin.smooth']>230).sum()>20 and (masks['skin.bodySmooth']>230).sum()>100
renders={}
for key in ['none','skin.smooth','skin.bodySmooth']:
    p['settings']['parameters']={} if key=='none' else {key:100};path=OUT/(key+'.bin')
    call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),render=str(OUT/(key+'.png')),linearRender=str(path))
    renders[key]=np.fromfile(path,dtype=np.float32).reshape(size[1],size[0],4)
for key,other in [('skin.smooth','skin.bodySmooth'),('skin.bodySmooth','skin.smooth')]:
    delta=np.max(np.abs(renders[key]-renders['none']),axis=2);protected=(masks[key]==0)&(masks[other]>230)
    assert protected.sum()>20 and delta[protected].max()<2e-6
    assert delta.max()>1e-4
preview=np.asarray(Image.open(source).convert('RGB')).copy();overlay=np.zeros_like(preview);overlay[:,:,0]=masks['skin.smooth'];overlay[:,:,1]=masks['skin.bodySmooth'];Image.fromarray((preview*.55+overlay*.45).astype(np.uint8)).save(OUT/'regions.jpg')
# Recognition upgrades the automatic masks without discarding the user's controls or painted masks.
p['settings']['parameters']={'color.warmth':20};p['settings']['masks']={'skin.bodySmooth':p['settings']['automaticMasks']['bodySkin']}
call(op='portrait_window',path=str(source),recipe=recipe,models=str(ROOT/'build/windows-release/models'));wait();call(op='portrait_state',click='portraitDetect');state=wait()
matched=next(v for v in state['recipe']['people'] if v['id']==p['id']);assert matched['settings']['parameters']==p['settings']['parameters'];assert matched['settings']['masks']==p['settings']['masks']
call(op='portrait_close');call(op='shutdown');print('Independent face/body masks, controls, protected regions, save and re-identification passed.')
