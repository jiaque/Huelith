"""M2 full main-toolbar inventory and actual Qt option/preview interactions."""
import ctypes
import json
import os
from pathlib import Path
import tempfile
import traceback

ROOT = Path(__file__).resolve().parents[1]

def main():
    qt = Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles = [os.add_dll_directory(str(qt/'bin')), os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH'] = str(qt/'plugins')
    os.environ['QT_QPA_PLATFORM'] = 'windows'
    lib = ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn = lib.huelith_python_command
    fn.argtypes, fn.restype = [ctypes.c_char_p], ctypes.c_char_p
    out = ROOT/'docs/test-evidence/M2/ui-audit'
    out.mkdir(parents=True, exist_ok=True)
    results, inventory = [], {}
    def call(op, **kw):
        result = json.loads(fn(json.dumps(dict(op=op, **kw), ensure_ascii=False).encode()))
        if 'error' in result:
            raise RuntimeError(result['error'])
        return result
    def test(name, body):
        try:
            detail = body()
            results.append(dict(name=name, passed=True, detail=detail))
        except Exception:
            results.append(dict(name=name, passed=False, error=traceback.format_exc()))
        print(results[-1], flush=True)
    def scene(size=240):
        call('window_scene', width=size, height=size, layers=[dict(name='image', color='#404040', width=size, height=size)])
    def tool(index): return call('window_chrome', click=index)
    def control(name, value=True): return call('window_control', name=name, value=value)
    def key(value): return call('window_key', key=value)
    def drag(a, b):
        call('window_mouse', phase='press', x=a, y=a)
        call('window_mouse', phase='move', x=b, y=b)
        call('window_mouse', phase='release', x=b, y=b)
    def mask(x): return call('window_probe', x=x, y=x)['range']
    with tempfile.TemporaryDirectory(prefix='huelith-ui-audit-') as folder:
        call('init', settings=folder)
        call('window')
        def all_tools():
            scene()
            for index in range(29):
                chrome = tool(index)
                state = call('window_interaction_state')
                assert state['tool'] == index and state['checkedTools'] == 1
                inventory[str(index)] = dict(options=state['options'], controls=chrome['options'])
                shown = state['options']
                assert ('selectionOptions' in shown) == (index in (2,3,4,5,9,10,28)), (index, shown)
                assert ('photo_aligned' in shown) == (index in (16,17)), (index, shown)
                assert ('photo_allLayers' in shown) == (index == 14), (index, shown)
                assert ('move_autoSelect' in shown) == (index == 0), (index, shown)
            inventory['menus'] = chrome['menus']
            assert len({b['x'] for b in chrome['buttons']}) == 2
            for parent, label in (('滤镜','智能效果…'), ('文件','批量导出与队列…'), ('图像','曲线…')):
                assert any(parent in m['path'] and m['text'] == label for m in chrome['menus'])
            return dict(tools=29, menus=len(chrome['menus']))
        test('all_29_tools_context_options_and_reachable_menus', all_tools)
        def ranges():
            for mode, expected in enumerate(((False,True,True),(True,True,True),(True,False,False),(False,True,False))):
                scene();tool(2);control('selectionMode0');drag(20,120)
                control('selectionMode'+str(mode));drag(70,170)
                assert tuple(mask(p)>0 for p in (30,80,150)) == expected, mode
                chrome = call('window_chrome')
                checked = [c['name'] for c in chrome['options'] if c['name'].startswith('selectionMode') and c.get('checked')]
                assert checked == ['selectionMode'+str(mode)], checked
                key('Ctrl+Z'); assert mask(30)>0 and mask(150)==0
                key('Ctrl+Shift+Z');assert tuple(mask(p)>0 for p in (30,80,150)) == expected
            control('selectionMode0')
            call('window_capture', path=str(out/'selection-options.png'))
        test('four_selection_buttons_real_masks_undo_redo_exclusive', ranges)
        def diameter():
            scene();tool(7);control('brushDiameter',40)
            assert call('window_interaction_state')['radius'] == 20
            shown = next(x for x in call('window_chrome')['options'] if x['name']=='brushDiameter')
            assert shown['value']==40 and '像素' in shown['text']
            key(']')
            assert next(x for x in call('window_chrome')['options'] if x['name']=='brushDiameter')['value'] == call('window_interaction_state')['radius']*2
            call('window_capture', path=str(out/'brush-options.png'))
        test('brush_diameter_matches_pixels_and_bracket_shortcut', diameter)
        def tone_cancel():
            scene(3200)
            revision = call('window_interaction_state')['revision']
            result = call('window_tone_validation', accept=False, capture=str(out/'tone-preview.png'))
            assert result['found'] and result['presented'] and not result['timedOut'], result
            assert result['edits']==12 and result['maximumGapMs']<500, result
            assert call('window_interaction_state')['revision']==revision
            return result
        test('10mp_tone_latest_preview_heartbeat_cancel_no_commit', tone_cancel)
        def tone_commit():
            scene();before=call('window_probe',x=60,y=60)['rgba'];revision=call('window_interaction_state')['revision']
            result=call('window_tone_validation',accept=True)
            assert result['presented'] and not result['timedOut'],result
            after=call('window_probe',x=60,y=60)['rgba'];assert after[0]>before[0]
            assert call('window_interaction_state')['revision']==revision+1
            key('Ctrl+Z');assert call('window_probe',x=60,y=60)['rgba']==before
            key('Ctrl+Shift+Z');assert call('window_probe',x=60,y=60)['rgba']==after
            return result
        test('tone_accept_one_transaction_pixels_undo_redo',tone_commit)
        call('shutdown')
    (out/'inventory.json').write_text(json.dumps(inventory,ensure_ascii=False,indent=2),encoding='utf-8')
    (out/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
    if not all(x['passed'] for x in results):
        raise SystemExit(1)
if __name__ == '__main__':
    main()
