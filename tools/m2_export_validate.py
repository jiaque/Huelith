"""Exercise the production C++ export worker and modeless Qt queue through Python."""
import ctypes
import json
import os
from pathlib import Path
import tempfile
import time
import traceback
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def main():
    qt = Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    handles = [os.add_dll_directory(str(qt / 'bin')),
               os.add_dll_directory(str(ROOT / 'build/windows-release'))]
    os.environ['QT_PLUGIN_PATH'] = str(qt / 'plugins')
    os.environ['QT_QPA_PLATFORM'] = 'windows'
    lib = ctypes.CDLL(str(ROOT / 'build/windows-release/huelith_python_driver.dll'))
    command = lib.huelith_python_command
    command.argtypes = [ctypes.c_char_p]
    command.restype = ctypes.c_char_p
    results, transcript = [], []
    output = ROOT / 'docs/test-evidence/M2/export'
    output.mkdir(parents=True, exist_ok=True)

    def call(op, **values):
        request = dict(op=op, **values)
        result = json.loads(command(json.dumps(request, ensure_ascii=False).encode()))
        transcript.append(dict(request=request, result=result))
        if 'error' in result:
            raise RuntimeError(result['error'])
        return result

    def wait(window=False):
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            items = call('export_queue', window=window)['items']
            if all(item['status'] >= 3 for item in items):
                return items
            call('pump', waitMs=30)
        raise AssertionError(items)

    def test(name, body):
        try:
            body()
            results.append(dict(name=name, passed=True))
        except Exception:
            results.append(dict(name=name, passed=False, error=traceback.format_exc()))
        print(json.dumps(results[-1], ensure_ascii=False), flush=True)

    with tempfile.TemporaryDirectory(prefix='huelith-export-') as folder:
        folder = Path(folder)
        call('init', settings=str(folder))
        call('window')

        def scene(color='red'):
            call('window_scene', width=120, height=80,
                 layers=[dict(name='pixels', color=color)])

        def snapshot():
            scene()
            revision = call('window_interaction_state')['revision']
            target = folder / '快照.png'
            call('export_queue', reset=True, path=str(target), width=60, height=40)
            scene('blue')
            call('export_queue', start=True)
            item = wait()[0]
            assert item['status'] == 3 and item['revision'] == revision, item
            with Image.open(target) as image:
                assert image.size == (60, 40) and image.convert('RGB').getpixel((10, 10)) == (255, 0, 0)
                assert image.info.get('icc_profile')

        test('immutable_snapshot_size_icc_while_document_changes', snapshot)

        def conflicts():
            scene()
            target = folder / 'existing.png'
            target.write_bytes(b'original sentinel')
            call('export_queue', reset=True, path=str(target), conflict=0)
            call('export_queue', path=str(target), conflict=1)
            call('export_queue', start=True)
            items = wait()
            assert [i['status'] for i in items] == [3, 6], items
            assert target.read_bytes() == b'original sentinel'
            assert Path(items[0]['path']).name == 'existing_1.png'
            call('export_queue', path=str(target), conflict=2)
            assert wait()[-1]['status'] == 3
            with Image.open(target) as image:
                assert image.convert('RGB').getpixel((10, 10)) == (255, 0, 0)

        test('number_skip_explicit_atomic_overwrite', conflicts)

        def protected():
            scene()
            target = folder / 'camera-original.png'
            target.write_bytes(b'raw-source-sentinel')
            call('export_queue', reset=True, path=str(target), conflict=2, protected=str(target))
            call('export_queue', start=True)
            assert wait()[0]['status'] == 4 and target.read_bytes() == b'raw-source-sentinel'

        test('overwrite_never_replaces_original_source', protected)

        def retry():
            scene()
            missing = folder / 'missing' / 'retry.png'
            success = folder / 'other.png'
            call('export_queue', reset=True, path=str(missing))
            call('export_queue', path=str(success))
            call('export_queue', start=True)
            assert [i['status'] for i in wait()] == [4, 3]
            before = success.read_bytes()
            missing.parent.mkdir()
            scene('blue')
            call('export_queue', retry=True)
            assert [i['status'] for i in wait()] == [3, 3]
            assert success.read_bytes() == before and not (folder / 'other_1.png').exists()
            with Image.open(missing) as image:
                assert image.convert('RGB').getpixel((10, 10)) == (255, 0, 0)

        test('failure_isolation_retry_failed_only_original_revision', retry)

        def cancellation():
            scene()
            cancelled = folder / 'cancelled.png'
            item = call('export_queue', reset=True, path=str(cancelled))
            call('export_queue', cancel=item['id'])
            call('export_queue', start=True)
            assert wait()[0]['status'] == 5 and not cancelled.exists()
            # A large production render allows cancelling an in-flight job, before file commit.
            call('window_scene', width=2600, height=1800, layers=[dict(name='large', color='red')])
            active = folder / 'active.tif'
            call('export_queue', reset=True, path=str(active), format='tif', bits=16)
            call('export_queue', start=True)
            call('export_queue', cancelAll=True)
            assert wait()[0]['status'] == 5 and not active.exists()
            assert not list(folder.glob('.huelith-export-*'))

        test('queued_active_cancel_no_partial_file', cancellation)

        def formats():
            scene()
            call('export_queue', reset=True)
            for fmt, bits in [('png', 16), ('tif', 16), ('jpg', 8)]:
                call('export_queue', path=str(folder / f'format-{bits}.{fmt}'), format=fmt, bits=bits)
            call('export_queue', start=True)
            items = wait()
            assert all(i['status'] == 3 for i in items), items
            for fmt, bits in [('png', 16), ('tif', 16), ('jpg', 8)]:
                with Image.open(folder / f'format-{bits}.{fmt}') as image:
                    assert image.size == (120, 80) and image.info.get('icc_profile')

        test('png16_tiff16_jpeg_encoded_files_with_icc', formats)

        def gui():
            scene()
            out = folder / '界面导出'
            out.mkdir()
            call('window_action', text='批量导出与队列…')
            call('export_queue', window=True, exportQueueDirectory=str(out),
                 exportQueueSizes='原图, 60', exportQueueNaming='{name}_{size}_{index}',
                 click='exportQueueStart')
            items = wait(window=True)
            assert len(items) == 2 and all(i['status'] == 3 for i in items), items
            assert sorted(Image.open(i['path']).size for i in items) == [(60, 40), (120, 80)]
            call('pump', waitMs=100)
            call('export_queue', window=True, capture=str(output / 'export-queue-window.png'))
            call('export_queue', window=True, click='exportQueueHide')

        test('real_menu_modeless_queue_multisize_output', gui)
        call('shutdown')

    report = dict(passed=sum(t['passed'] for t in results), total=len(results), tests=results)
    (output / 'results.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    (output / 'commands.json').write_text(json.dumps(transcript, ensure_ascii=False, indent=2), encoding='utf-8')
    return int(report['passed'] != report['total'])


if __name__ == '__main__':
    raise SystemExit(main())
