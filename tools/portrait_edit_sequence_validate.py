"""User regression: body errors must not poison face/liquify edits."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/edit-sequence';OUT.mkdir(exist_ok=True)
call(op='init',settings=str(OUT/'settings'))
def settled():
    until=time.monotonic()+45
    while time.monotonic()<until:
        call(op='pump',waitMs=20);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])
def rendered():
    r=settled()
    assert r['displayedRevision']==r['previewRevision'],r['status']
    return r
records=[]
for name,base,source_name in [('leroy-chiao','quality','leroy-chiao-source.png'),('mediapipe-pose','semantic-limbs','mediapipe-pose.jpg')]:
    folder=ROOT/'docs/test-evidence/M3'/base;source=folder/source_name
    recipe=json.loads((folder/(name+'-recipe.json')).read_text(encoding='utf-8'))
    for p in recipe['people']:p['settings']['parameters']={};p['settings'].pop('liquify',None)
    call(op='portrait_window',path=str(source),recipe=recipe);rendered();call(op='portrait_state',select=0)
    for round_index in range(2):
        for key in ['body.waist','body.upperArm','body.arm','body.thigh','body.calf','body.legWidth']:
            call(op='portrait_state',panel=3,linked=True)
            before=rendered()['recipe'];call(op='portrait_state',parameter=key,value=25+round_index*10)
            r=rendered();parameters=r['recipe']['people'][0]['settings']['parameters']
            records.append(dict(image=name,round=round_index,key=key,status=r['status'],parameters=parameters))
            # Any unsupported body edit must leave the entire recipe intact.
            if '未应用' in r['status'] or '请修正' in r['status']:
                assert r['recipe']==before,(name,key,'failed edit left settings behind')
            body_parameters={k:v for k,v in parameters.items() if k.startswith('body.')}
            call(op='portrait_state',panel=0);call(op='portrait_state',parameter='face.width',value=10+round_index*5)
            r=rendered()
            assert r['recipe']['people'][0]['settings']['parameters']['face.width']==10+round_index*5
            assert {k:v for k,v in r['recipe']['people'][0]['settings']['parameters'].items() if k.startswith('body.')}==body_parameters
        print(name,round_index,'body/face passed; entering liquify',flush=True)
        call(op='portrait_state',panel=4);rendered()
        before=rendered()['recipe']
        call(op='portrait_state',liquify=True);r=rendered()
        assert r['recipe']['people'][0]['settings'].get('liquify'),name
        call(op='portrait_state',history=-1);undone=rendered()['recipe']
        if undone!=before:
            a=before['people'][0]['settings'];b=undone['people'][0]['settings']
            print('undo differing keys',[(k,a.get(k) if k!='liquify' else 'mesh',b.get(k) if k!='liquify' else 'mesh') for k in a.keys()|b.keys() if a.get(k)!=b.get(k)],flush=True)
        assert undone==before
        call(op='portrait_state',history=1);rendered()
    saved=call(op='portrait_state',source=str(source),render=str(OUT/(name+'.png')),checkRegion=True,save=str(OUT/(name+'.huelith')))
    assert saved['roundtrip'] and saved['regionError']<2e-6
    final=rendered()['recipe'];call(op='portrait_close')
    call(op='portrait_window',path=str(source),recipe=final);rendered()
    call(op='portrait_state',panel=0,select=0);call(op='portrait_state',parameter='face.width',value=20);rendered()
    call(op='portrait_close')
(OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
call(op='shutdown');print('Body/face/liquify repeated editing, failed edit isolation and save/reopen passed.')
