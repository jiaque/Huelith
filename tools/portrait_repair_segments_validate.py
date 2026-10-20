from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-repair-segments';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
source=OUT/'texture.png'
y,x=np.mgrid[0:300,0:400];v=(125+12*np.sin(x*.65)+8*np.sin(y*.9)).astype(np.uint8);rgb=np.stack([v,v,v],axis=-1);rgb[100:104,120:270]=70;Image.fromarray(rgb).save(source)
mask=OUT/'skin.png';Image.new('L',(400,300),255).save(mask)
base=dict(version=1,points=[[.3,.34],[.675,.34]],radius=.025,feather=.5,opacity=1)
start=time.monotonic();a=call(op='portrait_geometry',source=str(source),sourceMask=str(mask),healingPath=base,render=str(OUT/'sparse.png'))['stroke'];elapsed=time.monotonic()-start
base['points']=[[float(t),.34] for t in np.linspace(.3,.675,101)]
b=call(op='portrait_geometry',source=str(source),sourceMask=str(mask),healingPath=base,render=str(OUT/'dense.png'))['stroke']
assert len(a['sourceOffsets'])>10
assert np.max(abs(np.array(a['points'])-np.array(b['points'])))<1e-10
assert np.max(abs(np.array(a['sourceOffsets'])-np.array(b['sourceOffsets'])))<1e-10
ia=np.array(Image.open(OUT/'sparse.png')).astype(float);ib=np.array(Image.open(OUT/'dense.png')).astype(float);assert np.max(abs(ia-ib))==0
assert np.max(abs(ia[:,:,:3]-rgb))>5
print('Canonical path and pixels independent of input event density; time',elapsed)
call(op='shutdown')

