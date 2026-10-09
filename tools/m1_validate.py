"""M1 regression scenarios; Python assertions call the actual C++ editor DLL.
Run with the same Conda codex Python used for M0. No test EXE is launched.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import tempfile
import traceback
import time
import zipfile
from PIL import Image, ImageGrab

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', default='windows-release')
    parser.add_argument('--output', type=Path, default=ROOT/'docs/test-evidence/M1')
    parser.add_argument('--native', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    qt = Path(r'D:\Program Files\Qt\6.10.3\msvc2022_64')
    handles = [os.add_dll_directory(str(qt/'bin')), os.add_dll_directory(str(ROOT/'build'/args.build))]
    os.environ['QT_PLUGIN_PATH'] = str(qt/'plugins')
    os.environ['QT_QPA_PLATFORM'] = 'windows' if args.native else 'offscreen'
    library = ctypes.CDLL(str(ROOT/'build'/args.build/'huelith_python_driver.dll'))
    command = library.huelith_python_command
    command.argtypes = [ctypes.c_char_p]
    command.restype = ctypes.c_char_p
    transcript = []
    def call(op, **values):
        request = dict(op=op, **values)
        result = json.loads(command(json.dumps(request, ensure_ascii=False).encode('utf-8')))
        transcript.append(dict(request=request, result=result))
        if 'error' in result:
            raise RuntimeError(result['error'])
        return result
    def rejected(op, **values):
        try:
            call(op, **values)
        except RuntimeError:
            return
        raise AssertionError(f'{op} should have been rejected')
    results = []
    with tempfile.TemporaryDirectory(prefix='huelith-m1-') as temp:
        folder=Path(temp)
        assert call('init', settings=str(folder/'settings'))['protocol']==2, 'Rebuild the Python bridge before testing'
        def test(name, function):
            try:
                function()
                results.append(dict(name=name, passed=True))
            except Exception:
                results.append(dict(name=name, passed=False, error=traceback.format_exc()))
        def history():
            call('create');call('snapshot')
            call('add', color='red')
            call('save', path=str(folder/'history.huelith'), snapshot=True)
            assert call('state')['dirty']
            call('undo');assert not call('state')['dirty']
            call('redo');assert call('state')['dirty']
            call('undo');call('add', color='blue');assert not call('state')['canRedo']
        test('revision_snapshot_branch', history)
        def cycles():
            call('create');group=call('add',kind=5)['id'];child=call('add',parent=group,color='red')['id']
            old=call('state');rejected('move',id=group,parent=child)
            assert call('state')==old
            call('set',id=child,locked=True);rejected('remove',id=child)
            call('set',id=child,locked=False);call('move',id=child,position=0)
            assert len(call('state')['layers'])==2
        test('tree_cycle_lock_move',cycles)
        def masks():
            call('create');layer=call('add',color='white')['id'];call('range',x=0,y=0,w=8,h=8)
            call('mask',id=layer);call('range',x=0,y=0,w=8,h=8,mode=2)
            assert call('state')['selection'] and call('state')['selectionPixel']==0
            call('deselect');assert call('pixel',x=2,y=2)['rgba'][3]==255
            assert call('pixel',x=12,y=12)['rgba'][3]==0
        test('active_empty_selection_independent_mask',masks)
        def clipping():
            call('create');base=call('add',color='red')['id'];top=call('add',color='blue')['id']
            call('set',id=top,clipped=True);call('set',id=base,visible=False)
            assert call('pixel')['rgba'][3]==0
        test('hidden_clipping_base',clipping)
        def isolation():
            call('create');call('add',color='red');group=call('add',kind=5)['id'];adjustment=call('add',kind=3,parent=group)['id']
            call('set',id=adjustment,exposure=-1);call('set',id=group,isolated=True)
            before=call('pixel')['linear'];call('set',id=group,isolated=False);after=call('pixel')['linear']
            assert abs(before[0]*.5-after[0])<1e-6
        test('group_adjustment_scope',isolation)
        def storage():
            call('create',name='中文 工程');layer=call('add',color='red')['id'];path=folder/'中文工程.huelith'
            call('save',path=str(path));old=path.read_bytes();before=call('pixel')
            with zipfile.ZipFile(path) as archive:
                assert archive.testzip() is None
                assert set(archive.namelist())=={'manifest.json','document.bin'}
            call('set',id=layer,name='changed');rejected('save',path=str(path),fail=True)
            assert path.read_bytes()==old
            call('load',path=str(path));assert call('pixel')==before
            bad=folder/'broken.huelith';bad.write_bytes(old[:50]);rejected('load',path=str(bad))
            assert call('pixel')==before
        test('roundtrip_atomic_failure_corruption',storage)
        def exports():
            call('create');call('add',color='#80ff0000');png=folder/'透明.png';jpeg=folder/'输出.jpg'
            call('export',path=str(png));call('export',path=str(jpeg))
            assert abs(Image.open(png).getpixel((0,0))[3]-128)<=1
            assert Image.open(jpeg).mode=='RGB'
            assert Image.open(png).info.get('icc_profile')
            assert call('state')['dirty']
        test('png_alpha_jpeg_matte_icc_export_dirty',exports)
        def fill_color():
            call('create');call('add',kind=4,fill='#808080')
            assert all(abs(v-128)<=1 for v in call('pixel')['rgba'][:3])
        test('fill_converts_srgb_to_linear_working_space',fill_color)
        def blend():
            call('create');call('add',color='#808080');top=call('add',color='#808080')['id']
            call('set',id=top,blend=2)
            assert all(abs(v-64)<=2 for v in call('pixel')['rgba'][:3])
            call('set',id=top,blend=6)
            assert all(abs(v-192)<=2 for v in call('pixel')['rgba'][:3])
            for mode in range(24):
                call('set',id=top,blend=mode)
                pixel=call('pixel')['linear']
                assert len(pixel)==4 and all(__import__('math').isfinite(v) for v in pixel)
                assert abs(pixel[3]-1)<1e-6
        test('artistic_blend_uses_encoded_srgb_all_24_finite',blend)
        def transforms():
            call('create');layer=call('add',color='white')['id'];call('range',x=0,y=0,w=4,h=4);call('mask',id=layer)
            call('transform',id=layer,x=6,y=0)
            assert call('pixel',x=1,y=1)['rgba'][3]==0
            assert call('pixel',x=7,y=1)['rgba'][3]==255
            call('transform',id=layer,x=3,y=0,mask=True)
            assert call('pixel',x=7,y=1)['rgba'][3]==0
            assert call('pixel',x=10,y=1)['rgba'][3]==255
        test('linked_and_independent_mask_transforms',transforms)
        def nested_transform():
            call('create');group=call('add',kind=6)['id'];call('add',parent=group,color='red');call('transform',id=group,x=8,y=0)
            assert call('pixel',x=2,y=2)['rgba'][3]==0
            assert call('pixel',x=10,y=2)['rgba'][3]==255
        test('nested_composition_transform',nested_transform)
        def soft_range():
            call('create');layer=call('add',color='white')['id'];call('range',x=4,y=4,w=8,h=8);call('feather',radius=2);call('mask',id=layer)
            assert 0<call('pixel',x=4,y=4)['rgba'][3]<255
            call('persist_ranges');state=call('state');path=folder/'ranges.huelith';call('save',path=str(path));call('load',path=str(path));after=call('state')
            assert (after['paths'],after['ranges'],after['snapshots'])==(1,1,1)
        test('soft_coverage_paths_snapshots_roundtrip',soft_range)
        def invalid_parameters():
            call('create');layer=call('add',color='red')['id'];before=call('state')
            rejected('set',id=layer,opacity=2);rejected('set',id=layer,exposure=100);rejected('transform',id=layer,scale=0)
            assert call('state')==before
            rejected('create',width=100000,height=100000)
            assert call('state')==before
        test('invalid_edits_leave_document_intact',invalid_parameters)
        def gui():
            call('window');call('window_new',width=400,height=300)
            call('window_action',text='像素');assert call('window_state')['layers']==1
            assert call('window_state')['treeSelected']==1
            call('window_draw',tool=2);assert call('window_state')['selected']
            revision=call('window_state')['revision'];call('window_draw',tool=7)
            assert call('window_state')['revision']==revision+1
            assert call('window_pixel',x=50,y=45)['alpha']>0
            assert call('window_pixel',x=200,y=150)['alpha']==0
            call('window_action',text='撤销');assert call('window_pixel',x=50,y=45)['alpha']==0
            call('window_action',text='蒙版');call('window_action',text='取消选区');assert not call('window_state')['selected']
            call('window_action',text='撤销');assert call('window_state')['selected']
            image=folder/'source.png';Image.new('RGB',(128,96),(190,100,40)).save(image)
            original=hashlib.sha256(image.read_bytes()).hexdigest()
            call('window_open',path=str(image));assert call('window_state')['count']==2
            call('window_open',path=str(image),place=True);state=call('window_state');assert state['layers']==2 and state['treeRows']==2
            assert hashlib.sha256(image.read_bytes()).hexdigest()==original
            before=call('window_state')['revision'];call('window_slider');after=call('window_state')['revision'];assert after==before+1
            call('window_action',text='撤销');assert call('window_state')['revision']==before
            call('window_tab',index=0);assert call('window_state')['layers']==1
            call('window_tab',index=1);assert call('window_state')['layers']==2
            saved=folder/'async-ui.huelith';call('window_filename',path=str(saved));call('window_action',text='保存工程')
            call('window_action',text='像素')
            deadline=time.monotonic()+5
            while not saved.exists() and time.monotonic()<deadline:
                call('pump')
            assert saved.exists()
            call('pump');assert call('window_state')['dirty']
            call('load',path=str(saved));assert len(call('state')['layers'])==2
            assert call('window_capture',path=str(args.output/f'{args.build}-window.png'))['ok']
        test('real_qt_window_documents_selection_layers',gui)
        def nested_gui():
            call('window_action',text='嵌套合成')
            initial=call('window_state')['revision']
            call('window_action',text='编辑嵌套合成…',answer='add')
            assert call('window_state')['children']==1
            assert call('window_state')['revision']==initial+1
            call('window_action',text='编辑嵌套合成…',answer='cancel')
            assert call('window_state')['children']==1
            assert call('window_state')['revision']==initial+1
            call('window_action',text='撤销');assert call('window_state')['children']==0
        test('nested_window_commit_cancel_single_undo',nested_gui)
        def multi_selection():
            before=call('window_state')['layers'];call('window_select',index=0);call('window_select',index=1,control=True)
            assert call('window_state')['treeSelected']==2
            call('window_action',text='删除');assert call('window_state')['layers']==before-2
            call('window_action',text='撤销');assert call('window_state')['layers']==before
        test('actual_tree_ctrl_multiselect_delete_undo',multi_selection)
        call('shutdown')
    report=dict(build=args.build, python=os.sys.executable, tests=results, passed=sum(r['passed'] for r in results), total=len(results), method='Python ctypes invoking production C++ libraries; no test EXE')
    (args.output/f'{args.build}-results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    (args.output/f'{args.build}-commands.json').write_text(json.dumps(transcript,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))
    return 0 if all(r['passed'] for r in results) else 1

if __name__=='__main__':
    raise SystemExit(main())
