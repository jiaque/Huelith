"""One bounded PS01–PS06 integration run against the production Qt DLL, offscreen."""
import ctypes, json, os, tempfile, traceback
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'docs/test-evidence/PS01-06'
OUT.mkdir(parents=True, exist_ok=True)
qt = Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
handles = [os.add_dll_directory(str(qt / 'bin')), os.add_dll_directory(str(ROOT / 'build/windows-release'))]
os.environ['QT_PLUGIN_PATH'] = str(qt / 'plugins')
os.environ['QT_QPA_PLATFORM'] = 'offscreen'
lib = ctypes.CDLL(str(ROOT / 'build/windows-release/huelith_python_driver.dll'))
fn = lib.huelith_python_command
fn.argtypes = [ctypes.c_char_p]
fn.restype = ctypes.c_char_p
results = []

def call(op, **args):
    value = json.loads(fn(json.dumps(dict(op=op, **args), ensure_ascii=False).encode()))
    if 'error' in value:
        raise RuntimeError(value)
    return value

def action(text, **args):
    return call('window_action', text=text, **args)

def control(name, value=None, **args):
    return call('window_control', name=name, value=value, **args)

def test(name, body):
    try:
        body()
        result = dict(name=name, passed=True)
    except Exception:
        result = dict(name=name, passed=False, error=traceback.format_exc())
    results.append(result)
    print(json.dumps(result, ensure_ascii=False), flush=True)

def scene():
    call('window_scene', width=400, height=300, layers=[dict(name='Red', color='#dd2222', width=100, height=80, x=20, y=20), dict(name='Blue', color='#2222dd', width=60, height=60, x=200, y=100)])

def probe():
    return call('window_probe')
