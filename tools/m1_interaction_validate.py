"""Python UI verification through the existing M0/M1 production DLL adapter."""
import argparse, ctypes, json, os, tempfile, traceback
from pathlib import Path
from PIL import Image, ImageDraw
ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--build',default='windows-release');parser.add_argument('--platform',default='windows');args=parser.parse_args()
    output=ROOT/('docs/test-evidence/M1/interaction' if args.platform=='windows' else 'docs/test-evidence/M1/interaction-offscreen');output.mkdir(parents=True,exist_ok=True)
    qt=Path(r'D:\Program Files\Qt\6.10.3\msvc2022_64')
    handles=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build'/args.build))]
    os.environ['QT_PLUGIN_PATH']=str(qt/'plugins');os.environ['QT_QPA_PLATFORM']=args.platform
    library=ctypes.CDLL(str(ROOT/'build'/args.build/'huelith_python_driver.dll'));command=library.huelith_python_command
    command.argtypes=[ctypes.c_char_p];command.restype=ctypes.c_char_p
    transcript=[];results=[]
    def call(op,**values):
        request=dict(op=op,**values);result=json.loads(command(json.dumps(request,ensure_ascii=False).encode()))
        transcript.append(dict(request=request,result=result))
        if 'error' in result:raise RuntimeError(result['error'])
        return result
    def state():return call('window_interaction_state')
    def key(value,**kw):return call('window_key',key=value,**kw)
    def action(text):return call('window_action',text=text)
    def test(name,fn):
        try:fn();results.append(dict(name=name,passed=True))
        except Exception:results.append(dict(name=name,passed=False,error=traceback.format_exc()))
    with tempfile.TemporaryDirectory() as folder:
        call('init',settings=folder);call('window');call('window_new',width=800,height=600)
        image=Image.new('RGB',(400,240),'#32434e');draw=ImageDraw.Draw(image)
        draw.rounded_rectangle((135,30,265,218),radius=18,fill='#d1a962');draw.rectangle((160,15,240,42),fill='#242c34');draw.text((167,110),'HUELITH',fill='#242c34')
        sample=Path(folder)/'product.png';image.save(sample);call('window_clipboard',path=str(sample))
        def paste():
            key('Ctrl+V');s=state();assert s['width']==400 and s['height']==240,s
            assert s['tool']==0 and s['checkedTools']==1 and s['tx']==200 and s['ty']==180,s
            assert 'move_transformControls' in s['options'] and 'brushOptions' not in s['options'],s
            assert len(s['handles'])==8
        test('paste_actual_shortcut_centered_layer_handles',paste)
        def scale():
            old=state();call('window_gesture',handle=4,scale=.5);s=state()
            assert s['pending'] and s['revision']==old['revision'],s
            assert abs(s['sx']-.5)<1e-6 and abs(s['sy']-.5)<1e-6,s
            assert s['width']==400 and s['height']==240
            key('Return');s=state();assert not s['pending'] and s['revision']==old['revision']+1,s
            key('Ctrl+Z');assert abs(state()['sx']-1)<1e-6
            key('Ctrl+Shift+Z');assert abs(state()['sx']-.5)<1e-6
        test('handle_scale_preview_enter_one_undo_redo_original_pixels',scale)
        def cancel():
            old=state();key('Ctrl+T');assert state()['pending']
            call('window_gesture',handle=4,scale=.5);key('Esc');s=state()
            assert not s['pending'] and s['revision']==old['revision'] and s['sx']==old['sx'],s
        test('ctrl_t_escape_restores_original',cancel)
        def modifiers():
            old=state();call('window_gesture',handle=4,scaleX=.5,scaleY=.75,modifiers=0x02000000);s=state()
            assert abs(s['sx']-old['sx']*.5)<1e-6 and abs(s['sy']-old['sy']*.75)<1e-6,s
            key('Esc');old=state();call('window_gesture',handle=4,scale=.75,modifiers=0x08000000);s=state()
            assert abs(s['sx']-old['sx']*.5)<1e-6,s
            assert abs((s['tx']+s['width']*s['sx']/2)-(old['tx']+old['width']*old['sx']/2))<1e-6,s
            key('Esc')
        test('shift_nonuniform_alt_center_scale',modifiers)
        def rotated_view():
            old=state();call('window_view',rotation=25,zoom=.9);call('window_gesture',handle=4,scale=.5);s=state()
            assert abs(s['sx']-old['sx']*.5)<1e-6 and abs(s['sy']-old['sy']*.5)<1e-6,s
            key('Esc');call('window_view',rotation=0,zoom=1)
            call('window_gesture',handle=4,rotate=27,modifiers=0x02000000);s=state()
            assert abs(s['m12']-old['sx']*.5)<1e-6,s
            key('Esc')
        test('rotated_zoomed_view_scale_and_shift_rotation_snap',rotated_view)
        def tools():
            for shortcut,tool,option in [('B',7,'brushOptions'),('E',8,'brushOptions'),('M',2,'selectionOptions'),('Shift+M',3,'selectionOptions'),('L',4,'selectionOptions'),('Shift+L',5,'selectionOptions'),('P',6,None),('H',1,None),('W',9,'selectionOptions'),('V',0,'move_transformControls')]:
                key(shortcut);s=state();assert s['tool']==tool and s['checkedTools']==1,(shortcut,s)
                if option:assert option in s['options'],(shortcut,s)
                if tool not in (7,8):assert 'brushOptions' not in s['options'],s
        test('tool_shortcuts_cycles_exclusive_icons_context_options',tools)
        def navigation():
            key('B');old=state();key('Space');call('window_gesture',x=20,y=20,endX=60,endY=50);key('Space',release=True);s=state()
            assert s['viewX']!=old['viewX'] and s['viewY']!=old['viewY'] and s['revision']==old['revision'] and s['tool']==7,s
        test('space_temporary_hand_preserves_tool_and_pixels',navigation)
        def wheel():
            old=state();call('window_wheel');s=state();assert s['zoom']==old['zoom'] and s['viewY']!=old['viewY']
            call('window_wheel',modifiers=0x08000000);assert state()['zoom']>s['zoom']
        test('wheel_scroll_alt_wheel_zoom',wheel)
        def colors():
            key('D');assert state()['foreground']=='#000000';key('X');assert state()['foreground']=='#ffffff';key('D')
        test('foreground_default_swap',colors)
        def selection():
            key('M');call('window_gesture',x=20,y=20,endX=120,endY=100);first=state()['rangeCount'];assert first>0
            call('window_gesture',x=200,y=200,endX=250,endY=250,modifiers=0x02000000);assert state()['rangeCount']>first
            call('window_gesture',x=200,y=200,endX=250,endY=250,modifiers=0x08000000);assert state()['rangeCount']==first
            key('Ctrl+D');assert state()['rangeCount']==0
            key('Ctrl+A');assert state()['rangeCount']==800*600
            key('Ctrl+Shift+I');assert state()['rangeCount']==0
            key('Ctrl+D')
        test('selection_shift_add_alt_subtract_select_all_invert_deselect',selection)
        def nudge():
            key('V');old=state();key('Right');assert state()['tx']==old['tx']+1
            key('Shift+Down');assert state()['ty']==old['ty']+10
        test('move_arrows_one_pixel_shift_ten',nudge)
        def radius():
            key('B');old=state()['radius'];key(']');assert state()['radius']==old+1;key('[');assert state()['radius']==old
        test('brush_bracket_size',radius)
        def copy():
            key('Ctrl+C');key('Ctrl+V');s=state();assert s['width']==200 and s['height']==120,s
            shortcuts={a['text']:a['shortcut'] for a in s['shortcuts']}
            assert shortcuts['复制']=='Ctrl+C' and shortcuts['复制合成']=='Ctrl+Shift+C'
        test('copy_active_layer_versus_copy_merged_shortcuts',copy)
        def move_preview():
            key('V');old=state();cx=old['tx']+old['width']*old['sx']/2;cy=old['ty']+old['height']*old['sy']/2
            call('window_gesture',x=cx,y=cy,endX=cx+40,endY=cy+15);s=state()
            assert not s['pending'] and s['revision']==old['revision']+1 and abs(s['tx']-old['tx']-40)<1e-6,s
            key('Ctrl+Z');s=state();assert abs(s['tx']-old['tx'])<1e-6,s
            key('Ctrl+J');assert call('window_state')['layers']==3;key('Ctrl+Z')
        test('move_drag_single_undo_duplicate_shortcut',move_preview)
        def large_paste():
            image.resize((1600,960)).save(sample);call('window_clipboard',path=str(sample));key('Ctrl+V');s=state()
            assert s['width']==1600 and s['height']==960,s
            for x,y in s['handles']:assert 0<x<s['viewportWidth'] and 0<y<s['viewportHeight'],s
            key('Ctrl+Z');key('Ctrl+0')
        test('large_paste_handles_visible_without_resampling',large_paste)
        def capture():
            key('V');key('Ctrl+0');call('window_capture',path=str(output/f'{args.build}-move.png'))
            call('window_gesture',handle=4,scale=.8);call('window_capture',path=str(output/f'{args.build}-transform.png'));key('Esc')
        test('own_window_visual_evidence',capture)
        call('shutdown')
    report=dict(build=args.build,passed=sum(x['passed'] for x in results),total=len(results),tests=results)
    (output/f'{args.build}-results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    (output/f'{args.build}-commands.json').write_text(json.dumps(transcript,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2));return int(report['passed']!=report['total'])
if __name__=='__main__':raise SystemExit(main())
