"""Restoring manual liquify must not leave protection compensation behind."""
from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],str(Path('tools/portrait_freeze_workflow_validate.py').resolve()),'exec'))
call(op='init',settings=str(OUT/'reset-settings'))
call(op='portrait_window',project=str(OUT/'freeze.huelith'));wait()
call(op='portrait_state',select=0,panel=4);wait()
call(op='portrait_state',click='portraitLiquifyReset');restored=field('reset-manual')
motion=float(np.max(np.linalg.norm(restored*(size-1),axis=2)))
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=recipe);wait()
call(op='portrait_state',select=0,panel=0,parameter='face.width',value=12);automatic=field('reset-auto-before')
call(op='portrait_state',panel=4,brushSize=100);wait()
fx,fy,fw,fh=recipe['people'][0]['face'];start=[fx+fw*.3,fy+fh*.65];end=[fx+fw*.55,fy+fh*.65]
call(op='portrait_state',paintPath=[start,end]);wait()
call(op='portrait_state',click='portraitFreeze1',brushSize=500);call(op='portrait_state',paintPath=[start,end]);wait()
call(op='portrait_state',liquifyTool=0,brushSize=100);call(op='portrait_state',paintPath=[start,end]);frozen=field('reset-auto-frozen')
call(op='portrait_state',click='portraitFreezeClear');cleared=field('reset-clear')
clear_error=float(np.max(np.linalg.norm((cleared-frozen)*(size-1),axis=2)))
call(op='portrait_state',click='portraitLiquifyReset');reset=field('reset-auto-after')
auto_error=float(np.max(np.linalg.norm((reset-automatic)*(size-1),axis=2)))
(OUT/'reset-results.json').write_text(json.dumps(dict(residualMotionPixels=motion,clearChangedPixels=clear_error,automaticPreservationPixels=auto_error),indent=2))
call(op='portrait_close');call(op='shutdown');print('Manual reset residual pixels',motion,'clear',clear_error,'automatic',auto_error)
assert motion<.05,'Restore all manual liquify must also remove its frozen/thawed contribution'
assert clear_error<.05,'Clearing freeze must preserve the visible result'
assert auto_error<.05,'Reset manual liquify must retain automatic face adjustment'
