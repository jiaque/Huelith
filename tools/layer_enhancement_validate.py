"""Selected-layer AI actions via the established Python production-window harness."""
from pathlib import Path
exec(Path('tools/m3_geometry_validate.py').read_text(encoding='utf-8').split('with tempfile.TemporaryDirectory() as tmp:')[0])
O=R/'docs/test-evidence/M3/layer-enhancement';O.mkdir(parents=True,exist_ok=True)
pixels=np.zeros((40,48,4),dtype=np.uint8)
for y in range(5,35):
 for x in range(5,43):pixels[y,x]=[x*5,y*6,(x+y)*2,180 if x<10 else 255]
source=O/'transparent-source.png';Image.fromarray(pixels).save(source)
records=[]
with tempfile.TemporaryDirectory() as tmp:
 checked(op='init',settings=tmp);checked(op='window');checked(op='window_open',path=str(source))
 for mode,scale in [(0,2),(0,4),(1,2),(2,2)]:
  r=checked(op='enhancement_ui',layer=True,fixture=str(source),mode=mode,scale=scale,warp=mode==1,
            undoCheck=True,save=str(O/f'layer-{mode}-{scale}.huelith'),capture=str(O/f'dialog-{mode}-{scale}.png'))
  print(mode,scale,r,flush=True);d=r['layerResult']
  assert r['previewed'] and r['documentsBefore']==r['documentsAfter'] and (r['width'],r['height'])==(384,256),r
  assert d['revisionDelta']==1 and d['originalPixelsPreserved'] and d['roundtrip'] and d['undoRestores'] and d['redoRestores'],d
  if mode<2:
   assert not d['originalVisible'] and d['selectedChanged'] and d['boundsError']<1e-6,d
   assert d['maskPreserved'] and d['opacityPreserved'] and d['warpPreserved'] and d['alphaCorner']==0,d
   assert (d['pixelWidth'],d['pixelHeight'])==((48*scale,40*scale) if mode==0 else (48,40)),d
   assert abs(d['blurRadius']-.3*(scale if mode==0 else 1))<1e-6,d
  else:assert r['hasSelection'] and d['outsideSelection']==0 and d['selectionMaximum']>0,d
  records.append(r)
 r=checked(op='enhancement_ui',layer=True,fixture=str(source),cancel=True)
 assert r['previewed'] and r['layerResult']['revisionDelta']==0 and r['layerResult']['originalVisible'] and not r['layerResult']['selectedChanged'],r
 records.append(r)
 r=checked(op='enhancement_ui',layer=True,fixture=str(source),changeSource=True)
 assert r['previewed'] and r['documentsAfter']==r['documentsBefore']+1 and r['layerResult']['revisionDelta']==1 and r['layerResult']['selectedChanged'],r
 records.append(r)
 # The image menu still processes the whole visible document and opens a new document.
 r=checked(op='enhancement_ui',mode=0,scale=2)
 assert r['previewed'] and r['documentsAfter']==r['documentsBefore']+1 and (r['width'],r['height'])==(768,512),r
 records.append(r)
 for reason in ['locked','unsupported','multi']:
  r=checked(op='enhancement_ui',layer=True,fixture=str(source),**{reason:True})
  assert not r['previewed'] and r['layerResult']['revisionDelta']==0 and r['status'],r
  records.append(r)
 checked(op='shutdown')
(O/'results.json').write_text(json.dumps(dict(passed=True,records=records),ensure_ascii=False,indent=2),encoding='utf-8')
print('Layer SR2/SR4/denoise/depth, transforms, transparency, undo, persistence and image-menu regression PASS',flush=True)
