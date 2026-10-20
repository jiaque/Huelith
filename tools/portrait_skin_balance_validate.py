from pathlib import Path
import io,base64
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-skin-balance';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());p=recipe['people'][0];p['face']=[.05,.1,.35,.8];p['settings']['parameters']={'color.balance':100};p['settings']['skinBalanceVersion']=2
buf=io.BytesIO();Image.new('L',(128,128),255).save(buf,format='PNG');p['settings']['automaticMasks']={'skin':base64.b64encode(buf.getvalue()).decode()};p['settings']['masks']={}
x,y=np.meshgrid(np.arange(400),np.arange(160));light=.08+.25*y/159;base=np.array([1.3,.93,.5]);base=base/(base@np.array([.2627,.678,.0593]));pixels=light[:,:,None]*base
# A body-area color cast, with unchanged illumination.
pixels[x>210,0]+=.04;pixels[x>210,1]-=.04*.2627/.678
pixels[:,:,0]+=.003*np.sin(x*2*np.pi/4);pixels[:,:,1]-=.003*.2627/.678*np.sin(x*2*np.pi/4)
srgb=np.where(pixels<=.0031308,pixels*12.92,1.055*np.maximum(pixels,0)**(1/2.4)-.055);source=OUT/'illumination-and-cast.png';Image.fromarray(np.uint8(np.clip(srgb*255,0,255))).save(source)
data={}
for value in [0,100]:
 p['settings']['parameters']['color.balance']=value;out=OUT/f'balance-{value}.bin';r=call(op='portrait_geometry',recipe=recipe,size=[400,160],source=str(source),render=str(out.with_suffix('.png')),linearRender=str(out),colorMetrics=True);data[value]=np.fromfile(out,np.float32).reshape(160,400,4)
 if value:assert r['maximumLuminanceError']<2e-6,r
assert np.max(abs(data[0][20:140,30:130]-data[100][20:140,30:130]))<2e-6
Y=data[0][:,:,:3]@np.array([.2627,.678,.0593]);a=data[0][:,:,0]/Y;b=data[100][:,:,0]/Y
reference=np.median(a[20:140,30:130]);assert np.mean(abs(b[20:140,270:370]-reference))<np.mean(abs(a[20:140,270:370]-reference))*.7
assert np.std(b[80,280:360])>np.std(a[80,280:360])*.8
call(op='shutdown');print('Regional color cast reduced, face unchanged, illumination and fine chroma retained')
