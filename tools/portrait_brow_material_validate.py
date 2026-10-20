from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-brow-material';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png';recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/jessica-watkins-recipe.json').read_text());p=recipe['people'][0];s=p['settings'];s['browMaterials']=call(op='portrait_geometry',source=str(source),person=p,repairAnalysis=True)['browMaterials'];s['browMaterialVersion']=2;s['parameters']={}
blank=OUT/'blank-skin.png';Image.new('RGB',Image.open(source).size,(180,140,115)).save(blank)
analysis=call(op='portrait_geometry',source=str(blank),person=p,repairAnalysis=True)['browMaterials']
import base64,io
assert all(np.asarray(Image.open(io.BytesIO(base64.b64decode(analysis[k])))).max()==0 for k in ['left','right'])
frames=[];masks=[]
for style in ['none','natural','straight','arched']:
    s['browMaterial']=style;s['parameters']={} if style=='none' else {'makeup.browShape.left':70,'makeup.browShape.right':70};path=OUT/f'{style}.png';call(op='portrait_geometry',recipe=recipe,source=str(source),size=list(Image.open(source).size),render=str(path));im=Image.open(path).convert('RGB');x,y,w,h=p['face'];frames.append(im.crop((int(x*im.width),int(y*im.height),int((x+w)*im.width),int((y+h*.65)*im.height))))
    if style!='none':
        mask=OUT/f'{style}-mask.png';call(op='portrait_geometry',recipe=recipe,size=[1200,750],styleMask='makeup.browShape.left',mask=str(mask));masks.append(np.asarray(Image.open(mask)))
assert all(m.max()>80 for m in masks);assert not np.array_equal(masks[0],masks[1]) and not np.array_equal(masks[0],masks[2])
board=Image.new('RGB',(frames[0].width*2,frames[0].height*2))
for i,frame in enumerate(frames):board.paste(frame,((i%2)*frame.width,(i//2)*frame.height))
board.resize((board.width*2,board.height*2)).save(OUT/'styles-compare.png')
def wait():
    end=time.monotonic()+40
    while time.monotonic()<end:
        call(op='pump',waitMs=20);r=call(op='portrait_state')
        if r['ready'] and not r['previewBusy']:return r
    raise AssertionError(r['status'])
s['parameters']={};call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=11);wait();call(op='portrait_state',styleSection=1);before=wait()['recipe'];call(op='portrait_state',click='portraitMaterialCard_browMaterial_straight');chosen=wait()['recipe'];assert chosen['people'][0]['settings']['browMaterialVersion']==2
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);assert wait()['recipe']==chosen
anchors=chosen['people'][0]['landmarks'];anchor=anchors[22][:2];call(op='portrait_state',click='portraitBrowAnchors');call(op='portrait_state',paintPath=[anchor,[anchor[0],anchor[1]-.01]]);moved=wait()['recipe'];assert moved['people'][0]['landmarks']!=anchors
call(op='portrait_state',history=-1);assert wait()['recipe']['people'][0]['landmarks']==anchors
call(op='portrait_state',parameter='makeup.brow',value=35);wait();call(op='portrait_state',click='portraitSwatch_755647');colored=wait()['recipe'];assert colored['people'][0]['settings']['browMaterial']=='straight'
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),checkRegion=True,save=str(OUT/'brow.huelith'));assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=colored);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Distinct brow shapes, color independent, undo, ROI and reopening passed')
