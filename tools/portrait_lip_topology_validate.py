"""Verify corrected OpenSeeFace lip mask never feathers pigment onto teeth."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import cv2
OUT=ROOT/'docs/test-evidence/M3/semantic-mouth'
call(op='init',settings=str(OUT/'topology-settings'))
for name in ['leroy-chiao','chris-hadfield']:
    recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text())
    p=recipe['people'][0];p['settings']['mouthTopologyVersion']=2
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=Image.open(source).size
    dest=OUT/f'{name}-lip-mask.png'
    call(op='portrait_geometry',styleMask='makeup.lip',size=size,recipe=recipe,mask=str(dest))
    mask=np.asarray(Image.open(dest));lm=np.array(p['landmarks'])[:,:2]*size
    inner=np.zeros(mask.shape,np.uint8);cv2.fillPoly(inner,[lm[[58,59,60,61,62,63,64,65]].round().astype(np.int32)],255)
    core=cv2.erode(inner,np.ones((7,7),np.uint8))>0
    assert mask.sum()>0 and (not core.any() or mask[core].max()==0)
    photo=Image.open(source).convert('RGB');overlay=np.asarray(photo).copy();a=mask.astype(float)[...,None]/255*.7
    overlay=(overlay*(1-a)+np.array([255,0,150])*a).astype(np.uint8)
    x,y,w,h=p['face'];Image.fromarray(overlay).crop((int(x*size[0]),int((y+h*.45)*size[1]),int((x+w)*size[0]),int((y+h)*size[1]))).save(OUT/f'{name}-lip-overlay.png')
call(op='shutdown');print('Corrected lip topology excludes the oral cavity core.')
