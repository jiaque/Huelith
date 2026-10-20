from pathlib import Path
exec(compile(Path('tools/portrait_freeze_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
import copy,base64,io
OUT=ROOT/'docs/test-evidence/M3/workspace-c03'
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';recipe=json.loads((source.parent/'leroy-chiao-recipe.json').read_text());recipe['people'][0]['settings']['parameters']={}
call(op='init',settings=str(OUT/'extras-settings'))
try:
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',workspaceTask=1,select=0,hairPage=0);wait();call(op='portrait_state',click='portraitHairCard3');wait()
 call(op='portrait_state',paint=[.49,.19],alt=True);wait();call(op='portrait_state',brushSize=45,paintPath=[[.56,.18],[.58,.20]]);s=wait();stroke=s['recipe']['people'][0]['settings']['repairs'][-1];assert stroke['kind']=='hairline' and stroke['hairSourceScale']==1
 call(op='portrait_state',capture=str(OUT/'003-fill.png'))
 variants=[]
 for angle,scale in [(0,1),(65,1),(0,1.5)]:
  r=copy.deepcopy(s['recipe']);r['people'][0]['settings']['repairs'][-1].update(hairSourceAngle=angle,hairSourceScale=scale);path=OUT/f'fill-{angle}-{scale}.png';call(op='portrait_geometry',source=str(source),size=list(Image.open(source).size),recipe=r,render=str(path));variants.append(np.array(Image.open(path)))
 assert np.max(abs(variants[0].astype(int)-variants[1]))>15 and np.max(abs(variants[0].astype(int)-variants[2]))>15
 call(op='portrait_state',hairPage=3);wait();call(op='portrait_state',click='portraitDecalAdd_blue-flower');s=wait();q=np.array(s['recipe']['people'][0]['settings']['decals'][0]['quad']);c=q.mean(0);top=(q[0]+q[1])/2;handle=top+(top-c)*.35
 call(op='portrait_state',paintPath=[handle.tolist(),(c+np.array([handle[1]-c[1],c[0]-handle[0]])).tolist()]);s=wait();rot=np.array(s['recipe']['people'][0]['settings']['decals'][0]['quad']);assert np.max(abs(rot-q))>.01
 call(op='portrait_state',paintPath=[rot[2].tolist(),(rot[2]+[.02,.02]).tolist()]);s=wait();assert s['recipe']['people'][0]['settings']['decals'][0]['quad']!=rot.tolist()
 call(op='portrait_state',click='portraitDecalAdd_white-flower');s=wait();assert len(s['recipe']['people'][0]['settings']['decals'])==2
 # Embed an imported alpha PNG, then remove file dependence before reopening.
 r=s['recipe'];settings=r['people'][0]['settings'];image=Image.new('RGBA',(1024,1024),(30,160,220,190));b=io.BytesIO();image.save(b,format='PNG');settings['decalAssets']['imported']=base64.b64encode(b.getvalue()).decode();settings['decals'][0]['asset']='imported'
 call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=r);wait();v=call(op='portrait_state',source=str(source),save=str(OUT/'embedded.huelith'),render=str(OUT/'embedded.png'),checkRegion=True);assert v['roundtrip'] and v['regionError']<2e-6
 call(op='portrait_close');call(op='portrait_window',project=str(OUT/'embedded.huelith'));wait();call(op='portrait_state',source=str(source),render=str(OUT/'embedded-reopen.png'));assert np.array_equal(np.array(Image.open(OUT/'embedded.png')),np.array(Image.open(OUT/'embedded-reopen.png')))
 print('Fill source transform; decal rotation, fitting, multiple instances and embedded asset reopening passed')
finally:call(op='portrait_close');call(op='shutdown')
