"""Real auto skin masks, editable independent ranges, rendering and persistence."""
from pathlib import Path
exec(Path('tools/m3_geometry_validate.py').read_text(encoding='utf-8').split('with tempfile.TemporaryDirectory() as tmp:')[0])
O=R/'docs/test-evidence/M3/skin';O.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory() as tmp:
 checked(op='init',settings=tmp)
 source=R/'docs/test-evidence/M3/geometry/portrait-source.png'
 checked(op='portrait_window',path=str(source),models=str(R/'build/windows-release/models'));r=settle();assert r['count']==1
 checked(op='portrait_state',select=0,panel=5,mask=str(O/'skin-mask.png'))
 r=checked(op='portrait_state',source=str(source),render=str(O/'original.png'));assert r['identity']
 checked(op='portrait_state',parameter='skin.smooth',value=70)
 checked(op='portrait_state',parameter='skin.texture',value=15)
 r=checked(op='portrait_state',source=str(source),render=str(O/'smooth.png'),checkRegion=True);assert not r['identity'] and r['regionError']<2e-6,r.get('regionError')
 before=r['recipe'];face_box=before['people'][0]['face'];center=[face_box[0]+face_box[2]*.35,face_box[1]+face_box[3]*.67]
 checked(op='portrait_state',rangeKey='skin.smooth',rangeMode=2,brushSize=120,paint=center)
 r=checked(op='portrait_state',mask=str(O/'edited-mask.png'))
 assert set(r['recipe']['people'][0]['settings']['masks'])=={'skin.smooth'}
 assert not np.array_equal(np.array(Image.open(O/'skin-mask.png')),np.array(Image.open(O/'edited-mask.png')))
 checked(op='portrait_state',source=str(source),render=str(O/'range-overlay-on.png'))
 checked(op='portrait_state',rangeVisible=False,source=str(source),render=str(O/'range-overlay-off.png'))
 assert np.array_equal(np.array(Image.open(O/'range-overlay-on.png')),np.array(Image.open(O/'range-overlay-off.png')))
 r=checked(op='portrait_state',history=-1);assert r['recipe']==before
 checked(op='portrait_state',panel=6,parameter='color.light',value=20)
 checked(op='portrait_state',parameter='color.warmth',value=35)
 checked(op='portrait_state',source=str(source),render=str(O/'skin-color.png'))
 checked(op='portrait_state',panel=0,parameter='face.width',value=35)
 r=checked(op='portrait_state',source=str(source),render=str(O/'skin-and-shape.png'),checkRegion=True);assert r['regionError']<2e-6,r['regionError']
 recipe=r['recipe'];checked(op='portrait_state',save=str(O/'skin.huelith'),source=str(source))
 checked(op='portrait_state',panel=5,rangeVisible=True);checked(op='pump',waitMs=600)
 checked(op='portrait_state',capture=str(O/'workspace.png'))
 checked(op='portrait_close');checked(op='portrait_window',path=str(source),models=str(R/'build/windows-release/models'),recipe=recipe)
 checked(op='portrait_state',source=str(source),render=str(O/'reopened.png'))
 assert np.array_equal(np.array(Image.open(O/'skin-and-shape.png')),np.array(Image.open(O/'reopened.png')))
 checked(op='portrait_close');checked(op='shutdown')
 (O/'recipe.json').write_text(json.dumps(recipe,indent=2))
 print('M3 skin core PASS',flush=True)
