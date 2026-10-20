from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
import io,base64,copy
OUT=ROOT/'docs/test-evidence/M3/workspace-c04'
def enc(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
r=json.loads((ROOT/'docs/test-evidence/M3/quality/leroy-chiao-recipe.json').read_text());s=r['people'][0]['settings'];s['parameters']={};s['repairs']=[];s['decals']=[];s['liquify']=[]
mask=Image.new('L',(320,240));ImageDraw.Draw(mask).rectangle((40,30,280,210),fill=255);s['automaticMasks']={'skin':enc(mask)};s['masks']={key:enc(Image.new('L',(320,240),255)) for key in ['paint.skin','paint.makeup','paint.glitter','paint.light']};s['colors']={'paint.skin':[.5,.25,.15],'paint.makeup':[.5,.03,.06],'paint.glitter':[.9,.7,.3]}
y,x=np.mgrid[:240,:320];gray=(90+8*np.sin(x*.6)+130*np.exp(-((y-65)/15)**2)).clip(0,255).astype('uint8');source=OUT/'material-source.png';Image.fromarray(np.repeat(gray[:,:,None],3,axis=2)).save(source)
call(op='init',settings=str(OUT/'algorithm-settings'))
def render(name,params):
 s['parameters']=params;path=OUT/name;call(op='portrait_geometry',recipe=r,source=str(source),size=[320,240],render=str(path.with_suffix('.png')),linearRender=str(path.with_suffix('.bin')));return np.fromfile(path.with_suffix('.bin'),np.float32).reshape(240,320,4)
try:
 a=render('material-base',{});skin=render('material-skin',{'paint.skin':100});makeup=render('material-makeup',{'paint.makeup':100});weights=np.array([.2627,.678,.0593]);assert np.max(abs(skin[:,:,:3]@weights-a[:,:,:3]@weights))<2e-6;assert np.max(abs(makeup[:,:,:3]@weights-a[:,:,:3]@weights))>.01
 glitter=render('material-glitter',{'paint.glitter':100});half=render('material-glitter-half',{'paint.glitter':50});assert np.max(abs((glitter-a)*.5-(half-a)))<2e-6;assert np.array_equal(glitter,render('material-glitter-repeat',{'paint.glitter':100}))
 light=render('material-light',{'paint.light':100,'paint.protect':100});unprotected=render('material-light-no-protect',{'paint.light':100,'paint.protect':0});assert np.mean(light[55:75,60:260,:3])<np.mean(unprotected[55:75,60:260,:3])
 for result in [skin,makeup,glitter,light]:assert np.array_equal(result[:25],a[:25]) and np.array_equal(result[:,:,3],a[:,:,3]) and np.isfinite(result).all()
 print('C04 distinct pigment/chroma, luminance/texture preservation, deterministic glitter, fixed opacity distribution, high-light and boundary protection passed')
finally:call(op='shutdown')
