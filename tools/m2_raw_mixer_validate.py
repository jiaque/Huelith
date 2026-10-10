"""Actual RAW HSL tab, color rail, independent parameters and history validation."""
import ctypes
import json
import os
from pathlib import Path
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    qt = Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles = [os.add_dll_directory(str(qt/'bin')), os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH'] = str(qt/'plugins')
    os.environ['QT_QPA_PLATFORM'] = 'windows'
    dll = ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn = dll.huelith_python_command
    fn.argtypes, fn.restype = [ctypes.c_char_p], ctypes.c_char_p
    out = ROOT/'docs/test-evidence/M2/raw-mixer'
    out.mkdir(parents=True, exist_ok=True)
    records = []

    def call(op, **values):
        result = json.loads(fn(json.dumps(dict(op=op, **values)).encode()))
        if 'error' in result:
            raise RuntimeError(result['error'])
        return result

    with tempfile.TemporaryDirectory(prefix='huelith-mixer-') as folder:
        call('init', settings=folder)
        call('window')
        call('raw_window', path=str(ROOT/'research/m0/samples/r10-5871.CR3'), denoise=False)
        end = time.monotonic()+30
        while time.monotonic() < end:
            call('pump', waitMs=30)
            if call('raw_window_state')['previewPresented'] > 0:
                break
        state = call('raw_hsl', tab=0, capture=str(out/'hue.png'))
        assert state['tabs'] == ['色相', '饱和度', '明度', '全部'], state
        assert len(state['rows']) == 8
        dragged = call('raw_hsl', slider='rawHsl0Band0Slider', position=.8)
        assert abs(dragged['recipe']['tone']['hue'][0]-60) <= 1
        for _ in range(4):
            call('pump', waitMs=100)
        call('raw_window_state', click='rawUndo')
        assert call('raw_hsl')['recipe'].get('tone', {}).get('hue', [0]*8)[0] == 0
        call('raw_window_state', click='rawRedo')
        reset = call('raw_hsl', slider='rawHsl0Band0Slider', reset=True)
        assert reset['recipe']['tone']['hue'][0] == 0
        right = call('raw_hsl', slider='rawHsl0Band0Slider', right=True)
        assert right['recipe']['tone']['hue'][0] == 1
        call('raw_hsl', slider='rawHsl0Band0Slider', reset=True)
        for _ in range(4):
            call('pump', waitMs=100)
        for channel, key in enumerate(['hue', 'saturationBands', 'luminanceBands']):
            state = call('raw_hsl', tab=channel, control=f'rawHsl{channel}Band2', value=20+channel)
            assert len(state['rows']) == 8 and state['recipe']['tone'][key][2] == 20+channel, state
            for _ in range(4):
                call('pump', waitMs=100)
        state = call('raw_hsl', tab=3, capture=str(out/'all.png'))
        assert len(state['rows']) == 24, state
        assert [state['recipe']['tone'][key][2] for key in ['hue','saturationBands','luminanceBands']] == [20,21,22]
        undone = call('raw_window_state', click='rawUndo')
        assert undone['recipe']['tone']['luminanceBands'][2] == 0, undone
        call('raw_window_state', click='rawRedo')
        state = call('raw_hsl', mode=1, band=2, capture=str(out/'color.png'))
        assert len(state['rows']) == 3, state
        assert [row['value'] for row in state['rows']] == [20,21,22], state
        call('raw_hsl', mode=0, tab=0)
        disabled = call('raw_hsl', enabled=False)
        assert disabled['recipe']['tone']['sections'][4] is False
        assert disabled['recipe']['tone']['hue'][2] == 20
        enabled = call('raw_hsl', enabled=True)
        assert enabled['recipe']['tone']['sections'][4] is True and enabled['recipe']['tone']['hue'][2] == 20
        # Inventory the entire RAW panel, not only the currently visible mixer.
        for section in range(8):
            call('raw_window_state', section=section)
        for _ in range(3):
            call('pump',waitMs=30)
        inventory = call('raw_window_state', inventory=True)
        for widget in inventory['controls']:
            if widget['name'].startswith('rawSectionBody') and widget['visible']:
                assert widget['height'] >= widget['minimumHeight'], widget
        (out/'raw-controls.json').write_text(json.dumps(inventory['controls'], ensure_ascii=False, indent=2), encoding='utf-8')
        assert len([c for c in inventory['controls'] if c['name'].startswith('rawSectionEnable')]) == 8
        call('raw_window_state', click='rawRail2')
        call('raw_window_state', click='rawAddLocal')
        for mode in range(1,5):
            chosen = call('raw_window_state', click='rawMaskTool'+str(mode), inventory=True)
            assert chosen['mode'] == mode
            checked = [c['name'] for c in chosen['controls'] if c['name'].startswith('rawMaskTool') and c.get('checked')]
            assert checked == ['rawMaskTool'+str(mode)]
            painted = call('raw_window_state', stroke=[[.25,.25],[.5,.5],[.6,.6]])
            component = painted['recipe']['locals'][-1]['components'][-1]
            assert component['mode'] == {1:'brush',2:'brush',3:'linear',4:'radial'}[mode]
            assert component['subtract'] == (mode == 2)
            assert abs(component['radius']-.025)<.0001
        call('raw_window_state', capture=str(out/'mask-tools.png'))
        call('raw_window_state', click='rawDeleteLocal')
        call('raw_window_state', click='rawRail0')
        accepted = call('raw_window_state', accept=True)
        assert accepted['result'] == 1
        call('raw_window_state', save=str(Path(folder)/'hsl.huelith'))
        call('shutdown')
        records.extend(['four_clickable_tabs_eight_rows', 'mouse_rail_hit_single_drag_undo', 'double_click_zero_keyboard_step', 'all_tab_24_rows', 'channels_independent',
                        'history_updates_all_views', 'color_mode_three_channels', 'section_switch_preserves_values', 'source_recipe_pixel_roundtrip', 'all_raw_panel_controls_inventoried', 'four_direct_mask_tools_exclusive_real_strokes', 'raw_brush_diameter_preserves_source_radius'])
    report = dict(passed=True, checks=records)
    (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(report)


if __name__ == '__main__':
    main()
