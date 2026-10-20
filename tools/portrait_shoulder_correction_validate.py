"""D14 recovery from uncertain visible elbow through real landmark dragging."""
from pathlib import Path
exec(compile(Path('tools/portrait_shoulder_width_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0], 'tools/portrait_shoulder_width_workflow_validate.py','exec'))
pose=base['people'][0]['settings']['pose']
target=pose[13][:2];pose[13]=[target[0]+.015,target[1]-.015,.4]
call(op='init',settings=str(OUT/'correction-settings'))
call(op='portrait_window',path=str(source),recipe=base);wait()
call(op='portrait_state',select=0,panel=3,linked=True)
call(op='portrait_state',parameter='body.shoulderWidth',value=40);rejected=wait()
state=call(op='portrait_state');assert '左侧肘部' in state['status'],state['status']
assert not rejected['people'][0]['settings']['parameters'].get('body.shoulderWidth.left',0)
call(op='portrait_state',toggle='portraitLocate',checked=True)
call(op='portrait_state',paintPath=[pose[13][:2],target])
corrected=wait();updated=corrected['people'][0]['settings']['pose'][13]
assert updated[2]==1 and np.linalg.norm(np.array(updated[:2])-target)<.004,updated
call(op='portrait_state',toggle='portraitLocate',checked=False)
call(op='portrait_state',parameter='body.shoulderWidth',value=40);applied=wait()
params=applied['people'][0]['settings']['parameters'];assert params['body.shoulderWidth.left']==params['body.shoulderWidth.right']==40
saved=call(op='portrait_state',source=str(source),render=str(OUT/'corrected-width.png'),save=str(OUT/'corrected-width.huelith'),checkRegion=True)
assert saved['roundtrip'] and saved['regionError']<2e-6
call(op='portrait_state',history=-1);assert wait()==corrected
call(op='portrait_state',history=-1);assert wait()==rejected
call(op='portrait_close');call(op='shutdown')
(OUT/'correction.json').write_text(json.dumps(dict(rejectedWithJointName=True,dragCorrection=updated,editAfterCorrection=True,undo=True,roundtrip=True,regionError=saved['regionError']),indent=2))
print('Named refusal, visible-joint correction, shoulder edit, history and project roundtrip passed.')
