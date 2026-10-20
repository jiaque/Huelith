"""NumPy inverse-homography oracle and production RAW geometry interactions."""
import ctypes
import json
import os
from pathlib import Path
import tempfile
import time
import traceback
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/test-evidence/M2/raw-geometry'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins');os.environ['QT_QPA_PLATFORM']='windows'
    dll=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn=dll.huelith_python_command;fn.argtypes,fn.restype=[ctypes.c_char_p],ctypes.c_char_p
    def raw(op,**kw):return json.loads(fn(json.dumps(dict(op=op,**kw)).encode()))
    def call(op,**kw):
        result=raw(op,**kw);assert 'error' not in result,result;return result
    records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True));print('PASS',name,flush=True)
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()));print(records[-1]['error'],flush=True)
    def wait():
        end=time.monotonic()+40
        while time.monotonic()<end:
            call('pump',waitMs=30);s=call('raw_window_state')
            if s['previewPresented']>0 and s['previewPresented']==s['previewRequested']:return s
        raise AssertionError(s)
    with tempfile.TemporaryDirectory(prefix='huelith-geometry-') as folder:
        call('init',settings=folder)
        try:
            def identity():
                assert call('geometry_probe')['identity']
                assert call('geometry_probe',settings=dict(version=1,enabled=False,rotation=35))['identity']
                for s in (dict(version=1,vertical=101),dict(version=1,scale=0),dict(version=1,rotation=181),dict(version=2),dict(version=1,flipHorizontal=1)):
                    assert 'error' in raw('geometry_probe',settings=s)
                assert 'error' in raw('geometry_probe',cancelled=True)
            test('identity_disabled_validation_cancel',identity)
            def oracle():
                rng=np.random.default_rng(342)
                yy,xx=np.mgrid[:48,:64];grid=np.stack([xx.ravel(),yy.ravel(),np.ones(64*48)])
                cx,cy=31.5,23.5;r=np.hypot(cx,cy)
                n=np.array([[1/r,0,-cx/r],[0,1/r,-cy/r],[0,0,1]])
                cases=[dict(rotation=180),dict(flipHorizontal=True),dict(flipVertical=True),dict(rotation=90)]
                for _ in range(20):
                    cases.append(dict(vertical=float(rng.uniform(-60,60)),horizontal=float(rng.uniform(-60,60)),rotation=float(rng.uniform(-60,60)),aspect=float(rng.uniform(-40,40)),scale=float(rng.uniform(70,150)),offsetX=float(rng.uniform(-10,10)),offsetY=float(rng.uniform(-10,10))))
                for s in cases:
                    angle=np.deg2rad(s.get('rotation',0));a=2**(s.get('aspect',0)/100);scale=s.get('scale',100)/100
                    rotation=np.array([[np.cos(angle),-np.sin(angle),0],[np.sin(angle),np.cos(angle),0],[0,0,1]])
                    sizing=np.diag([r*scale*a*(-1 if s.get('flipHorizontal') else 1),r*scale/a*(-1 if s.get('flipVertical') else 1),1])
                    trans=np.array([[1,0,cx+s.get('offsetX',0)/100*64],[0,1,cy+s.get('offsetY',0)/100*48],[0,0,1]])
                    perspective=np.array([[1,0,0],[0,1,0],[s.get('horizontal',0)/180,s.get('vertical',0)/180,1]])
                    points=np.linalg.solve(trans@rotation@sizing@perspective@n,grid);points/=points[2]
                    px,py=points[:2];valid=(px>=-1e-7)&(py>=-1e-7)&(px<=63+1e-7)&(py<=47+1e-7)
                    expected=np.stack([np.clip(px,0,63)/63*.5,np.clip(py,0,47)/47*.5,np.full_like(px,.1),np.full_like(px,.5)],axis=1);expected[~valid]=0
                    actual=np.array(call('geometry_probe',alpha=True,settings=dict(version=1,**s))['pixels'])
                    assert np.max(np.abs(actual-expected))<1e-6,s
            test('independent_numpy_homography_24_cases_flips_rotation_hdr_alpha',oracle)
            def crop():
                for angle in (-45,-20,20,45,90):
                    result=call('geometry_probe',settings=dict(version=1,rotation=angle,constrainCrop=True))
                    x,y,w,h=result['crop'];pixels=np.array(result['pixels']).reshape(48,64,4)
                    assert 0<w<=64 and 0<h<=48
                    assert pixels[y:y+h,x:x+w,3].min()>.9999
            test('constrain_crop_contains_no_transparent_pixels_five_rotations',crop)
            call('window');call('raw_window',path=str(ROOT/'research/m0/samples/r10-5871.CR3'),denoise=False);wait()
            def gui():
                call('raw_window_state',click='rawRail1');call('raw_window_state',click='rawGeometryExpand')
                s=call('raw_window_state',control='rawGeometryRotation',value=20)
                assert s['recipe']['geometry']['rotation']==20
                wait();call('pump',waitMs=300)
                s=call('raw_window_state',click='rawGeometryConstrain');assert s['recipe']['geometry']['constrainCrop']
                wait()
                s=call('raw_window_state',click='rawUndo');assert not s['recipe']['geometry']['constrainCrop'] and s['recipe']['geometry']['rotation']==20
                call('raw_window_state',click='rawRedo');wait()
                s=call('raw_window_state',click='rawGeometryFlipHorizontal');assert s['recipe']['geometry']['flipHorizontal']
                wait();call('raw_window_state',widget='rawGeometryRotation',capture=str(OUT/'geometry.png'))
                s=call('raw_window_state',accept=True);assert s['result']==1,s
                call('raw_window_state',save=str(Path(folder)/'geometry.huelith'))
            test('real_raw_geometry_controls_undo_crop_flip_source_roundtrip',gui)
        finally:call('shutdown')
    report=dict(passed=all(r['passed'] for r in records),checks=records)
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)
    return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
