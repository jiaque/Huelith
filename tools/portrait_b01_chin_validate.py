from pathlib import Path
import base64,io
exec(compile(Path('tools/portrait_b01_workflow_validate.py').read_text(encoding='utf-8').split("\ncall(op='init'")[0],__file__,'exec'))
OUT=ROOT/'docs/test-evidence/M3/workspace-b01/chin-correction';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'settings'));results=[]
try:
 for name in ['john-glenn','chris-hadfield','leroy-chiao','ellen-ochoa']:
  source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png'
  call(op='portrait_window',path=str(source),models=str(ROOT/'build/windows-release/models'));wait();call(op='portrait_state',workspaceTask=3,select=0,panel=0);wait()
  call(op='portrait_state',parameter='face.doubleChin',value=80);s=wait();person=s['recipe']['people'][0];analysis=person['settings'].get('doubleChinAnalysis',{})
  record={'sample':name,'status':s['status'],'applied':bool(analysis)}
  if analysis:
   mask=Image.open(io.BytesIO(base64.b64decode(analysis['mask'])));mask.save(OUT/f'{name}-mask.png')
   r=call(op='portrait_state',source=str(source),render=str(OUT/f'{name}-after.png'),save=str(OUT/f'{name}.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
   (OUT/f'{name}-recipe.json').write_text(json.dumps(s['recipe']),encoding='utf-8')
   im=Image.open(source);x,y,w,h=person['face'];box=(int((x-w*.2)*im.width),int((y+h*.65)*im.height),int((x+w*1.2)*im.width),int((y+h*1.45)*im.height))
   sheet=Image.new('RGB',(960,400));sheet.paste(im.crop(box).resize((480,400)),(0,0));sheet.paste(Image.open(OUT/f'{name}-after.png').crop(box).resize((480,400)),(480,0));sheet.save(OUT/f'{name}-comparison.png')
  else:assert '未找到' in s['status'],s['status']
  call(op='portrait_state',parameter='face.width',value=-15);assert wait()['status'].startswith('已调整')
  results.append(record);call(op='portrait_close')
 (OUT/'result.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(results,ensure_ascii=True))
finally:call(op='portrait_close');call(op='shutdown')
