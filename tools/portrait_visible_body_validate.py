from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],'tools/portrait_freeze_workflow_validate.py','exec'))
import copy
OUT=ROOT/'build/visible-body';OUT.mkdir(exist_ok=True)
os.environ['HUELITH_AI_WORKER_EXE']=str(ROOT/'build/windows-release/Huelith.exe')
base=json.loads((ROOT/'docs/test-evidence/M3/semantic-pelvis/jassi-standing-recipe.json').read_text())
base['people'][0]['settings']['parameters']={}
source=ROOT/'research/m3/portraits/jassi-standing.jpg'
call(op='init',settings=str(OUT/'settings'))
call(op='portrait_window',path=str(source),recipe=base);wait();call(op='portrait_state',workspaceTask=8,select=0);wait()
results=[]
current=0
for key in ['body.waist','body.abdomen','body.arm','body.upperArm','body.legWidth','body.thigh','body.calf','body.straightLeg','body.legLength','body.height','body.shoulderWidth','body.shoulder','body.squareShoulder','body.neck','body.neckLength','body.trapezius','body.hipWidth']:
 group=2 if key in ['body.waist','body.abdomen'] else 3 if key in ['body.arm','body.upperArm'] else 4 if key in ['body.legWidth','body.thigh','body.calf','body.straightLeg','body.legLength','body.height'] else 5 if key=='body.hipWidth' else 1
 if current!=group:call(op='portrait_state',click='portraitBodyGroup'+str(group));wait();current=group
 call(op='portrait_state',parameter=key,value=35);s=wait();p=s['recipe']['people'][0]['settings']['parameters'];accepted=p.get(key)==35 or p.get(key+'.left')==35
 print(key,accepted,s['status'],flush=True);results.append([key,accepted,s['status']]);assert accepted,(key,s['status'])
 if accepted and key in ['body.waist','body.legLength','body.height']:call(op='portrait_state',source=str(source),render=str(OUT/(key+'.png')))
 call(op='portrait_state',parameter=key,value=0);wait()
call(op='portrait_state',workspaceTask=1);wait()
for index,key in enumerate(['head.crown','head.hairVolume','head.hairline']):
 call(op='portrait_state',click='portraitHairCard'+str(index));wait()
 call(op='portrait_state',parameter=key,value=35);s=wait();print(key,s['recipe']['people'][0]['settings']['parameters'].get(key),s['status'],flush=True);assert s['recipe']['people'][0]['settings']['parameters'].get(key)==35,s['status']
 call(op='portrait_state',parameter=key,value=0);wait()
(OUT/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
# Repeated mixed edits, including single-leg adjustment next to the other leg.
call(op='portrait_state',workspaceTask=8,click='portraitBodyGroup2');wait()
call(op='portrait_state',parameter='body.waist',value=50);wait()
call(op='portrait_state',click='portraitBodyGroup4');wait()
call(op='portrait_state',bodySide=1,parameter='body.legLength',value=35);s=wait();assert s['recipe']['people'][0]['settings']['parameters']['body.legLength.left']==35,s['status']
call(op='portrait_state',bodySide=0,parameter='body.height',value=35);wait()
call(op='portrait_state',workspaceTask=1,click='portraitHairCard0');wait();call(op='portrait_state',parameter='head.crown',value=35);s=wait();assert s['recipe']['people'][0]['settings']['parameters']['head.crown']==35
call(op='portrait_state',workspaceTask=9,slimMode=1,liquifyTool=0,brushSize=70,paintPath=[[.46,.19],[.465,.19],[.47,.19]]);s=wait();assert s['recipe']['people'][0]['settings'].get('liquify')
call(op='portrait_state',workspaceTask=1);wait();call(op='portrait_state',workspaceTask=9,slimMode=1,liquifyTool=0,paintPath=[[.47,.19],[.475,.19]]);wait()
r=call(op='portrait_state',source=str(source),render=str(OUT/'combined.png'),save=str(OUT/'combined.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close')
# A low-confidence heel is present; an off-image ankle is genuinely absent.
bad=copy.deepcopy(base)
for i in [27,28]:bad['people'][0]['settings']['pose'][i]=[.5,1.2,0.01]
call(op='portrait_window',path=str(source),recipe=bad);wait();call(op='portrait_state',workspaceTask=8,select=0,click='portraitBodyGroup4');wait()
call(op='portrait_state',parameter='body.legLength',value=35);s=wait();assert not s['recipe']['people'][0]['settings']['parameters'].get('body.legLength.left',0)
call(op='portrait_state',workspaceTask=9,slimMode=1,liquifyTool=0,brushSize=70,paintPath=[[.46,.19],[.47,.19]]);s=wait();assert s['recipe']['people'][0]['settings'].get('liquify')
call(op='portrait_close')
# Clothed walking people: slight knee bend must not invoke straight-leg restrictions.
r=json.loads((ROOT/'docs/test-evidence/M3/semantic-pelvis/FudanPed00001-recipe.json').read_text())
clothes=ROOT/'research/m2/instances/FudanPed00001.png'
for person in r['people']:person['settings']['parameters']={}
for key,version in [('body.waist','waistGeometryVersion'),('body.legLength','legLengthVersion'),('body.height','heightVersion')]:
 candidate=copy.deepcopy(r);settings=candidate['people'][1]['settings'];settings[version]=2
 settings['parameters']={key:35} if key!='body.legLength' else {key+'.left':35,key+'.right':35}
 result=call(op='portrait_geometry',recipe=candidate,size=Image.open(clothes).size,source=str(clothes),render=str(OUT/(key+'-clothes.png')))
 assert not result['identity'],key
call(op='shutdown')
print('Visible body/hair, mixed tools, rejection recovery, project roundtrip and clothed walking cases passed.',flush=True)
