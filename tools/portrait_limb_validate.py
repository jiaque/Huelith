from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-limbs';OUT.mkdir(parents=True,exist_ok=True)
call(op='init',settings=str(OUT/'settings'));records=[]
for name in ['leroy-chiao','christina-koch','jasmin-moghbeli','FudanPed00001','jessica-watkins']:
    recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text())
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=np.array(Image.open(source).size)
    p=recipe['people'][0];p['settings']['limbGeometryVersion']=2
    for key in ['body.upperArm','body.arm','body.thigh','body.calf','body.legWidth']:
        for side in ['left','right']:
            p['settings']['parameters']={key+'.'+side:100}
            try:r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,field=str(OUT/'field.bin'),source=str(source),render=str(OUT/f'{name}-{key}-{side}.png'))
            except RuntimeError as e:
                records.append(dict(name=name,key=key,side=side,refused=str(e)));print(name,key,side,'REFUSED',str(e),flush=True);continue
            assert not r['identity'],(name,key,'no effect')
            mw,mh=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(mh,mw,4)[:,:,:2]
            pose=np.array(p['settings']['pose']);indices=([11,13,15] if key in ['body.upperArm','body.arm'] else [23,25,27]);indices=np.array(indices)+(0 if side=='left' else 1)
            joints=pose[indices,:2];valid=pose[indices,2]>.5;joints=joints[valid]
            drift=float(np.linalg.norm((forward_points(field,joints)-joints)*size,axis=1).max())
            assert drift<.8,(name,key,side,'joint moved',drift)
            records.append(dict(name=name,key=key,side=side,jointDrift=drift,ms=r['milliseconds']));print(records[-1],flush=True)
(OUT/'metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown')
