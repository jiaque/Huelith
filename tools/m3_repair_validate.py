"""Repair controls exercise the production window and FP32 output, not a test EXE."""
from pathlib import Path
exec(Path('tools/m3_geometry_validate.py').read_text(encoding='utf-8').split('with tempfile.TemporaryDirectory() as tmp:')[0])
O=R/'docs/test-evidence/M3/repair';O.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory() as tmp:
 checked(op='init',settings=tmp)
 source=R/'docs/test-evidence/M3/geometry/portrait-source.png'
 checked(op='portrait_window',path=str(source),models=str(R/'build/windows-release/models'));r=settle()
 checked(op='portrait_state',select=0,panel=8)
 r=checked(op='portrait_state',source=str(source),render=str(O/'neutral.png'));assert r['identity'] and not r['recipe']['people'][0]['settings'].get('repairs')
 face_box=r['recipe']['people'][0]['face'];p=[face_box[0]+face_box[2]*.3,face_box[1]+face_box[3]*.65];q=[p[0]+face_box[2]*.10,p[1]]
 checked(op='portrait_state',click='portraitRepairBrush',brushSize=30)
 checked(op='portrait_state',paint=q,alt=True)
 r=checked(op='portrait_state',paint=p);assert len(r['recipe']['people'][0]['settings']['repairs'])==1
 r=checked(op='portrait_state',source=str(source),render=str(O/'wrinkle.png'),checkRegion=True);assert not r['identity'] and r['regionError']<2e-6,r.get('regionError')
 checked(op='portrait_state',repairSelect=0,repairOpacity=0)
 r=checked(op='portrait_state',source=str(source),render=str(O/'zero.png'));assert r['identity']
 checked(op='portrait_state',repairOpacity=65)
 checked(op='portrait_state',panel=10,click='portraitAnalyzeRepairs',mask=str(O/'teeth-mask.png'),maskKey='teeth.light')
 settle()
 checked(op='portrait_state',parameter='teeth.yellow',value=50)
 checked(op='portrait_state',parameter='teeth.light',value=30)
 r=checked(op='portrait_state',source=str(source),render=str(O/'teeth.png'),checkRegion=True);assert r['regionError']<2e-6
 print(r['recipe']['people'][0]['settings'].get('teethStatus'),flush=True)
 checked(op='portrait_state',panel=7,click='portraitConcealBrush')
 checked(op='portrait_state',paint=q,alt=True);checked(op='portrait_state',paint=p)
 r=checked(op='portrait_state',source=str(source),render=str(O/'conceal.png'),checkRegion=True);assert r['regionError']<2e-6
 recipe=r['recipe'];checked(op='portrait_state',save=str(O/'repair.huelith'),source=str(source))
 checked(op='pump',waitMs=800);checked(op='portrait_state',capture=str(O/'workspace.png'))
 checked(op='portrait_close');checked(op='portrait_window',path=str(source),recipe=recipe)
 checked(op='portrait_state',source=str(source),render=str(O/'reopened.png'))
 assert np.array_equal(np.array(Image.open(O/'conceal.png')),np.array(Image.open(O/'reopened.png')))
 (O/'recipe.json').write_text(json.dumps(recipe,indent=2))
 checked(op='portrait_close');checked(op='shutdown');print('M3 repair workflow PASS',flush=True)
