"""Snapping: real production mouse events and preferences transaction, no test exe."""
import ctypes,json,os,tempfile,traceback,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/test-evidence/M2/snapping'
def main():
    qt=Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins');os.environ['QT_QPA_PLATFORM']='windows'
    lib=ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'));fn=lib.huelith_python_command
    fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
    def call(op,**kw):
        r=json.loads(fn(json.dumps(dict(op=op,**kw)).encode()))
        if 'error' in r:raise RuntimeError(r['error'])
        return r
    def pref(accept=True,**kw):call('window_action',text='参考线与网格…',dialog=dict(accept=accept,**kw))
    def only(kind,**extra):
        kw={f'snap{k}Preference':k==kind for k in ['Guides','Grid','Layers','Document']};kw.update(extra);pref(**kw)
    def scene(vertical=(),horizontal=(),other=None,zoom=1):
        layers=[dict(name='moving',color='#aa4455',width=100,height=80,x=60,y=70)]
        if other:layers+=other
        call('window_scene',width=600,height=400,layers=layers,vertical=list(vertical),horizontal=list(horizontal))
        call('window_view',zoom=zoom)
        call('window_control',name='transformControls',value=False)
    def drag(dx,dy=0,ctrl=False):
        call('window_mouse',phase='press',x=110,y=110)
        for phase in ['move','release']:call('window_mouse',phase=phase,x=110+dx,y=110+dy,modifiers=0x04000000 if ctrl else 0)
        return call('window_interaction_state')
    if len(sys.argv)>1:
        call('init',settings=sys.argv[1]);call('window');scene();print(json.dumps(call('canvas_preferences_state')));call('shutdown');return 0
    OUT.mkdir(parents=True,exist_ok=True);records=[]
    def test(name,body):
        try:body();records.append(dict(name=name,passed=True))
        except Exception:records.append(dict(name=name,passed=False,error=traceback.format_exc()))
        print(json.dumps(records[-1],ensure_ascii=False),flush=True)
    with tempfile.TemporaryDirectory(prefix='huelith-snap-') as folder:
        call('init',settings=folder);call('window');scene()
        def guides():
            only('Guides');scene(vertical=[200,204],horizontal=[200])
            s=drag(136,126);assert abs(s['tx']-200)<1e-6 and abs(s['ty']-200)<1e-6,s
            call('window_key',key='Ctrl+Z');s=call('window_interaction_state');assert s['tx']==60 and s['ty']==70,s
            scene(vertical=[200]);s=drag(136,ctrl=True);assert abs(s['tx']-196)<1e-6,s
            scene(vertical=[200]);call('window_action',text='显示参考线');s=drag(136);assert abs(s['tx']-196)<1e-6,s
            call('window_action',text='显示参考线')
        test('nearest_guide_not_summed_ctrl_override_hidden_guides_undo',guides)
        def document():
            only('Document');scene();s=drag(-56);assert abs(s['tx'])<1e-6,s
            scene();s=drag(437);assert abs(s['tx']-500)<1e-6,s
            scene();s=drag(0,-67);assert abs(s['ty'])<1e-6,s
        test('document_edges_both_axes',document)
        def layers():
            only('Layers');other=[dict(name='target',color='#4466bb',width=80,height=60,x=280,y=220)]
            scene(other=other);s=drag(116,67);assert abs(s['tx']-180)<1e-6 and abs(s['ty']-140)<1e-6,s
            scene(other=[dict(other[0],visible=False)]);s=drag(116);assert abs(s['tx']-176)<1e-6,s
            scene(other=[dict(name='hidden group',kind=5,visible=False,children=other)]);s=drag(116);assert abs(s['tx']-176)<1e-6,s
        test('layer_bounds_and_hidden_group_exclusion',layers)
        def grid():
            only('Grid',gridSpacingPreference=100,gridSubdivisionsPreference=4)
            scene();call('canvas_preferences_state',grid=True);s=drag(137);assert abs(s['tx']-200)<1e-6,s
            scene();call('canvas_preferences_state',grid=False);s=drag(137);assert abs(s['tx']-197)<1e-6,s
        test('visible_grid_subdivision_and_hidden_grid',grid)
        def other_tools():
            only('Guides');scene(vertical=[40,200],horizontal=[40,200])
            call('window_key',key='M')
            for phase,x,y in [('press',44,44),('move',196,196),('release',196,196)]:call('window_mouse',phase=phase,x=x,y=y)
            count=call('window_interaction_state')['rangeCount'];assert count==25600,count
            call('window_scene',width=600,height=400,vertical=[40,200],horizontal=[40,200],layers=[dict(name='background',color='#aaaaaa',backgroundLayer=True)])
            call('window_key',key='C')
            for phase,x,y in [('press',44,44),('move',196,196),('release',196,196)]:call('window_mouse',phase=phase,x=x,y=y)
            call('window_key',key='Return');s=call('window_interaction_state');assert s['width']==160 and s['height']==160,s
        test('selection_and_crop_creation_snap_to_guides',other_tools)
        def distance():
            only('Guides',snapDistancePreference=8)
            scene(vertical=[200],zoom=2);s=drag(135);assert abs(s['tx']-195)<1e-6,s
            scene(vertical=[200],zoom=.5);s=drag(130);assert abs(s['tx']-200)<1e-6,s
            pref(snapDistancePreference=2);scene(vertical=[200]);s=drag(137);assert abs(s['tx']-197)<1e-6,s
            pref(snapDistancePreference=32,accept=False);assert call('canvas_preferences_state')['snapDistance']==2
            s=json.loads(subprocess.check_output([sys.executable,__file__,folder],text=True).strip().splitlines()[-1])
            assert s['snapDistance']==2 and s['snapGuides'] and not s['snapLayers'],s
            pref(capture=str(OUT/'snapping-preferences.png'))
        test('screen_pixel_tolerance_zoom_cancel_and_new_process_persistence',distance)
        call('shutdown')
    report=dict(passed=sum(r['passed'] for r in records),total=len(records),tests=records)
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    return int(report['passed']!=report['total'])
if __name__=='__main__':raise SystemExit(main())
