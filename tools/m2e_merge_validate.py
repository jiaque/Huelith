"""Real C++ worker: radiance recovery, sharp-region selection, natural panorama, Qt acceptance."""
import ctypes,json,os,tempfile
from pathlib import Path
import cv2
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2-E/merge'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command
    fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def raw(op,**kw):return json.loads(fn(json.dumps(dict(op=op,**kw)).encode()))
    def call(op,**kw):
        r=raw(op,**kw);assert 'error' not in r,r;return r
    records=[]
    with tempfile.TemporaryDirectory(prefix='huelith-merge-') as folder:
        call('init',settings=folder)
        try:
            width,height=320,160
            radiance=np.broadcast_to(np.linspace(.01,3.2,width)[None,:,None],(height,width,3))
            paths=[]
            for index,ev in enumerate([-2,0,2]):
                linear=np.clip(radiance*2**ev,0,1)
                encoded=np.where(linear<=.0031308,linear*12.92,1.055*linear**(1/2.4)-.055)
                p=OUT/f'bracket-{index}.png';Image.fromarray(np.rint(encoded*255).astype('uint8')).save(p);paths.append(str(p))
            result=call('photo_merge_probe',mode=0,paths=paths,ev=[-2,0,2],reference=1,align=False,
                        samples=[[x,80] for x in [20,100,200,310]],output=str(OUT/'hdr-preview.png'))
            assert result['hdrLinear'] and result['maximumLinear']>3.1,result
            for x,sample in zip([20,100,200,310],result['samples']):assert abs(sample[0]-radiance[80,x,0])<.035,(x,sample,radiance[80,x,0])
            records.append(dict(case='hdr-recovers-known-radiance-above-white',**result));print(records[-1],flush=True)
            original=np.array(Image.open(ROOT/'research/m2/semantic/samples/ADE_val_00000001.jpg').convert('RGB').resize((512,384)))
            blurred=cv2.GaussianBlur(original,(0,0),3)
            first=original.copy();first[:,256:]=blurred[:,256:]
            second=original.copy();second[:,:256]=blurred[:,:256]
            focus=[]
            for i,image in enumerate([first,second]):
                p=OUT/f'focus-{i}.png';Image.fromarray(image).save(p);focus.append(str(p))
            result=call('photo_merge_probe',mode=2,paths=focus,align=False,output=str(OUT/'focus.png'))
            recovered=np.array(Image.open(OUT/'focus.png'))[...,:3].astype(float)
            error=float(np.mean((recovered-original)**2));baseline=min(float(np.mean((im.astype(float)-original)**2)) for im in [first,second])
            assert error<baseline*.3,(error,baseline,result)
            records.append(dict(case='focus-both-sharp-halves',mse=error,singleFrameMse=baseline,**result));print(records[-1],flush=True)
            shifted=cv2.warpAffine(second,np.float32([[1,0,12],[0,1,-7]]),(512,384))
            p=OUT/'focus-shifted.png';Image.fromarray(shifted).save(p)
            result=call('photo_merge_probe',mode=2,paths=[focus[0],str(p)],align=True,output=str(OUT/'focus-aligned.png'))
            assert 480<result['width']<=512 and 350<result['height']<=384,result
            records.append(dict(case='focus-feature-alignment-and-covered-crop',**result));print(records[-1],flush=True)
            panorama=[str(ROOT/'research/m2/merge'/f'a{i}.png') for i in [1,2,3]]
            result=call('photo_merge_probe',mode=1,paths=panorama,output=str(OUT/'panorama.png'))
            assert result['width']>600 and result['height']>150,result
            records.append(dict(case='natural-opencv-three-photo-panorama',**result));print(records[-1],flush=True)
            natural=[str(ROOT/'research/m2/merge/focus'/f'pcb_{i:03}.jpg') for i in range(1,8)]
            result=call('photo_merge_probe',mode=2,paths=natural,align=True,reference=3,output=str(OUT/'natural-focus.png'))
            recovered=cv2.imread(str(OUT/'natural-focus.png'),cv2.IMREAD_GRAYSCALE)
            detail=float(cv2.Laplacian(recovered,cv2.CV_64F).var())
            baseline=max(float(cv2.Laplacian(cv2.imread(p,cv2.IMREAD_GRAYSCALE),cv2.CV_64F).var()) for p in natural)
            assert detail>baseline*.8 and result['width']>500 and result['height']>500,(result,detail,baseline)
            records.append(dict(case='real-seven-frame-pcb-focus-sequence',detailVariance=detail,bestSingleVariance=baseline,**result));print(records[-1],flush=True)
            assert 'error' in raw('photo_merge_probe',mode=0,paths=paths,ev=[0],align=False)
            assert 'error' in raw('photo_merge_probe',mode=2,paths=focus,cancelled=True)
            call('window')
            for mode,sources,ev in [(0,paths,[-2,0,2]),(1,panorama,[]),(2,focus,[])]:
                result=call('photo_merge_ui',mode=mode,paths=sources,ev=ev,reference=1 if mode==0 else 0,align=False,capture=str(OUT/f'dialog-{mode}.png'))
                assert result['previewed'] and result['documentsAfter']==result['documentsBefore']+1,result
                records.append(dict(case=f'actual-menu-preview-merge-{mode}',**result));print(records[-1],flush=True)
            result=call('photo_merge_ui',mode=2,paths=focus,align=False,cancel=True)
            assert result['previewed'] and result['documentsAfter']==result['documentsBefore'],result
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
if __name__=='__main__':main()
