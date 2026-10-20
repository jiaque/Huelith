from pathlib import Path
import io,base64
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-skin-uniform';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
for name in ['jessica-watkins','ellen-ochoa','leroy-chiao']:
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=list(Image.open(source).size);recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text());p=recipe['people'][0];p['settings']['skinUniformVersion']=2
    x,y,w,h=p['face'];box=(int(x*size[0]),int(y*size[1]),int((x+w)*size[0]),int((y+h)*size[1]));cw,ch=box[2]-box[0],box[3]-box[1];sheet=Image.new('RGB',(cw*2,ch+25),(30,34,38));draw=ImageDraw.Draw(sheet)
    for col,value in enumerate([0,100]):
        p['settings']['parameters']={'skin.uniform':value};path=OUT/f'{name}-{value}.png';call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),render=str(path));sheet.paste(Image.open(path).crop(box),(col*cw,25));draw.text((col*cw+4,5),str(value),fill='white')
    sheet.save(OUT/f'{name}-comparison.png')
buf=io.BytesIO();Image.new('L',(32,32),255).save(buf,format='PNG');p['settings']['masks']={key:base64.b64encode(buf.getvalue()).decode() for key in ['skin.uniform','color.balance']};p['face']=[0,0,1,1];size=[256,128]
records=[]
for kind in ['neutral-shading','local-red']:
    x,y=np.meshgrid(np.arange(256),np.arange(128));ramp=100+x*.35+2*np.sin(x*2*np.pi/3);rgb=np.repeat(ramp[:,:,None],3,axis=2)
    if kind=='local-red':rgb[:,:,0]+=45*np.exp(-((x-128)**2+(y-64)**2)/100)
    source=OUT/f'{kind}.png';Image.fromarray(np.uint8(rgb)).save(source);outputs={}
    for value in [0,100]:
        p['settings']['parameters']={'skin.uniform':value};binary=OUT/f'{kind}-{value}.bin';call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),render=str(binary.with_suffix('.png')),linearRender=str(binary));outputs[value]=np.fromfile(binary,np.float32).reshape(128,256,4)[:,:,:3]
    before,after=outputs[0],outputs[100];delta=float(np.max(abs(after-before)));records.append(dict(kind=kind,maximumChange=delta))
    if kind=='neutral-shading':assert delta<2e-6,delta
    else:
        assert delta>1e-4
        assert after[64,128,0]-after[64,128,1]<before[64,128,0]-before[64,128,1]
        assert np.max(abs(after[:,0:60]-before[:,0:60]))<2e-6
(OUT/'metrics.json').write_text(json.dumps(records,indent=2));call(op='shutdown');print(records)
