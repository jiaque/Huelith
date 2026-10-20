"""Independent W3C/Adobe-defined blend equations and production FP32 composition matrix."""
import base64,ctypes,json,os,tempfile,traceback
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/blend-matrix'

def lum(v):return np.dot(v,[.3,.59,.11])
def sat(v):return float(np.max(v)-np.min(v))
def set_sat(v,s):
    span=sat(v)
    return (v-np.min(v))*s/span if span else np.zeros(3)
def set_lum(v,l):
    c=v+(l-lum(v));middle=lum(c);lo=min(c);hi=max(c)
    if lo<0:c=middle+(c-middle)*middle/(middle-lo)
    if hi>1:c=middle+(c-middle)*(1-middle)/(hi-middle)
    return c

def term(s,b,m):
    s=np.clip(np.array(s,float),0,1);b=np.clip(np.array(b,float),0,1)
    if m==20:return set_lum(set_sat(s,sat(b)),lum(b))
    if m==21:return set_lum(set_sat(b,sat(s)),lum(b))
    if m==22:return set_lum(s,lum(b))
    if m==23:return set_lum(b,lum(s))
    def burn(s,b):
        return np.where(b>=1,1,np.where(s<=0,0,1-np.minimum(1,(1-b)/np.maximum(s,1e-30))))
    def dodge(s,b):
        return np.where(b<=0,0,np.where(s>=1,1,np.minimum(1,b/np.maximum(1-s,1e-30))))
    vivid=lambda s,b:np.where(s<=.5,burn(2*s,b),dodge(2*s-1,b))
    operations=[lambda:s,lambda:np.minimum(s,b),lambda:s*b,lambda:burn(s,b),lambda:s+b-1,
        lambda:np.maximum(s,b),lambda:1-(1-s)*(1-b),lambda:dodge(s,b),lambda:s+b,
        lambda:np.where(b<=.5,2*b*s,1-2*(1-s)*(1-b)),
        lambda:np.where(s<=.5,b-(1-2*s)*b*(1-b),b+(2*s-1)*(np.where(b<=.25,((16*b-12)*b+4)*b,np.sqrt(b))-b)),
        lambda:np.where(s<=.5,2*b*s,1-2*(1-s)*(1-b)),lambda:vivid(s,b),lambda:b+2*s-1,
        lambda:np.where(s<=.5,np.minimum(b,2*s),np.maximum(b,2*s-1)),lambda:(vivid(s,b)>=.5).astype(float),
        lambda:abs(b-s),lambda:b+s-2*b*s,lambda:b-s,lambda:np.where(s==0,1,b/np.maximum(s,1e-30))]
    return np.clip(operations[m](),0,1)

def encode(v):
    v=np.clip(v,0,1);return np.where(v<=.0031308,12.92*v,1.055*v**(1/2.4)-.055)
def decode(v):return np.where(v<=.04045,v/12.92,((v+.055)/1.055)**2.4)
def over(b,s,m=0,k=1):
    b=np.array(b,float);s=np.array(s,float);a=s[3]*k
    rgb=s[:3]*k+b[:3]*(1-a)
    if m and a>0 and b[3]>0:
        blend=decode(term(encode(s[:3]/s[3]),encode(b[:3]/b[3]),m))
        rgb=s[:3]*k*(1-b[3])+b[:3]*(1-a)+a*b[3]*blend
    return np.r_[rgb,a+b[3]*(1-a)]

