"""Check the real Qt language selection, persistence and translated controls offscreen."""
from pathlib import Path
import ast
import ctypes
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / 'apps/desktop/assets/translations/en.json'


def child(settings, phase):
    qt = Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles = [os.add_dll_directory(str(qt / 'bin')),
               os.add_dll_directory(str(ROOT / 'build/windows-release'))]
    os.environ['QT_QPA_PLATFORM'] = 'offscreen'
    os.environ['QT_PLUGIN_PATH'] = str(qt / 'plugins')
    dll = ctypes.CDLL(str(ROOT / 'build/windows-release/huelith_python_driver.dll'))
    command = dll.huelith_python_command
    command.argtypes = [ctypes.c_char_p]
    command.restype = ctypes.c_char_p

    def call(op, **values):
        result = json.loads(command(json.dumps(dict(op=op, **values)).encode()))
        assert 'error' not in result, result
        return result

    init = dict(settings=settings, loadLanguage=True)
    if phase == 'english':
        init['language'] = 'en'
    call('init', **init)
    call('window')
    result = call('language_probe')
    english = phase in ('english', 'restart_en')
    assert result['language'] == ('en' if english else 'zh_CN'), result
    assert result['file'] == ('Preferences' if english else '首选项'), result
    assert result['errorText'] == ('The layer does not exist' if english else '图层不存在'), result
    assert len(result['labels']) > 100
    if english:
        assert not any(re.search('[\u4e00-\u9fff]', s) for s in result['menus'] + result['labels']), result
        assert 'Eye Distance' in result['labels'] and 'Nose Tip Width' in result['labels']
        call('window_scene', width=64, height=50,
             layers=[dict(name='原始照片', color='red'), dict(name='Retouch 修复', color='blue')])
        copy = call('photoshop_roundtrip', path=str(Path(settings) / 'localized.psd'))
        assert copy['names'] == ['原始照片', 'Retouch 修复'] and copy['samePixels'], copy
    call('window_capture', path=str(ROOT / 'build' / f'language-{phase}.png'))
    if phase == 'english':
        cancelled = call('language_probe', choose=0, accept=False)
        assert cancelled['found'] and not cancelled['accepted'] and cancelled['language'] == 'en'
        saved = call('language_probe', choose=0, accept=True,
                     capture=str(ROOT / 'build/language-preferences-en.png'))
        assert saved['found'] and saved['accepted'] and saved['language'] == 'zh_CN'
        assert saved['file'] == 'Preferences', 'Language changes must wait for restart'
    elif phase == 'restart_zh':
        saved = call('language_probe', choose=1, accept=True,
                     capture=str(ROOT / 'build/language-preferences-zh.png'))
        assert saved['accepted'] and saved['language'] == 'en'
        assert saved['file'] == '首选项'
    call('shutdown')
    print(phase, 'passed')


def main():
    catalog = json.loads(CATALOG.read_text(encoding='utf-8'))
    for source, target in catalog.items():
        assert target, source
        assert sorted(re.findall(r'%\d+|%n', source)) == sorted(re.findall(r'%\d+|%n', target)), source
        assert not re.search('[\u4e00-\u9fff]', target) or source == '简体中文', source
    for folder in ('apps/desktop', 'src', 'include'):
        for source_file in (ROOT / folder).rglob('*'):
            if source_file.suffix not in ('.cpp', '.h'):
                continue
            # Adjacent C++ string literals form one translation key.
            for match in re.finditer(
                    r'(?:\btr|\btranslate)\(\s*(?:"Huelith",\s*)?((?:"(?:\\.|[^"\\])*"\s*)+)',
                    source_file.read_text(encoding='utf-8')):
                text = ''.join(ast.literal_eval(part) for part in
                               re.findall(r'"(?:\\.|[^"\\])*"', match[1]))
                if re.search('[\u4e00-\u9fff]', text):
                    assert text in catalog, (str(source_file), text)
    with tempfile.TemporaryDirectory(dir=ROOT / 'build', ignore_cleanup_errors=True) as settings:
        for phase in ('english', 'restart_zh', 'restart_en'):
            subprocess.run([sys.executable, __file__, settings, phase], check=True, timeout=90)
    print('Catalog placeholders and English/Chinese preference persistence passed')


if __name__ == '__main__':
    if len(sys.argv) == 3:
        child(*sys.argv[1:])
    else:
        main()
