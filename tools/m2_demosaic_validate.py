"""Independent paper-kernel reference, Bayer invariants and real RAW quality evidence."""
import ctypes,json,os,tempfile,time,traceback
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/demosaic'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins')
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn=dll.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def raw(op,**kw):return json.loads(fn(json.dumps(dict(op=op,**kw)).encode()))
    def call(op,**kw):
        r=raw(op,**kw);assert 'error' not in r,r;return r
    records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    def convolve(a,k):
        p=np.pad(a,2,mode='reflect');h,w=a.shape
        return sum(k[y,x]*p[y:y+h,x:x+w] for y in range(5) for x in range(5))
    green=np.array([[0,0,-1,0,0],[0,0,2,0,0],[-1,2,4,2,-1],[0,0,2,0,0],[0,0,-1,0,0]],float)/8
    horizontal=np.array([[0,0,.5,0,0],[0,-1,0,-1,0],[-1,4,5,4,-1],[0,-1,0,-1,0],[0,0,.5,0,0]],float)/8
    opposite=np.array([[0,0,-1.5,0,0],[0,2,0,2,0],[-1.5,0,6,0,-1.5],[0,2,0,2,0],[0,0,-1.5,0,0]],float)/8
    def oracle(a,pattern):
        h,w=a.shape;r=np.zeros((h,w,3));positions={}
        for y in range(2):
            for x in range(2):positions.setdefault(pattern[y*2+x],[]).append((y,x))
        filtered=[convolve(a,k) for k in (green,horizontal,horizontal.T,opposite)]
        for y in range(2):
            for x in range(2):
                letter=pattern[y*2+x];c='RGB'.index(letter);sl=(slice(y,None,2),slice(x,None,2))
                r[sl+(c,)]=a[sl]
                if letter!='G':
                    r[sl+(1,)]=filtered[0][sl];r[sl+(2-c,)]=filtered[3][sl]
                else:
                    red_horizontal=positions['R'][0][0]==y
                    r[sl+(0,)]=filtered[1 if red_horizontal else 2][sl]
                    r[sl+(2,)]=filtered[2 if red_horizontal else 1][sl]
        return r
    def probe(a,pattern='RGGB',**kw):
        r=call('demosaic_probe',width=a.shape[1],height=a.shape[0],mosaic=a.ravel().tolist(),pattern=pattern,**kw)
        return np.array(r['pixels']).reshape(*a.shape,4)
    with tempfile.TemporaryDirectory(prefix='huelith-demosaic-') as folder:
        call('init',settings=folder)
        try:
            def kernels():
                rng=np.random.default_rng(740)
                for pattern in ('RGGB','BGGR','GRBG','GBRG'):
                    for shape in ((4,4),(6,8),(32,48)):
                        a=rng.uniform(-.5,4,shape).astype(np.float32)
                        actual=probe(a,pattern);expected=oracle(a,pattern)
                        assert np.max(np.abs(actual[:,:,:3]-expected))<1e-6
                        assert np.all(actual[:,:,3]==1)
                        for y in range(2):
                            for x in range(2):assert np.array_equal(actual[y::2,x::2,'RGB'.index(pattern[y*2+x])],a[y::2,x::2])
            test('independent_5x5_convolution_all_four_phases_edges_hdr_measured_samples',kernels)
            def constants():
                for pattern in ('RGGB','BGGR','GRBG','GBRG'):
                    a=np.empty((32,48));rgb=[.1,1.7,3.2]
                    for y in range(2):
                        for x in range(2):a[y::2,x::2]=rgb['RGB'.index(pattern[y*2+x])]
                    assert np.max(np.abs(probe(a,pattern)[:,:,:3]-rgb))<1e-6
            test('constant_chromatic_fields_preserved_with_unclipped_hdr',constants)
            def quality():
                y,x=np.mgrid[:128,:128];gray=.5+.2*np.sin(.38*x+.17*y)+.15*np.cos(.29*y-.21*x)
                actual=probe(gray)[:,:,:3]
                # Standard bilinear interpolation, including measured green samples.
                p=np.pad(gray,2,mode='reflect');b=np.empty_like(actual)
                for yy in range(2):
                    for xx in range(2):
                        for c,letter in enumerate('RGB'):
                            sl=(slice(yy,None,2),slice(xx,None,2));here='RGGB'[yy*2+xx]
                            if letter==here:v=gray
                            elif letter=='G':v=(p[1:-3,2:-2]+p[3:-1,2:-2]+p[2:-2,1:-3]+p[2:-2,3:-1])/4
                            elif here!='G':v=(p[1:-3,1:-3]+p[1:-3,3:-1]+p[3:-1,1:-3]+p[3:-1,3:-1])/4
                            elif (letter=='R' and yy==0) or (letter=='B' and yy==1):v=(p[2:-2,1:-3]+p[2:-2,3:-1])/2
                            else:v=(p[1:-3,2:-2]+p[3:-1,2:-2])/2
                            b[sl+(c,)]=v[sl]
                before=np.mean((b[2:-2,2:-2]-gray[2:-2,2:-2,None])**2)
                after=np.mean((actual[2:-2,2:-2]-gray[2:-2,2:-2,None])**2)
                # The paper's mean gain on Kodak is not a per-image bound. Measure
                # this fixture's actual gain and require a genuine improvement.
                (OUT/'synthetic-quality.json').write_text(json.dumps(dict(bilinear_mse=before,malvar_mse=after,gain_db=10*np.log10(before/after))),encoding='utf-8')
                assert after<before,(before,after)
            test('achromatic_fine_texture_error_reduced_against_standard_bilinear',quality)
            def versions():
                a=np.ones((4,4));kw=dict(width=4,height=4,mosaic=a.ravel().tolist())
                assert call('demosaic_probe',**kw)['quality']
                assert not call('demosaic_probe',**kw,recipe=dict(version=1))['quality']
                assert call('demosaic_probe',**kw,recipe=dict(version=1,demosaic='malvar-2004'))['quality']
                assert 'error' in raw('demosaic_probe',**kw,recipe=dict(demosaic='invalid'))
                assert 'error' in raw('demosaic_probe',**kw,cancelled=True)
                assert call('raw_cache_key',source='same',highQuality=True)!=call('raw_cache_key',source='same',highQuality=False)
            test('version_legacy_preservation_cache_identity_and_cancellation',versions)
            def real_raw():
                results=[]
                for high in (False,True):
                    path=Path(folder)/f'{high}.f32'
                    r=call('raw_develop',path=str(ROOT/'research/m0/samples/r10-5871.CR3'),highQuality=high,denoise=False,floatOutput=str(path),preview=str(OUT/('malvar.png' if high else 'legacy.png')))
                    a=np.memmap(path,dtype=np.float32,mode='r',shape=(r['height'],r['width'],4))
                    assert np.isfinite(a).all() and np.all(a[:,:,3]==1)
                    assert r['metadata']['backend']==('malvar-2004' if high else 'bilinear-reference')
                    results.append(r);del a
                (OUT/'real-raw.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
            test('real_r10_24mp_both_versioned_paths_finite_alpha_preview_timing',real_raw)
            def session_versions():
                for recipe,expected in (({},'malvar-2004'),(dict(version=1),'bilinear')):
                    call('raw_window',path=str(ROOT/'research/m0/samples/r10-5871.CR3'),
                         recipe=dict(recipe,denoise=False,directml=False))
                    deadline=time.monotonic()+90
                    while True:
                        call('pump',waitMs=20)
                        state=call('raw_window_state')
                        if state['ready']:break
                        assert time.monotonic()<deadline,state
                    assert state['recipe']['demosaic']==expected,state['recipe']
                    call('raw_window_state',accept=True)
                    saved=call('raw_window_state',save=str(Path(folder)/(expected+'.huelith')))
                    assert saved['recipe']['demosaic']==expected,saved
            test('new_and_legacy_actual_raw_dialog_commit_project_roundtrip',session_versions)
        finally:call('shutdown')
    report=dict(passed=all(r['passed'] for r in records),checks=records)
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