def main():
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64');handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins');OUT.mkdir(parents=True,exist_ok=True)
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def raw(**kw):return json.loads(fn(json.dumps(dict(op='brush_probe',**kw)).encode()))
    def call(**kw):
        r=raw(**kw);assert 'error' not in r,r;return r
    def render(layers,**kw):
        r=call(composite=True,layers=layers,**kw);assert r['roundtrip'];return np.frombuffer(base64.b64decode(r['data']),'<f4').reshape(kw.get('height',8),kw.get('width',8),4)
    results=[];metrics=[]
    def test(name,body):
        try:body();results.append(dict(name=name,passed=True))
        except Exception:results.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(results[-1],flush=True)
    def command(op,**kw):
        r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert 'error' not in r,r;return r
    with tempfile.TemporaryDirectory(prefix='huelith-blend-') as folder:
        command('init',settings=folder)
        try:
            def equations():
                rng=np.random.default_rng(2724);cases=[];expected=[]
                colors=[np.array(c,np.float32) for c in [[0,0,0],[1,1,1],[.5]*3,[0,.25,1],[1,0,.5],[-.3,2,.1]]]
                pairs=[(a,b) for a in colors for b in colors]+[(a,b) for a,b in rng.uniform(-.05,1.05,(100,2,3)).astype('float32')]
                for mode in range(24):
                    for source,backdrop in pairs:
                        cases.append(dict(mode=mode,source=source.tolist(),backdrop=backdrop.tolist()));expected.append(term(source,backdrop,mode))
                actual=np.array(call(blendTerms=cases)['values']);difference=abs(actual-np.array(expected));assert difference.max()<2e-6,(difference.max(),cases[int(np.unravel_index(difference.argmax(),difference.shape)[0])])
                assert 'error' in raw(blendTerms=[dict(mode=24,source=[0,0,0],backdrop=[1,1,1])])
                metrics.append(dict(equationCases=len(cases),maximumError=float(difference.max())))
            test('24_modes_3264_independent_equations_boundaries_nonseparable_hdr_clamp',equations)
            def alpha_matrix():
                rng=np.random.default_rng(189);maximum=0;count=0
                for mode in range(24):
                    for i in range(12):
                        sa,ba=rng.uniform(.05,1,2);s=np.r_[rng.uniform(-.1,2.5,3)*sa,sa];b=np.r_[rng.uniform(-.1,2.5,3)*ba,ba]
                        opacity=float(rng.uniform(.1,1));mask=int(rng.integers(1,256));k=opacity*mask/255
                        image=render([dict(rgba=b.tolist()),dict(rgba=s.tolist(),mode=mode,opacity=opacity,mask=mask)])
                        expected=over(b,s,mode,k);error=float(abs(image-expected).max());maximum=max(maximum,error)
                        assert error<8e-5,(mode,i,error,image[0,0],expected)
                        count+=1
                metrics.append(dict(alphaHdrMaskCases=count,maximumError=maximum))
            test('24_modes_premultiplied_partial_alpha_mask_opacity_hdr_color_space',alpha_matrix)
            def groups():
                b=np.array([.16,.09,.32,.6]);s=np.array([.36,.12,.04,.5]);k=.7*133/255;maximum=0
                for mode in range(24):
                    for isolated in [False,True]:
                        child=dict(rgba=s.tolist(),mode=mode,opacity=.8,mask=191)
                        group=dict(kind=5,isolated=isolated,opacity=.7,mask=133,children=[child])
                        actual=render([dict(rgba=b.tolist()),group])
                        local=over(np.zeros(4) if isolated else b,s,mode,.8*191/255)
                        expected=over(b,local,0,k) if isolated else b+(local-b)*k
                        maximum=max(maximum,float(abs(actual-expected).max()));assert np.allclose(actual,expected,atol=8e-5,rtol=0),(mode,isolated,actual[0,0],expected)
                metrics.append(dict(groupCases=48,maximumError=maximum))
            test('all_modes_isolated_and_pass_through_group_opacity_masks',groups)
            def clipping():
                backdrop=np.array([.2,.4,.1,1]);base=np.array([.2,.1,.05,.4]);s=np.array([.1,.3,.2,.6]);base_k=.75*128/255
                for mode in range(24):
                    for grouped in [False,True]:
                        root=dict(kind=5,children=[dict(rgba=base.tolist())],opacity=.75,mask=128) if grouped else dict(rgba=base.tolist(),opacity=.75,mask=128)
                        a=over(backdrop,base,k=base_k)
                        # A neutral adjustment between the base and clipping layer must
                        # not cause the lazy clip-base optimization to discard the base.
                        layers=[dict(rgba=backdrop.tolist()),root,dict(kind=3),dict(rgba=s.tolist(),mode=mode,clipped=True,opacity=.8,mask=200)]
                        expected=over(a,s,mode,.8*200/255*base[3]*base_k)
                        actual=render(layers);assert np.allclose(actual,expected,atol=8e-5,rtol=0),(mode,grouped,actual[0,0],expected)
                hidden=render([dict(rgba=backdrop.tolist()),dict(rgba=base.tolist(),visible=False),dict(rgba=s.tolist(),clipped=True)])
                assert np.allclose(hidden,backdrop,atol=8e-5)
            test('clip_uses_intrinsic_group_alpha_hidden_base_and_intervening_adjustment',clipping)
            def spatial():
                b=[.15,.24,.1,1];s=[.2,.05,.12,.4]
                layers=[dict(rgba=b),dict(kind=5,isolated=True,opacity=.8,mask=128,children=[dict(kind=5,children=[dict(rgba=s,mode=6,blur=1.3,vector=[2,1,3,4])])])]
                image=render(layers,file=str(Path(folder)/'complex.huelith'),capture=str(OUT/'nested-output.png'))
                expected=np.broadcast_to(np.array(b),(8,8,4)).copy();expected[1:5,2:5]=over(b,s,k=.8*128/255)
                assert np.allclose(image,expected,atol=8e-5,rtol=0),float(abs(image-expected).max())
                encoded=np.rint(encode(expected[:,:,:3]/expected[:,:,3,None])*255).astype(int)
                actual=np.asarray(Image.open(OUT/'nested-output.png').convert('RGB')).astype(int)
                assert abs(encoded-actual).max()<=2,int(abs(encoded-actual).max())
                assert 'error' in raw(composite=True,layers=layers,cancelled=True)
            test('nested_vector_mask_spatial_effect_roundtrip_export_and_cancel',spatial)
        finally:command('shutdown')
    report=dict(passed=sum(r['passed'] for r in results),total=len(results),tests=results,metrics=metrics)
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf-8');return int(report['passed']!=report['total'])
if __name__=='__main__':raise SystemExit(main())
