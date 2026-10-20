from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'docs/test-evidence/M3/workspace-c02';source=ROOT/'docs/test-evidence/M3/semantic-back/postural-kyphosis.png';recipe=json.loads(source.with_name('postural-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={};recipe['people'][0]['settings'].pop('backContour',None)
call(op='init',settings=str(OUT/'fallback-settings'))
try:
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',workspaceTask=8,select=0,click='portraitBodyGroup2');wait();call(op='portrait_state',action='portraitBodyCorrect_back');wait()
 size=np.array(Image.open(source).size)
 for point in np.array([[365,74],[496,225],[459,382]])/(size-1):call(op='portrait_state',paint=point.tolist())
 s=wait();assert len(s['recipe']['people'][0]['settings'].get('backContour',[]))==3,s['status']
 call(op='portrait_state',parameter='body.back',value=70);s=wait();assert s['recipe']['people'][0]['settings']['parameters'].get('body.back')==70,s['status']
 call(op='portrait_state',bodyMode=1,bodyTool=1,parameter='body.manualSlim',value=10);s=wait();assert s['recipe']['people'][0]['settings']['parameters'].get('body.manualSlim')==10,s['status']
 print('C02 contextual back correction and manual-after-body fallback passed')
finally:call(op='portrait_close');call(op='shutdown')
