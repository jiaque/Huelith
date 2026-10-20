"""Actual makeup/hair/light controls, independent masks and FP32 persistence."""
from pathlib import Path
exec(Path('tools/m3_geometry_validate.py').read_text(encoding='utf-8').split('with tempfile.TemporaryDirectory() as tmp:')[0])
O=R/'docs/test-evidence/M3/style';O.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory() as tmp:
 checked(op='init',settings=tmp)
 source=R/'docs/test-evidence/M3/geometry/portrait-source.png'
 checked(op='portrait_window',path=str(source),models=str(R/'build/windows-release/models'));r=settle();checked(op='portrait_state',select=0)
 assert all('makeupSkin' in person['settings']['automaticMasks'] for person in r['recipe']['people'])
 for panel,key in [(11,'makeup.lip'),(11,'makeup.brow'),(11,'makeup.blush'),(11,'makeup.shadow'),(11,'makeup.liner'),(11,'makeup.lash'),(11,'makeup.iris'),(11,'makeup.catchlight'),(12,'hair.color'),(12,'hair.smooth'),(12,'hair.accessory'),(13,'contour.highlight'),(14,'light.face')]:
  checked(op='portrait_state',panel=panel)
  if panel==11:checked(op='portrait_state',styleSection=0 if 'lip' in key else 1 if 'brow' in key else 3 if 'blush' in key else 4 if 'catch' in key else 2)
  elif panel==12:checked(op='portrait_state',styleSection=0 if key=='hair.color' else 3 if key=='hair.accessory' else 1)
  checked(op='portrait_state',parameter=key,value=40);settle()
  r=checked(op='portrait_state',source=str(source),render=str(O/(key+'.png')),checkRegion=True)
  assert not r['identity'],key
  assert r['regionError']<2e-6,(key,r['regionError'])
  checked(op='portrait_state',parameter=key,value=0)
 checked(op='portrait_state',panel=11);checked(op='portrait_state',styleSection=4);checked(op='portrait_state',linked=False,parameter='makeup.catchlight',value=40)
 r=checked(op='portrait_state');params=r['recipe']['people'][0]['settings']['parameters'];assert params.get('makeup.catchlight.left')==40 and params.get('makeup.catchlight.right',0)==0
 checked(op='portrait_state',panel=12);checked(op='portrait_state',styleSection=0);checked(op='portrait_state',click='portraitAddHairZone')
 r=checked(op='portrait_state');s=r['recipe']['people'][0]['settings'];key=s['hairZones'][0]['key'];assert key in s['masks']
 face_box=r['recipe']['people'][0]['face'];checked(op='portrait_state',rangeKey=key,rangeMode=1,brushSize=120,paint=[face_box[0]+face_box[2]*.8,face_box[1]])
 checked(op='portrait_state',panel=14,click='portraitLightSuggest')
 r=checked(op='portrait_state',source=str(source),render=str(O/'combined.png'),checkRegion=True);assert r['regionError']<2e-6
 recipe=r['recipe'];checked(op='portrait_state',save=str(O/'style.huelith'),source=str(source))
 checked(op='portrait_close');checked(op='portrait_window',path=str(source),recipe=recipe);settle()
 checked(op='portrait_state',source=str(source),render=str(O/'reopened.png'));assert np.array_equal(np.array(Image.open(O/'combined.png')),np.array(Image.open(O/'reopened.png')))
 checked(op='portrait_close');checked(op='shutdown');print('M3 style workflow PASS',flush=True)
