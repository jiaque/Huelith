"""C05 material controls: spatial independence, clipping and real output board."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],__file__,'exec'))
import copy, base64, io
OUT=ROOT/'docs/test-evidence/M3/workspace-c05/effects';OUT.mkdir(parents=True,exist_ok=True)
source=ROOT/'docs/test-evidence/M3/quality/jessica-watkins-source.png'
recipe=json.loads((source.parent/'jessica-watkins-recipe.json').read_text());person=recipe['people'][0];s=person['settings']
call(op='init',settings=str(OUT/'settings'))
try:
 analysis=call(op='portrait_geometry',repairAnalysis=True,source=str(source),person=person)
 for key in ['irisMaterials','browMaterials','lipLighting','cheekLighting']:s[key]=analysis[key]
 s['parameters']={};s['disabledParameters']={};s['enabled']={};s['masks']={}
 for key in ['eyeShadowVersion','eyeLinerVersion','eyeLashVersion','irisMaterialVersion','lipMaterialVersion','lipFinishVersion','mouthTopologyVersion','browMaterialVersion','blushMaterialVersion']:s[key]=2
 s['linerMaterial']='wing'
 size=list(Image.open(source).size)
 def mask(key,name):
  path=OUT/(name+'.png');call(op='portrait_geometry',recipe=recipe,size=size,styleMask=key,mask=str(path));return np.array(Image.open(path)).astype(float)
 pairs=[('shadow','shadowFeather',100),('liner','linerWidth',80),('liner','linerTail',80),('lash','lashLength',80),('lash','lashCurl',80),('aegyosal','aegyosalY',60),('aegyosal','aegyosalWidth',70),('iris','irisX',2),('iris','irisY',2),('iris','irisSize',80),('iris','irisFeather',90)]
 for root,control,value in pairs:
  s['parameters']={};baseline=mask('makeup.'+root+'.right',control+'-base');other=mask('makeup.'+root+'.left',control+'-other')
  s['parameters']={'makeup.'+control+'.right':value}
  changed=mask('makeup.'+root+'.right',control+'-changed');untouched=mask('makeup.'+root+'.left',control+'-untouched')
  assert abs(changed-baseline).sum()>1,(root,control,'has no effect')
  assert np.array_equal(other,untouched),(control,'changed opposite eye')
  if root=='iris':
   visible=np.array(Image.open(io.BytesIO(base64.b64decode(s['irisMaterials']['cornea.right'])))).astype(np.uint8)
   visible=np.array(Image.fromarray(visible).resize(size,Image.Resampling.BILINEAR))
   assert changed[visible==0].max(initial=0)==0,'iris leaks outside visible eye'
 # Every eye material must respect opaque eyeglass frames, even at extreme controls.
 automatic=copy.deepcopy(s['automaticMasks']);buf=io.BytesIO();Image.new('L',tuple(size),255).save(buf,format='PNG');s['automaticMasks']['eyeglassFrame']=base64.b64encode(buf.getvalue()).decode()
 for key in ['shadow','liner','lash','iris','catchlight']:
  assert mask('makeup.'+key+'.right','blocked-'+key).max()==0,key
 s['automaticMasks']=automatic
 # Actual material renders use one fixed source, not illustrative icons.
 frames=[]
 for title,params in [('original',{}),('lip',{'makeup.lip':60,'makeup.lipGloss':40}),('eyes',{'makeup.shadow.left':40,'makeup.shadow.right':40,'makeup.liner.left':60,'makeup.liner.right':60,'makeup.lash.left':60,'makeup.lash.right':60}),('blush',{'makeup.blush.left':50,'makeup.blush.right':50})]:
  s['parameters']=params;path=OUT/(title+'.png');call(op='portrait_geometry',source=str(source),recipe=recipe,size=size,render=str(path))
  im=Image.open(path).convert('RGB');x,y,w,h=person['face'];frames.append(im.crop((int(x*size[0]),int(y*size[1]),int((x+w)*size[0]),int((y+h)*size[1]))).resize((360,440)))
 board=Image.new('RGB',(1440,440))
 for i,frame in enumerate(frames):board.paste(frame,(i*360,0))
 board.save(OUT/'materials-board.png')
 print('C05 eleven detail controls change their own material; opposite eye and frame/cornea boundaries preserved')
finally:call(op='shutdown')
