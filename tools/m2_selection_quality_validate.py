"""Fixed fur/soft-alpha cases and manual correction using production selection refinement."""
import ctypes,json,os,tempfile,time
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
from scipy.ndimage import distance_transform_edt
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/selection-quality'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64');handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins');os.environ['QT_QPA_PLATFORM']='windows'
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):
        r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()));assert 'error' not in r,r;return r
    def wait():
        end=time.monotonic()+30
        while time.monotonic()<end:
            call('pump',waitMs=25);r=call('refine_window')
            if r['ready']:return r
            assert not r['failed'],r
        raise AssertionError(r)
    records=[]
    with tempfile.TemporaryDirectory(prefix='huelith-selection-quality-') as directory:
        folder=Path(directory);call('init',settings=directory)
        try:
            data=ROOT/'research/m2/matting'
            rgb=np.array(Image.open(data/'lemur.png').convert('RGB'))
            trimap=np.array(Image.open(data/'lemur_trimap.png').convert('L'))
            reference=np.array(Image.open(data/'lemur_alpha.png').convert('L'))/255.
            foreground=trimap==255;background=trimap==0;unknown=~(foreground|background)
            df=distance_transform_edt(~foreground);db=distance_transform_edt(~background)
            rough=np.where(foreground|((df<db)&unknown),255,0).astype('uint8')
            Image.fromarray(rough).save(folder/'rough.png')
            call('refine_probe',image=str(data/'lemur.png'),mask=str(folder/'rough.png'),settings=dict(radius=8,smart=True),output=str(OUT/'fur-mask.png'))
            refined=np.array(Image.open(OUT/'fur-mask.png').convert('L'))/255.
            before=float(np.mean(abs(rough[unknown]/255-reference[unknown])))
            after=float(np.mean(abs(refined[unknown]-reference[unknown])))
            soft=int(np.count_nonzero((refined>0)&(refined<1)&unknown))
            assert soft>100 and after<before,(before,after,soft)
            sheet=Image.new('RGB',(rgb.shape[1]*3,rgb.shape[0]+24),'#222');draw=ImageDraw.Draw(sheet)
            for i,(name,alpha) in enumerate([('Initial selection',rough/255),('Refined',refined),('Published reference',reference)]):
                composite=rgb*alpha[:,:,None]+np.array([65,65,65])*(1-alpha[:,:,None])
                sheet.paste(Image.fromarray(np.clip(composite,0,255).astype('uint8')),(i*rgb.shape[1],24));draw.text((i*rgb.shape[1]+4,4),name,fill='white')
            sheet.save(OUT/'fur-comparison.jpg')
            records.append(dict(case='natural fur published reference',unknownMAEBefore=before,unknownMAEAfter=after,softPixels=soft));print(records[-1],flush=True)
            # Soft translucent input must remain soft through the identity workflow.
            y,x=np.mgrid[:200,:320];alpha=np.clip((x-40)/240,0,1)
            mask=np.round(alpha*255).astype('uint8');Image.fromarray(mask).save(folder/'soft.png')
            glass=np.round(np.array([35,120,190])*alpha[:,:,None]+np.array([220,190,130])*(1-alpha[:,:,None])).astype('uint8')
            Image.fromarray(glass).save(folder/'glass.png')
            call('refine_probe',image=str(folder/'glass.png'),mask=str(folder/'soft.png'),settings={},output=str(OUT/'soft-identity.png'))
            assert np.array_equal(np.array(Image.open(OUT/'soft-identity.png')),mask)
            records.append(dict(case='known semitransparent mask preserved exactly',passed=True))
            call('refine_window',image=str(data/'lemur.png'),mask=str(folder/'rough.png'));wait()
            call('refine_window',refineTool=1,refineBrushSize=35)
            point=[.95,.05]
            call('refine_window',stroke=[point,point]);wait();call('refine_window',save=str(OUT/'added.png'))
            added=np.array(Image.open(OUT/'added.png'))
            py,px=int(point[1]*rgb.shape[0]),int(point[0]*rgb.shape[1]);assert added[py,px]>200
            call('refine_window',stroke=[point,point],alt=True);wait();call('refine_window',save=str(OUT/'subtracted.png'))
            sub=np.array(Image.open(OUT/'subtracted.png'));assert sub[py,px]<20
            call('refine_window',key='Ctrl+Z');wait();call('refine_window',save=str(OUT/'undone.png'))
            assert np.array_equal(np.array(Image.open(OUT/'undone.png')),added)
            call('refine_window',reject=True);call('refine_window',close=True)
            records.append(dict(case='natural image manual add subtract undo',passed=True))
            # Reuse unchanged model outputs from the frozen ADE fixtures. These are
            # measured limits, not a claim that a difficult scene is automatically solved.
            for n in [1,2]:
                stem=f'ADE_val_{n:08d}';samples=ROOT/'research/m2/semantic/samples'
                pred=np.array(Image.open(ROOT/'docs/test-evidence/M2/semantic'/(stem+'-labels.png')))
                truth=np.array(Image.open(samples/(stem+'.png')).resize((pred.shape[1],pred.shape[0]),Image.Resampling.NEAREST)).astype(int)-1
                labels=[4,9,17,29,66,72]
                selected=np.isin(pred,labels);expected=np.isin(truth,labels)
                records.append(dict(case=stem,productionVegetationLabels=labels,
                    vegetationIoU=float(np.count_nonzero(selected&expected)/np.count_nonzero(selected|expected)),
                    needsManualCorrection=n==2))
        finally:call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,indent=2),'utf-8')

if __name__=='__main__':main()
