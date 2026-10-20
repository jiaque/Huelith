"""D16: full-field continuity and fixed anatomy at bilateral extremes."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0], str(Path('tools/portrait_contour_validate.py').resolve()), 'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-shoulder'
call(op='init',settings=str(OUT/'square-surface-settings'))
records=[]
for name in ['leroy-chiao','christina-koch','jasmin-moghbeli']:
    source=ROOT/'docs/test-evidence/M3/quality'/f'{name}-source.png'
    size=np.array(Image.open(source).size)
    recipe=json.loads((OUT/f'{name}-production-recipe.json').read_text())
    person=recipe['people'][0]
    for amount in [50,100]:
        parameters={'body.squareShoulder.right':amount}
        if name!='jasmin-moghbeli':parameters['body.squareShoulder.left']=amount
        person['settings']['parameters']=parameters
        result=call(op='portrait_geometry',recipe=recipe,size=size.tolist(),source=str(source),field=str(OUT/'square-surface.bin'),render=str(OUT/f'{name}-square-surface-{amount}.png'))
        w,h=result['meshSize']
        field=np.fromfile(OUT/'square-surface.bin',np.float32).reshape(h,w,4)[:,:,:2]
        # Derivatives of the actual inverse sampling map, including support
        # boundaries. Check the entire mesh, not only the fitted shoulder line.
        dy,dx=np.gradient(field,1/(h-1),1/(w-1),axis=(0,1))
        determinant=(1+dx[:,:,0])*(1+dy[:,:,1])-dy[:,:,0]*dx[:,:,1]
        pose=np.array(person['settings']['pose'])
        joints=pose[[13,14,15,16,23,24]]
        fixed=joints[joints[:,2]>=.6,:2]
        drift=float(np.linalg.norm((forward_points(field,fixed)-fixed)*(size-1),axis=1).max())
        lm=np.array(person['landmarks'])[:,:2]
        face=float(np.linalg.norm((forward_points(field,lm)-lm)*(size-1),axis=1).max())
        record=dict(name=name,amount=amount,minimumJacobian=float(determinant.min()),fixedJointDrift=drift,faceDrift=face)
        records.append(record);print(record,flush=True)
        assert determinant.min()>.05 and drift<.5 and face<.5,record
(OUT/'square-surface-results.json').write_text(json.dumps(records,indent=2))
call(op='shutdown')
