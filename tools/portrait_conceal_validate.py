from pathlib import Path
import io,base64
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-conceal';call(op='init',settings=str(OUT/'algorithm-settings'));recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());p=recipe['people'][0]
buf=io.BytesIO();Image.new('L',(64,64),255).save(buf,format='PNG');p['face']=[0,0,1,1];p['settings']['masks']={'repair.conceal':base64.b64encode(buf.getvalue()).decode()};p['settings']['concealColor']=[.35,.25,.18]
x,y=np.meshgrid(np.arange(256),np.arange(64));a=np.zeros((64,256,3));a[:]=[180,145,120];a[:,:,0]+=8*np.sin(x*2*np.pi/3);source=OUT/'color-detail.png';Image.fromarray(np.uint8(a)).save(source)
outputs={}
for version,value in [(2,0),(1,100),(2,100)]:
    p['settings']['concealVersion']=version;p['settings']['parameters']={'repair.conceal':value};path=OUT/f'detail-v{version}-{value}.bin';r=call(op='portrait_geometry',recipe=recipe,size=[256,64],source=str(source),render=str(path.with_suffix('.png')),linearRender=str(path),colorMetrics=True);assert r['maximumLuminanceError']<2e-6,r
    pixels=np.fromfile(path,np.float32).reshape(64,256,4);chroma=pixels[:,:,0]/(pixels[:,:,:3]@np.array([.2627,.678,.0593]));outputs[(version,value)]=float(chroma[20,40:210].std())
assert outputs[(2,100)]>outputs[(1,100)]*2 and outputs[(2,100)]>outputs[(2,0)]*.85,outputs
(OUT/'texture-metrics.json').write_text(json.dumps({str(k):v for k,v in outputs.items()},indent=2));call(op='shutdown');print(outputs)
