"""Reopen the real portrait project in the main window and export through its queue."""
from pathlib import Path
exec(Path('tools/m3_geometry_validate.py').read_text(encoding='utf-8').split('with tempfile.TemporaryDirectory() as tmp:')[0])
O=R/'docs/test-evidence/M3/export';O.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory() as tmp:
 checked(op='init',settings=tmp);checked(op='window')
 checked(op='window_open',path=str(R/'docs/test-evidence/M3/style/style.huelith'))
 for fmt,bits in [('png',8),('tif',16),('jpg',8)]:
  checked(op='export_queue',path=str(O/('portrait.'+fmt)),format=fmt,bits=bits,conflict=2)
 checked(op='export_queue',start=True)
 deadline=time.monotonic()+90
 while time.monotonic()<deadline:
  checked(op='pump',waitMs=50);r=checked(op='export_queue')
  if all(i['status'] in [3,4,5,6] for i in r['items']):break
 assert all(i['status']==3 for i in r['items']),r
 expected=Image.open(R/'docs/test-evidence/M3/style/combined.png').convert('RGB')
 for fmt in ['png','tif','jpg']:
  im=Image.open(O/('portrait.'+fmt));assert im.size==expected.size and im.info.get('icc_profile')
  if fmt=='tif':assert im.tag_v2[258][0]==16
 delta=int(abs(np.array(Image.open(O/'portrait.png').convert('RGB')).astype(int)-np.array(expected).astype(int)).max())
 assert delta<=1,delta
 (O/'results.json').write_text(json.dumps(dict(passed=True,pngDelta=delta,items=r['items']),indent=2))
 checked(op='shutdown');print('Portrait project -> main window -> PNG/TIFF16/JPEG export PASS',flush=True)
