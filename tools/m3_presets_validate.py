"""Preview/apply/cancel, editable presets and cross-photo coordinate isolation."""
from pathlib import Path
exec(Path('tools/m3_geometry_validate.py').read_text(encoding='utf-8').split('with tempfile.TemporaryDirectory() as tmp:')[0])
O=R/'docs/test-evidence/M3/presets';O.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory() as tmp:
 checked(op='init',settings=tmp)
 source=R/'docs/test-evidence/M3/geometry/portrait-source.png'
 checked(op='portrait_window',path=str(source),models=str(R/'build/windows-release/models'));r=settle();checked(op='portrait_state',select=0,panel=15)
 baseline=checked(op='portrait_state')['recipe']
 r=checked(op='portrait_state',presetIndex=0,previewPreset=False);assert r['recipe']==baseline
 r=checked(op='portrait_state',presetStrength=50,previewPreset=True);params=r['recipe']['people'][0]['settings']['parameters'];assert params['skin.smooth']==10 and params['color.redness']==7.5
 checked(op='portrait_state',panel=5,parameter='skin.smooth',value=33)
 r=checked(op='portrait_state',source=str(source),render=str(O/'preset-edit.png'));assert not r['identity']
 incoming=dict(r['recipe']['people'][0]['settings']);incoming['masks']={'fake':'do not copy'};incoming['repairs']=[{'bad':'do not copy'}];incoming['pose']=['do not copy'];incoming['liquify']={'do':'not copy'}
 checked(op='portrait_close')
 target=R/'research/m2/instances/FudanPed00001.png';checked(op='portrait_window',path=str(target),models=str(R/'build/windows-release/models'));r=settle();assert r['count']==2
 checked(op='portrait_state',select=1,incomingPreset=incoming);checked(op='portrait_state',panel=15,presetIndex=5)
 before=checked(op='portrait_state')['recipe'];r=checked(op='portrait_state',previewPreset=True)
 assert r['recipe']['people'][0]==before['people'][0]
 s=r['recipe']['people'][1]['settings'];assert s['parameters']['skin.smooth']==33 and 'masks' not in s and 'repairs' not in s and 'liquify' not in s
 assert s['pose']==before['people'][1]['settings']['pose']
 r=checked(op='portrait_state',save=str(O/'synced.huelith'),source=str(target));assert r['roundtrip']
 checked(op='portrait_close');checked(op='shutdown');print('M3 preset core PASS',flush=True)
