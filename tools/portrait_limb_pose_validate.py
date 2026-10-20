from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-limbs';OUT.mkdir(parents=True,exist_ok=True)
call(op='init',settings=str(OUT/'settings'));records=[]
for name in ['mediapipe-pose']:
    recipe=json.loads((OUT/f'{name}-recipe.json').read_text(encoding='utf-8'))
    source=OUT/f'{name}.jpg';size=np.array(Image.open(source).size)
    p=recipe['people'][0];p['settings']['limbGeometryVersion']=2
    for key in ['body.upperArm','body.arm','body.thigh','body.calf','body.legWidth']:
        for side,value in [(side,value) for side in ['left','right'] for value in [-100,0,100]]:
            p['settings']['parameters']={key+'.'+side:value}
            try:r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'field.bin'),source=str(source),render=str(OUT/f'{name}-{key}-{side}-{value}.png'))
            except RuntimeError as e:
                records.append(dict(name=name,key=key,side=side,refused=str(e)));print(name,key,side,'REFUSED',str(e),flush=True);continue
            if value==0:
                assert r['identity'];continue
            assert not r['identity'],(name,key,'no effect')
            mw,mh=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(mh,mw,4)[:,:,:2]
            pose=np.array(p['settings']['pose']);indices=([11,13,15] if key in ['body.upperArm','body.arm'] else [23,25,27]);indices=np.array(indices)+(0 if side=='left' else 1)
            joints=pose[indices,:2];valid=pose[indices,2]>.5;joints=joints[valid]
            drift=float(np.linalg.norm((forward_points(field,joints)-joints)*size,axis=1).max())
            assert drift<.8,(name,key,side,'joint moved',drift)
            records.append(dict(name=name,key=key,side=side,value=value,jointDrift=drift,ms=r['milliseconds']));print(records[-1],flush=True)
assert sum('jointDrift' in r for r in records)>=18,records
(OUT/'pose-metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown')