with tempfile.TemporaryDirectory(prefix='huelith-ps01-06-', dir=ROOT / 'build', ignore_cleanup_errors=True) as folder:
    call('init', settings=folder)
    call('window')
    scene()

    def layout():
        compact = call('ps_state', width=1366, height=768, capture=str(OUT / 'main-1366.png'))
        large = call('ps_state', width=1920, height=1080, capture=str(OUT / 'main-1920.png'))
        assert compact['width'] == 1366 and compact['height'] == 768, compact
        assert large['canvasWidth'] > compact['canvasWidth'], (compact, large)
        before = call('window_workspace')['docks']
        control('workspacePanelToggle')
        hidden = call('window_workspace')['docks']
        assert not any((item['visible'] for item in hidden.values())), hidden
        control('workspacePanelToggle')
        assert call('window_workspace')['docks'] == before
        control('toolColumnToggle')
        assert call('ps_state')['columns'] == 1
        control('toolColumnToggle')
        assert call('ps_state')['columns'] == 2
    test('PS01 adaptive main canvas and panel/toolbar controls', layout)

    def documents():
        steps = [dict(name='newDocumentCategory', value=3), dict(name='newDocumentPresets', value=1)]
        a4 = call('ps_new_document', steps=steps, capture=str(OUT / 'new-document.png'))
        assert (a4['width'], a4['height']) == (2480, 3508), a4
        for unit in [0, 3, 2, 1, 0, 3, 1]:
            steps.append(dict(name='documentUnit', value=unit))
        roundtrip = call('ps_new_document', steps=steps)
        assert (roundtrip['width'], roundtrip['height']) == (2480, 3508), roundtrip
        created = call('ps_new_document', accept=True, steps=[dict(name='documentUnit', value=0), dict(name='documentWidth', value=32), dict(name='documentHeight', value=24), dict(name='documentBackground', value=1)])
        assert created['recent'] == 1 and call('window_pixel', x=5, y=5)['rgba'] == [255, 255, 255, 255]
        cancelled = call('ps_new_document', steps=[dict(name='documentWidth', value=100)])
        assert cancelled['recent'] == 1
    test('PS02 presets units background creation and cancel', documents)

    def preferences():
        scene()
        action('界面与工作区…', dialog=dict(accept=True, interfaceThemePreference=2))
        assert call('ps_state')['theme'] == 2
        action('界面与工作区…', dialog=dict(accept=False, interfaceThemePreference=1))
        assert call('ps_state')['theme'] == 2
        action('界面与工作区…', dialog=dict(accept=True, interfaceThemePreference=0, preferenceSearch='字体', capture=str(OUT / 'preferences-search.png')))
        path = str(Path(folder) / 'operations.jsonl')
        action('历史记录…', dialog=dict(accept=True, operationLogEnabled=True, operationLogPath=path, operationLogDetail=1))
        control('layerExposure', 0.5)
        action('撤销')
        rows = [json.loads(line) for line in Path(path).read_text(encoding='utf-8').splitlines()]
        assert len(rows) >= 2 and all(('document' in row and 'historyPosition' in row for row in rows)), rows
        action('历史记录…', dialog=dict(accept=True, operationLogEnabled=False))
    test('PS03 preferences transactions search theme and log output', preferences)

    def transform():
        scene()
        call('window_select', index=0)
        action('数值变换…', dialog=dict(accept=True, transformValue0=50, transformValue1=60, transformValue2=120))
        current = call('window_interaction_state')
        assert current['tx'] == 50 and current['ty'] == 60 and (current['sx'] == 2), current
        action('撤销')
        assert call('window_interaction_state')['sx'] == 1
        action('重做')
        assert call('window_interaction_state')['sx'] == 2
    test('PS04 numeric transform and history', transform)

    def layers():
        scene()
        call('window_select', index=0)
        action('红色')
        control('layerFlagsFilter', 6)
        assert len(call('ps_state')['visibleLayers']) == 1
        control('layerFlagsFilter', 0)
        control('layerNameFilter', 'Red')
        assert call('ps_state')['visibleLayers'] == ['Red']
        control('layerNameFilter', '')
        call('window_select', index=0)
        call('window_select', index=1, control=True)
        action('链接选中图层')
        before = probe()['layers']
        call('window_select', index=0)
        action('数值变换…', dialog=dict(accept=True, transformValue0=210, transformValue1=110))
        after = probe()['layers']
        assert all((a['tx'] != b['tx'] for a, b in zip(before, after))), (before, after)
        action('保存当前图层复合…', answer='Layout')
        saved = probe()['layers']
        action('数值变换…', dialog=dict(accept=True, transformValue0=230, transformValue1=130))
        action('应用图层复合…', answer='Layout')
        assert probe()['layers'] == saved
        path = str(Path(folder) / 'layers.huelith')
        call('window_filename', path=path)
        action('保存工程')
        metadata = call('ps_state')['metadata']
        for _ in range(100):
            if Path(path).exists():
                break
            call('pump', waitMs=20)
        call('window_open', path=path)
        assert call('ps_state')['metadata'] == metadata
    test('PS05 filters links layer comps and project persistence', layers)

    def channels():
        scene()
        action('全选')
        control('channelSave', answer='Alpha Full')
        assert 'Alpha Full' in call('ps_state')['alpha']
        action('取消选区')
        control('channelsList', 3)
        control('channelLoad')
        assert probe()['selected'] >= 0
        assert call('window_probe', x=5, y=5)['range'] == 255
        control('channelCalculate', dialog=dict(source1=3, source2=3, blendMode=0, channelOutput=1))
        assert len(call('ps_state')['alpha']) == 2
        action('撤销')
        assert len(call('ps_state')['alpha']) == 1
        control('channelsList', 0)
        control('channelLoad')
        assert call('window_probe', x=25, y=25)['range'] > 150
        action('焦点范围…', dialog=dict(accept=True))
        assert call('window_interaction_state')['rangeCount'] > 0
        call('ps_state', capture=str(OUT / 'channels.png'))
    test('PS06 alpha RGB calculation focus and undo', channels)
    call('shutdown')
(OUT / 'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
raise SystemExit(0 if all((row['passed'] for row in results)) else 1)
