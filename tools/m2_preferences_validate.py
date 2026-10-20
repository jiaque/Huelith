"""Test preference transactions and their actual canvas rendering using production Qt."""
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import traceback

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'docs/test-evidence/M2/preferences'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    qt = Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles = [os.add_dll_directory(str(qt/'bin')), os.add_dll_directory(str(ROOT/'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH'] = str(qt/'plugins')
    os.environ['QT_QPA_PLATFORM'] = 'windows'
    dll = ctypes.CDLL(str(ROOT/'build/windows-release/huelith_python_driver.dll'))
    fn = dll.huelith_python_command
    fn.argtypes, fn.restype = [ctypes.c_char_p], ctypes.c_char_p

    def call(op, **values):
        result = json.loads(fn(json.dumps(dict(op=op, **values)).encode()))
        if 'error' in result:
            raise RuntimeError(result['error'])
        return result

    def state(**values):
        return call('canvas_preferences_state', **values)

    def preferences(name='工具与画面操控…', accept=True, **values):
        call('window_action', text=name, dialog=dict(accept=accept, **values))

    if len(sys.argv) == 3 and sys.argv[1] == '--probe':
        call('init', settings=sys.argv[2])
        call('window')
        call('window_scene', width=600, height=400, layers=[])
        print(json.dumps(state()))
        call('shutdown')
        return

    results = []

    def test(name, body):
        try:
            body()
            results.append(dict(name=name, passed=True))
        except Exception:
            results.append(dict(name=name, passed=False, error=traceback.format_exc()))
        print(results[-1], flush=True)

    with tempfile.TemporaryDirectory(prefix='huelith-preferences-') as folder:
        call('init', settings=folder)
        call('window')
        call('window_scene', width=600, height=400, layers=[])
        call('window_view', zoom=1)
        revision = state()['revision']

        def units():
            expected = [1, 1/300, 2.54/300, 25.4/300, 72/300, 100/600]
            for index, scale in enumerate(expected):
                preferences('单位与标尺…', rulerUnitPreference=index)
                actual = state()
                assert actual['unit'] == index and abs(actual['unitX']-scale) < 1e-10, actual
                if index == 5:
                    assert abs(actual['unitY']-.25) < 1e-10
            preferences('单位与标尺…', rulerUnitPreference=0, accept=False)
            assert state()['unit'] == 5
        test('all_ruler_units_use_document_ppi_and_axis_extent_cancel_preserves', units)

        def grid():
            before = state(grid=True)['digest']
            preferences('参考线与网格…', gridSpacingPreference=64, gridSubdivisionsPreference=4,
                        gridStylePreference=2, guideStylePreference=1,
                        capture=str(OUT/'grid-preferences.png'))
            after = state(grid=True)
            assert after['gridSpacing'] == 64 and after['divisions'] == 4
            assert before != after['digest']
            preferences('参考线与网格…', gridSpacingPreference=1, gridSubdivisionsPreference=100)
            assert max(call('window_navigation_frames', zooms=[.1, 1, 32])['frameMs']) < 500
            preferences('参考线与网格…', gridSpacingPreference=64, gridSubdivisionsPreference=4)
        test('grid_spacing_subdivision_style_repaint_bounded_density', grid)

        def transparency():
            call('window_view', zoom=1)
            state(grid=False)
            preferences('透明度与画布…', checkerSizePreference=1,
                        steps=[dict(click='checkerLightPreference', color='#123456'),
                               dict(click='checkerDarkPreference', color='#654321'),
                               dict(click='canvasColorPreference', color='#345612')])
            actual = state(capture=str(OUT/'custom-transparency.png'))
            assert actual['checkerSize'] == 4
            for color in ['#123456', '#654321', '#345612']:
                assert actual['colors'][color] > 100, actual
            preferences('透明度与画布…', checkerSizePreference=0)
            actual = state()
            assert actual['colors']['#654321'] == 0 and actual['colors']['#123456'] > 100
            before = actual['digest']
            preferences('透明度与画布…', accept=False,
                        steps=[dict(click='canvasColorPreference', color='#ffffff')])
            assert state()['digest'] == before
        test('actual_checker_and_canvas_colors_render_and_cancel_is_atomic', transparency)

        def cursors():
            call('window_chrome', click=7)
            call('window_mouse', phase='move', space='widget', x=250, y=220)
            preferences('光标…', paintingCursorPreference=3, cursorCrosshairPreference=False)
            full = state()['digest']
            preferences('光标…', paintingCursorPreference=2)
            normal = state()['digest']
            assert full != normal
            preferences('光标…', cursorCrosshairPreference=True)
            assert state()['digest'] != normal
            call('window_key', key='CapsLock')
            precise = state()['digest']
            call('window_key', key='CapsLock')
            assert state()['digest'] != precise
        test('paint_tip_size_crosshair_caps_lock_change_actual_cursor_render', cursors)

        def tools():
            timestamp=1000
            def key(value, delay=0):
                nonlocal timestamp
                timestamp+=10
                call('window_key', key=value, timestamp=timestamp)
                if delay:
                    call('pump', waitMs=delay)
                timestamp+=max(10,delay)
                call('window_key', key=value, release=True, timestamp=timestamp)
            preferences(shiftToolSwitchPreference=True, springToolsPreference=True,
                        springDelayPreference=200)
            key('V'); key('M')
            assert state()['tool'] == 2
            key('M'); assert state()['tool'] == 2
            key('Shift+M'); assert state()['tool'] == 3
            key('Shift+M'); assert state()['tool'] == 2
            key('J'); assert state()['tool'] == 18
            key('Shift+J'); assert state()['tool'] == 17
            key('Shift+J'); assert state()['tool'] == 27
            key('Shift+J'); assert state()['tool'] == 18
            key('O'); key('Shift+O'); assert state()['tool'] == 22
            key('Shift+O'); assert state()['tool'] == 23
            key('V'); key('B', 300); assert state()['tool'] == 0
            key('B'); assert state()['tool'] == 7
            key('V')
            call('window_key',key='B')
            call('pump',waitMs=300)
            call('window_focus_loss')
            assert state()['tool']==0
            call('window_key',key='B',release=True)
            assert state()['tool']==0
            call('window_scene',width=600,height=400,layers=[dict(name='spring stroke',color='white')])
            revision_before=state()['revision']
            call('window_key',key='B',timestamp=100000)
            call('window_mouse',phase='press',x=100,y=100)
            call('window_key',key='B',release=True,timestamp=100400)
            assert state()['tool']==7
            call('window_mouse',phase='move',x=150,y=100)
            call('window_mouse',phase='release',x=150,y=100)
            assert state()['tool']==0 and state()['revision']!=revision_before
            call('window_action',text='撤销')
            assert state()['revision']==revision_before
            preferences(springToolsPreference=False, shiftToolSwitchPreference=False)
            key('M'); assert state()['tool'] == 2
            key('M'); assert state()['tool'] == 3
            key('V'); key('B', 300); assert state()['tool'] == 7
            preferences(springToolsPreference=True, springDelayPreference=500)
            key('V'); key('B', 250); assert state()['tool'] == 7
            preferences(springDelayPreference=100, accept=False)
            assert state()['springDelay'] == 500
            preferences(showTooltipsPreference=True)
            tooltip=call('window_tooltip')
            assert tooltip['visible'],tooltip
            preferences(showTooltipsPreference=False)
            call('pump', waitMs=400)
            assert not call('window_tooltip')['visible']
        test('tool_groups_shift_toggle_spring_hold_tap_and_timing_preferences', tools)

        def persistence():
            preferences('光标…', paintingCursorPreference=3)
            before = state()
            child = subprocess.run([sys.executable, __file__, '--probe', folder],
                                   capture_output=True, text=True, timeout=45)
            assert child.returncode == 0, child.stderr
            reopened = json.loads(child.stdout.strip().splitlines()[-1])
            for key in ['unit', 'paintingCursor', 'gridSpacing', 'divisions', 'checkerSize']:
                assert reopened[key] == before[key], (key, reopened, before)
            preferences(restoreDefaults=True, accept=False)
            assert state()['gridSpacing'] == 64
            preferences(restoreDefaults=True)
            defaults = state()
            assert defaults['gridSpacing'] == 100 and defaults['unit'] == 0
            assert defaults['paintingCursor'] == 2 and defaults['checkerSize'] == 8
            assert defaults['revision'] == revision
        test('fresh_process_persistence_reset_commit_cancel_no_document_mutation', persistence)
        def history():
            call('window_scene', width=32, height=32, layers=[dict(name='base', color='gray')])
            preferences('历史记录…', historyStatesPreference=5)
            for value in range(1, 11):
                call('window_control', name='layerExposure', value=value/10)
            assert state()['historyCount'] == 5
            current = state()['revision']
            preferences('历史记录…', historyStatesPreference=2, accept=False)
            assert state()['historyCount'] == 5
            preferences('历史记录…', historyStatesPreference=3)
            assert state()['historyCount'] == 3 and state()['revision'] == current
            call('window_action', text='撤销');call('window_action', text='撤销')
            earliest = state()['revision']
            call('window_action', text='撤销');assert state()['revision'] == earliest
            call('window_action', text='重做');call('window_action', text='重做')
            assert state()['revision'] == current
            call('window_action', text='撤销')
            call('window_control', name='layerExposure', value=.3)
            branch = state()['revision']
            call('window_action', text='重做');assert state()['revision'] == branch
        test('history_limit_trims_old_states_preserves_current_cancel_undo_redo_branch', history)
        def recovery():
            call('window_scene', width=32, height=32, layers=[dict(name='recovery',color='gray')])
            call('window_control',name='layerExposure',value=.5)
            revision=call('window_recovery_state')['revision']
            preferences('文件处理…',autoRecoveryPreference=False,recoveryMinutesPreference=7)
            s=call('window_recovery_state',trigger=True)
            assert not s['enabled'] and not s['busy'] and not s['exists']
            assert s['interval']==420000
            preferences('文件处理…',autoRecoveryPreference=True,recoveryMinutesPreference=3,accept=False)
            assert not call('window_recovery_state')['enabled']
            preferences('文件处理…',autoRecoveryPreference=True,recoveryMinutesPreference=3)
            started=call('window_recovery_state',trigger=True)
            assert started['busy'] and started['enabled'] and started['interval']==180000
            assert started['triggerMs']<100
            call('window_control',name='layerExposure',value=1)
            def wait_recovery():
                end=time.monotonic()+10
                while time.monotonic()<end:
                    call('pump',waitMs=30)
                    s=call('window_recovery_state')
                    if not s['busy']:return s
                raise AssertionError(s)
            s=wait_recovery()
            assert s['dirty'] and s['recoveredRevision']==revision
            assert call('window_recovery_state',inspect=True)['savedExposure']==.5
            call('window_recovery_state',trigger=True);wait_recovery()
            assert call('window_recovery_state',inspect=True)['savedExposure']==1
            assert not call('window_recovery_state',trigger=True)['busy']
            good_path=s['path']
            bad=Path(folder)/'recovery-is-directory';bad.mkdir()
            call('window_control',name='layerExposure',value=1.5)
            call('window_recovery_state',path=str(bad),trigger=True)
            failed=wait_recovery()
            assert '自动恢复写入失败' in failed['status']
            assert failed['dirty']
            call('window_recovery_state',path=good_path,trigger=True);wait_recovery()
            assert call('window_recovery_state',inspect=True)['savedExposure']==1.5
            recent=[str(Path(folder)/f'{i}.huelith') for i in range(6)]
            call('window_recovery_state',recent=recent)
            preferences('文件处理…',recentFilesPreference=3)
            assert call('window_recovery_state')['recent']==recent[:3]
            preferences('文件处理…',recentFilesPreference=0,accept=False)
            assert call('window_recovery_state')['recent']==recent[:3]
            preferences('文件处理…',recentFilesPreference=0)
            assert call('window_recovery_state')['recent']==[]
        test('file_preferences_background_recovery_snapshot_skip_failure_retry_recent_limit',recovery)
        call('shutdown')
    (OUT/'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
    assert all(r['passed'] for r in results), results


if __name__ == '__main__':
    main()
