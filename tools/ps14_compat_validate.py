"""Read independent PSD/PSB fixtures through production code; no Photoshop installation needed."""
import ctypes
import json
import os
from pathlib import Path
import struct
import tempfile
import zlib
from PIL import Image

root = Path(__file__).resolve().parents[1]
qt = Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
handles = [os.add_dll_directory(str(qt/'bin')), os.add_dll_directory(str(root/'build/windows-release'))]
os.environ['QT_QPA_PLATFORM'] = 'offscreen'
os.environ['QT_PLUGIN_PATH'] = str(qt/'plugins')
dll = ctypes.CDLL(str(root/'build/windows-release/huelith_python_driver.dll'))
fn = dll.huelith_python_command
fn.argtypes, fn.restype = [ctypes.c_char_p], ctypes.c_char_p
def call(op, **args):
    return json.loads(fn(json.dumps(dict(op=op, **args)).encode()))
def pack(fmt, *v): return struct.pack('>'+fmt, *v)
def fixture(depth, comp, version, layered):
    w, h = 8, 4
    channels = [[(x*17+y*9+c*45) % 256 for y in range(h) for x in range(w)] for c in range(3)]
    def plane(values):
        values = [v*(257 if depth==16 else 1) for v in values]
        if comp==3:
            values = [((v-values[i-1]) if i%w else v) % (1<<depth) for i,v in enumerate(values)]
        return b''.join(pack('H' if depth==16 else 'B', v) for v in values)
    planes = [plane(v) for v in channels]
    length = lambda n: pack('Q' if version==2 else 'I', n)
    info = b''
    if layered:
        payloads = [pack('H', comp)+zlib.compress(p) for p in planes]
        extra = pack('II',0,0)+b'\x05Layer\0\0'
        rec = pack('iiiiH',0,0,h,w,3)
        rec += b''.join(pack('h',i)+length(len(p)) for i,p in enumerate(payloads))
        rec += b'8BIMnorm'+bytes([255,0,0,0])+pack('I',len(extra))+extra
        layer = pack('h',1)+rec+b''.join(payloads)
        if len(layer)%2: layer += b'\0'
        info = length(len(layer))+layer+pack('I',0)
    return (b'8BPS'+pack('H',version)+b'\0'*6+pack('HIIHH',3,h,w,depth,3)
            +pack('II',0,0)+length(len(info))+info+pack('H',comp)+zlib.compress(b''.join(planes))), channels

with tempfile.TemporaryDirectory(dir=root/'build') as tmp:
    tmp = Path(tmp)
    assert 'error' not in call('init',settings=str(tmp))
    for depth in (8,16):
        for comp in (2,3):
            for version in (1,2):
                for layered in (False,True):
                    blob, expected = fixture(depth,comp,version,layered)
                    path=tmp/'input.psd';path.write_bytes(blob)
                    output=tmp/'decoded.png'
                    r=call('photoshop_read',path=str(path),output=str(output))
                    assert 'error' not in r,(depth,comp,version,layered,r)
                    pixels=list(Image.open(output).convert('RGB').getdata())
                    assert max(abs(p[c]-expected[c][i]) for i,p in enumerate(pixels) for c in range(3))<=1
    # A truncated ZIP is an error, never a silently black-filled image.
    path.write_bytes(blob[:-4])
    assert 'error' in call('photoshop_read',path=str(path),merged=True)
    for ext in ('psd','psb'):
        result=call('photoshop_group_probe',path=str(tmp/f'group.{ext}'))
        assert result.get('structure') and result['pixelError']<=1,result
    call('shutdown')
print('PSD/PSB ZIP and prediction 8/16 bit, truncated data, editable groups and masks passed')
