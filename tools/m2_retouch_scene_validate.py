"""Fixed natural texture repair cases and a 24MP production editor stroke."""
import base64,ctypes,json,os,tempfile,time
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/retouch-scenes'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64');handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins');os.environ['QT_QPA_PLATFORM']='windows'
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):
        r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert 'error' not in r,r;return r
    enc=lambda a:base64.b64encode(a.tobytes()).decode()
    results=[]
    with tempfile.TemporaryDirectory(prefix='huelith-retouch-scenes-') as folder:
        call('init',settings=folder)
        try:
            source=Image.open(ROOT/'docs/test-evidence/M2/color-scenes/r10-8185.CR3-full-rawPreview.png').convert('RGB')
            for name,box in [('grass',(1000,200,1256,456)),('water',(1500,3300,1756,3556))]:
                original=np.asarray(source.crop(box)).astype(np.float32)/255
                # Supply scene colors in the production working space; evaluate against the
                # identical undamaged fixture, not an unavailable Photoshop reference.
                clean=np.ones((256,256,4),dtype='<f4');clean[:,:,:3]=original
                yy,xx=np.mgrid[:256,:256];mask=((xx-128)**2+(yy-128)**2<10**2).astype(np.uint8)*255
                damaged=clean.copy();damaged[mask>0,:3]=[1,0,1]
                r=call('brush_probe',patch=True,automatic=True,width=256,height=256,data=enc(damaged),mask=enc(mask),apply=True)
                fixed=np.frombuffer(base64.b64decode(r['data']),dtype='<f4').reshape(clean.shape)
                before=float(np.mean(abs(damaged[mask>0,:3]-clean[mask>0,:3])))
                after=float(np.mean(abs(fixed[mask>0,:3]-clean[mask>0,:3])))
                assert after<before*.5,(name,before,after)
                assert np.array_equal(fixed[mask==0],clean[mask==0])
                sheet=Image.new('RGB',(768,280),'#222');draw=ImageDraw.Draw(sheet)
                for i,(label,a) in enumerate([('Clean',clean),('Blemish',damaged),('Repaired',fixed)]):
                    sheet.paste(Image.fromarray((np.clip(a[:,:,:3],0,1)*255).astype('uint8')),(i*256,24));draw.text((i*256+4,4),label,fill='white')
                sheet.save(OUT/(name+'.png'))
                results.append(dict(scene=name,errorBefore=before,errorAfter=after,ms=r['elapsedMs'],passed=True));print(results[-1],flush=True)
            call('window');path=ROOT/'docs/test-evidence/M2/color-scenes/r10-8185.CR3-full-rawPreview.png'
            call('window_scene',width=6000,height=4000,layers=[dict(name='24MP',path=str(path))])
            call('window_control',name='toolButton_16',value=True)
            for phase in ['press','release']:call('window_mouse',phase=phase,x=2500,y=2000,modifiers=134217728)
            timings=[]
            for phase,x,y in [('press',3000,2200),('move',3030,2220),('move',3060,2240),('release',3060,2240)]:
                start=time.monotonic();call('window_mouse',phase=phase,x=x,y=y);timings.append((time.monotonic()-start)*1000)
            result=dict(scene='24MP clone stroke',eventMs=timings);results.append(result);print(result,flush=True)
            call('window_key',key='Ctrl+Z')
            start=time.monotonic()
            call('window_brush',liquifyDialog=True,actions=[dict(liquifySize=800),dict(**{'from':[.4,.5],'to':[.45,.5]})],accept=True)
            result=dict(scene='24MP liquify open stroke commit',seconds=time.monotonic()-start);results.append(result);print(result,flush=True)
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(results,indent=2),'utf-8')

if __name__=='__main__':main()
