from pathlib import Path
import io,base64
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-wrinkle';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());p=recipe['people'][0];p['settings']['parameters']={};buf=io.BytesIO();Image.new('L',(256,128),255).save(buf,format='PNG');p['settings']['masks']={'skin':base64.b64encode(buf.getvalue()).decode()}
stroke=dict(version=1,kind='wrinkle',wrinkleMethod='lighting-v2',id='test',points=[[.2,.5],[.8,.5]],offsetX=0,offsetY=0,radius=.08,feather=.6,opacity=1)
x,y=np.meshgrid(np.arange(256),np.arange(128));records=[]
for kind in ['gradient','wrinkle']:
    intensity=140+y*.35+2*np.sin(x*2*np.pi/3)
    if kind=='wrinkle':intensity-=30*np.exp(-((y-64)/2.5)**2)
    source=OUT/f'{kind}.png';Image.fromarray(np.uint8(np.repeat(intensity[:,:,None],3,axis=2))).save(source);outputs=[]
    for value in [0,1]:
        stroke['opacity']=value;p['settings']['repairs']=[stroke];binary=OUT/f'{kind}-{value}.bin';call(op='portrait_geometry',recipe=recipe,size=[256,128],source=str(source),render=str(binary.with_suffix('.png')),linearRender=str(binary));outputs.append(np.fromfile(binary,np.float32).reshape(128,256,4))
    a,b=outputs;delta=np.max(abs(a-b),axis=2)
    if kind=='gradient':assert delta.max()<2e-6,float(delta.max())
    else:
        assert b[64,128,0]>a[64,128,0]+.005
        assert delta[:50].max()<2e-6
        before=np.std(a[64,80:170,0]);after=np.std(b[64,80:170,0]);assert after>before*.8
    records.append(dict(kind=kind,change=float(delta.max())))
(OUT/'metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown');print(records)
