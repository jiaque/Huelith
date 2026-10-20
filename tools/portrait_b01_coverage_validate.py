"""B01 full-head/upper-face coverage regression, using production renderer."""
from pathlib import Path
import base64, io
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/workspace-b01/coverage-fix';OUT.mkdir(parents=True,exist_ok=True)
call(op='init',settings=str(OUT/'settings'))
records=[]
try:
 for name in ['chris-hadfield','leroy-chiao','glasses']:
  recipe=json.loads((ROOT/f'docs/test-evidence/M3/workspace-b01/orientation/{name}-recipe.json').read_text(encoding='utf-8'))
  person=recipe['people'][0];person['settings']['contourVersion']=2;person['settings']['protectBackground']=True
  source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';im=Image.open(source);size=np.array(im.size)
  surface=np.array(person['settings']['faceSurface']['points']);lm=np.array(person['landmarks'])[:,:2]
  hair=np.array(Image.open(io.BytesIO(base64.b64decode(person['settings']['automaticMasks']['hair']))))
  yy,xx=np.where(hair>220);cap=np.array([xx/hair.shape[1],yy/hair.shape[0]]).T
  cap=cap[(cap[:,0]>surface[:,0].min())&(cap[:,0]<surface[:,0].max())&(cap[:,1]<surface[:,1].min())]
  assert len(cap)>10,'sample must include visible head-top hair'
  cap=cap[cap[:,1]<np.quantile(cap[:,1],.25)]
  x,y,w,h=person['face'];box=(int((x-w*.35)*size[0]),int((y-h*.65)*size[1]),int((x+w*1.35)*size[0]),int((y+h*1.5)*size[1]))
  cases=[('neutral',{}),('yaw-100',{'head.yaw':-100}),('yaw100',{'head.yaw':100}),('pitch-100',{'head.pitch':-100}),('pitch100',{'head.pitch':100}),('width-100',{'face.width':-100}),('width100',{'face.width':100}),('combined',{'head.yaw':100,'head.pitch':-100,'face.width':60}),('yaw40',{'head.yaw':40}),('pitch-40',{'head.pitch':-40}),('width40',{'face.width':40})]
  sheet=Image.new('RGB',(4*320,3*420),(30,34,38));draw=ImageDraw.Draw(sheet)
  for i,(label,parameters) in enumerate(cases):
   person['settings']['parameters']=parameters
   path=OUT/f'{name}-{label}.png'
   r=call(op='portrait_geometry',size=size.tolist(),recipe=recipe,source=str(source),render=str(path),field=str(OUT/'field.bin'))
   assert r['identity']==(not parameters)
   record={'name':name,'case':label,'milliseconds':r['milliseconds']}
   if parameters:
    mw,mh=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(mh,mw,4)[:,:,:2];assert np.isfinite(field).all()
    np.save(OUT/f'{name}-{label}-field.npy',field)
    lm=np.array(person['landmarks'])[:,:2];fw=np.linalg.norm((lm[16]-lm[0])*size)
    # Far shoulders/background must remain fixed, irrespective of head edits.
    assert np.max(np.abs(field[0]))==0 and np.max(np.abs(field[-1]))==0
    record['faceWidthPixels']=float(fw)
    dy,dx=np.gradient(field,1/(mh-1),1/(mw-1),axis=(0,1))
    jac=(1+dx[:,:,0])*(1+dy[:,:,1])-dx[:,:,1]*dy[:,:,0]
    assert jac.min()>.1,'folded deformation'
    record['minimumJacobian']=float(jac.min())
    if label.startswith(('yaw','pitch')):
     direction=0 if label.startswith('yaw') else 1
     hairMove=np.mean(sample(field,cap)*size,axis=0)
     browMove=np.mean(sample(field,surface[[10,151],:2])*size,axis=0)
     assert abs(hairMove[direction])>fw*.003,'head-top hair pinned'
     assert hairMove[direction]*browMove[direction]>0,'hair and forehead move in opposite directions'
     record['headTopInversePixels']=hairMove.tolist()
    if label.startswith('width'):
     temples=sample(field,surface[[127,356],:2])*size
     amount=parameters['face.width']
     assert temples[0,0]*amount>0 and temples[1,0]*amount<0,'temple direction inconsistent with face width'
     assert np.min(np.abs(temples[:,0]))>fw*.01,'upper face width not covered'
     record['templeInversePixels']=temples.tolist()
   crop=Image.open(path).crop(box);crop.thumbnail((320,390));sheet.paste(crop,((i%4)*320+(320-crop.width)//2,(i//4)*420+25));draw.text(((i%4)*320+5,(i//4)*420+5),label,fill='white')
   records.append(record)
  sheet.save(OUT/f'{name}-comparison.png')
 (OUT/'result.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
 print(json.dumps(records))
finally:call(op='shutdown')
