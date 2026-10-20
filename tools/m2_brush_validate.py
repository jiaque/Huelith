"""Advanced brush numerical contracts and real Qt dialog/painting transactions.

All rendering is production C++; Python checks geometry, event independence,
pressure/flow behavior, preset persistence, selection and undo.
"""
import base64, ctypes, io, json, os, subprocess, sys, tempfile, traceback
from pathlib import Path
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'docs/test-evidence/M2/brushes'

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    qt = Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles = [os.add_dll_directory(str(qt/'bin')), os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH'] = str(qt/'plugins')
    lib = ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn = lib.huelith_python_command; fn.argtypes = [ctypes.c_char_p]; fn.restype = ctypes.c_char_p
    def raw(op, **kw): return json.loads(fn(json.dumps(dict(op=op, **kw), ensure_ascii=False).encode()))
    def call(op, **kw):
        value = raw(op, **kw); assert 'error' not in value, value; return value
    if len(sys.argv)>1:
        call('init', settings=sys.argv[1]); call('window'); call('window_scene',layers=[dict(color='transparent')])
        result=call('window_brush'); call('shutdown')
        print(json.dumps(result,ensure_ascii=True)); return
    records=[]
    def test(name, body):
        try: body(); records.append(dict(name=name,passed=True))
        except Exception: records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(records[-1],flush=True)
    def settings(**kw): return dict(version=1,pressureSize=False,hardness=1,**kw)
    def probe(s, points, name=None, **kw):
        if name: kw['capture']=str(OUT/(name+'.png'))
        return call('brush_probe',settings=s,points=points,**kw)
    def alpha(name): return np.asarray(Image.open(OUT/(name+'.png')))[:,:,3]
    with tempfile.TemporaryDirectory(prefix='huelith-brush-') as tmp:
        call('init',settings=tmp)
        try:
            def spacing():
                s=settings(diameter=20,spacing=.5,flow=.2)
                a=probe(s,[[50,100],[450,100]])
                b=probe(s,[[x,100] for x in range(50,451)])
                assert a['hash']==b['hash'] and a['stamps']==b['stamps']==41,(a,b)
                still=probe(s,[[50,100]]*20)
                assert still['stamps']==1,still
                sparse=probe(settings(diameter=20,spacing=2),[[50,100],[450,100]],'spacing')
                assert sparse['stamps']==11 and alpha('spacing')[100,70]==0
            test('distance_spacing_independent_of_mouse_event_frequency',spacing)
            def geometry():
                for angle in [0,90]:
                    probe(settings(diameter=80,roundness=.25,angle=angle),[[120,120]],f'ellipse-{angle}')
                    yy,xx=np.nonzero(alpha(f'ellipse-{angle}'))
                    assert abs((xx.max()-xx.min()+1)-(80 if angle==0 else 20))<=2
                    assert abs((yy.max()-yy.min()+1)-(20 if angle==0 else 80))<=2
                s=settings(diameter=60)
                h=probe(s,[[100,100]])['hash']; s['tip']=1
                assert probe(s,[[100,100]])['hash']!=h
            test('diameter_angle_roundness_and_nonround_tip_geometry',geometry)
            def pressure():
                s=settings(diameter=80);s['pressureSize']=True
                probe(s,[[100,100,.25]],'pressure-small');probe(s,[[100,100,1]],'pressure-full')
                assert np.count_nonzero(alpha('pressure-small'))<np.count_nonzero(alpha('pressure-full'))*.1
                s['pressureSize']=False;s['pressureFlow']=True
                a=probe(s,[[100,100,.25]],samples=[[100,100]])['samples'][0]
                b=probe(s,[[100,100,1]],samples=[[100,100]])['samples'][0]
                assert abs(a-64)<=2 and b==255,(a,b)
                s['pressureFlow']=False;s['flow']=.25;s['spacing']=.1
                assert probe(s,[[80,100],[120,100]],samples=[[100,100]])['samples'][0]>a
            test('pressure_size_pressure_flow_and_flow_buildup',pressure)
            def dynamics():
                s=settings(diameter=30,scatter=2,scatterBoth=True,count=4,sizeJitter=.8,angleJitter=150,roundness=.3)
                a=probe(s,[[50,128],[450,128]],'scatter',seed=32)
                assert a['hash']==probe(s,[[50,128],[450,128]],seed=32)['hash']
                assert a['hash']!=probe(s,[[50,128],[450,128]],seed=33)['hash']
                yy,_=np.nonzero(alpha('scatter')); assert yy.max()-yy.min()>70
                hashes=[]
                for texture in range(4): hashes.append(probe(settings(diameter=80,texture=texture,textureDepth=1),[[100,100]])['hash'])
                assert len(set(hashes))==4
            test('seeded_scatter_size_angle_count_and_real_texture',dynamics)
            def smoothing():
                s=settings(diameter=6,smoothing=.8,catchUp=False)
                points=[[50,128],[250,128]]
                plain=probe(settings(diameter=6),points)
                smoothed=probe(s,points,'smoothing')
                assert 195<smoothed['position'][0]<225 and smoothed['stamps']<plain['stamps'],smoothed
                s['catchUpEnd']=True
                end=probe(s,points,finish=True);assert end['position']==[250,128]
                s['catchUpEnd']=False;s['catchUp']=True
                caught=probe(s,points,idle=[.1]*10);assert caught['position'][0]>249.9
                s['pulledString']=True
                pulled=probe(s,points,idle=[.1]*10);assert abs(pulled['position'][0]-210)<1e-6,pulled
                zoomed=probe(s,points,zoom=2);assert abs(zoomed['position'][0]-230)<1e-6
                s['adjustZoom']=False;assert probe(s,points,zoom=2)['position']==pulled['position']
                air=settings(diameter=30,flow=.05,airbrush=True)
                a=probe(air,[[100,100]],samples=[[100,100]])
                b=probe(air,[[100,100]],idle=[.04]*25,samples=[[100,100]])
                c=probe(air,[[100,100]],idle=[1],samples=[[100,100]])
                assert b['stamps']==c['stamps']==26 and b['hash']==c['hash'] and b['samples'][0]>a['samples'][0]+100
                air['airbrush']=False;assert probe(air,[[100,100]],idle=[1])['stamps']==1
            test('smoothing_pull_string_catch_up_end_zoom_and_time_based_airbrush',smoothing)
            def presets():
                default=probe({},[]); assert len(default['presets'])==20
                for preset in default['presets']:
                    s=preset['settings'];a=probe(s,[[50,128],[450,128]])
                    assert probe(a['settings'],[[50,128],[450,128]])['hash']==a['hash']
                custom=Image.new('L',(80,20),255);buf=io.BytesIO();custom.save(buf,format='PNG')
                s=settings(tip=6,customTip=base64.b64encode(buf.getvalue()).decode(),diameter=80)
                a=probe(s,[[120,120]],'custom');yy,xx=np.nonzero(alpha('custom'))
                assert 3.5<(xx.max()-xx.min()+1)/(yy.max()-yy.min()+1)<4.5
                assert probe(a['settings'],[[120,120]])['hash']==a['hash']
                for key,value in [('diameter',0),('spacing',0),('roundness',0),('tip',8),('count',17),('scatter',11),('flow','bad'),('unknown',1)]:
                    s=settings();s[key]=value;assert 'error' in raw('brush_probe',settings=s)
                huge=Image.new('L',(1025,1),255);buf=io.BytesIO();huge.save(buf,format='PNG')
                assert 'error' in raw('brush_probe',settings=settings(tip=6,customTip=base64.b64encode(buf.getvalue()).decode()))
            test('twenty_presets_custom_tip_aspect_roundtrip_and_invalid_inputs',presets)
            call('window');call('window_scene',width=512,height=256,layers=[dict(name='paint',color='transparent')]);call('window_key',key='B')
            def dialog():
                before=call('window_brush')
                r=call('window_brush',dialog=True,brushDiameter=70,brushSpacing=50,brushRoundness=30,saveName='cancelled',accept=False)
                assert r['ready'] and r['settings']==before['settings'] and r['saved']==[] and r['history']==before['history']
                r=call('window_brush',dialog=True,brushDiameter=70,brushSpacing=50,brushRoundness=30,brushAngle=25,saveName='我的扁平笔',accept=True,capture=str(OUT/'brush-settings.png'))
                assert r['ready'] and r['settings']['diameter']==70 and r['settings']['roundness']==.3 and r['history']==before['history']
                assert len(r['saved'])==1 and r['saved'][0]['name']=='我的扁平笔'
                child=subprocess.run([sys.executable,__file__,tmp],capture_output=True,text=True,encoding='utf-8',timeout=45)
                assert child.returncode==0,child.stderr
                loaded=json.loads(child.stdout.strip().splitlines()[-1]);assert loaded['settings']==r['settings'] and loaded['saved']==r['saved']
                r=call('window_brush',dialog=True,preset=20,remove=True,accept=False)
                assert len(r['saved'])==1
            test('actual_dialog_cancel_apply_custom_preset_and_new_process_persistence',dialog)
            def painting():
                call('window_brush',settings=settings(diameter=24,spacing=.2,opacity=.5))
                before=call('window_brush');call('window_gesture',x=50,y=128,endX=450,endY=128)
                painted=call('window_brush',pixels=str(OUT/'paint-opacity.png'))
                assert painted['history']==before['history']+1 and painted['hash']!=before['hash']
                a=alpha('paint-opacity');assert 125<=a.max()<=129,a.max()
                call('window_key',key='Ctrl+Z');assert call('window_brush')['hash']==before['hash']
                call('window_key',key='Ctrl+Shift+Z');assert call('window_brush')['hash']==painted['hash']
                call('window_key',key='E');call('window_gesture',x=250,y=128,endX=250,endY=128)
                call('window_brush',pixels=str(OUT/'erase.png'));assert alpha('erase')[128,250]<a[128,250]
                call('window_key',key='Ctrl+Z');assert call('window_brush')['hash']==painted['hash']
            test('actual_paint_opacity_one_history_step_undo_redo_and_erase',painting)
            def airbrush_ui():
                call('window_scene',width=512,height=256,layers=[dict(name='paint',color='transparent')]);call('window_key',key='B')
                call('window_brush',settings=settings(diameter=30,flow=.04,airbrush=True))
                before=call('window_brush')['history']
                call('window_mouse',phase='press',x=250,y=128)
                call('pump',waitMs=480)
                call('window_mouse',phase='release',x=250,y=128)
                r=call('window_brush',pixels=str(OUT/'airbrush-hold.png'))
                assert r['history']==before+1 and alpha('airbrush-hold').max()>40,(r,alpha('airbrush-hold').max())
                old=r['hash'];call('pump',waitMs=160);assert call('window_brush')['hash']==old
                call('window_key',key='Ctrl+Z');assert call('window_brush')['history']==before
            test('real_timer_stationary_airbrush_release_and_single_undo',airbrush_ui)
        finally: call('shutdown')
    (OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
    raise SystemExit(not all(r['passed'] for r in records))

if __name__=='__main__': main()
