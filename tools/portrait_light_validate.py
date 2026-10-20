from pathlib import Path
import io,base64
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-skin-light';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
for name in ['jessica-watkins','ellen-ochoa','leroy-chiao']:
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=list(Image.open(source).size);recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text());p=recipe['people'][0];p['settings']['skinLightVersion']=2
    x,y,w,h=p['face'];box=(int(x*size[0]),int(y*size[1]),int((x+w)*size[0]),int((y+h)*size[1]));cw,ch=box[2]-box[0],box[3]-box[1];sheet=Image.new('RGB',(cw*3,ch+25),(30,34,38));draw=ImageDraw.Draw(sheet)
    for col,value in enumerate([-100,0,100]):
        p['settings']['parameters']={'color.light':value};path=OUT/f'{name}-{value}.png';call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),render=str(path));sheet.paste(Image.open(path).crop(box),(col*cw,25));draw.text((col*cw+4,5),str(value),fill='white')
    sheet.save(OUT/f'{name}-comparison.png')
buf=io.BytesIO();Image.new('L',(32,32),255).save(buf,format='PNG');p['settings']['masks']={'color.light':base64.b64encode(buf.getvalue()).decode()};size=[256,16]
for kind in ['neutral','warm']:
    source=OUT/f'{kind}-ramp.png';ramp=np.tile(np.arange(256)[None,:,None],(16,1,3)).astype(float)
    if kind=='warm':ramp*=np.array([1,.75,.55])
    Image.fromarray(ramp.astype(np.uint8)).save(source);outputs={}
    for value in [0,-100,100]:
        p['settings']['parameters']={'color.light':value};binary=OUT/f'{kind}-{value}.bin';call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),render=str(binary.with_suffix('.png')),linearRender=str(binary));outputs[value]=np.fromfile(binary,np.float32).reshape(16,256,4)[:,:,:3]
    original=outputs[0]
    for value in [-100,100]:
        out=outputs[value];assert np.isfinite(out).all() and out.max()<=1+2e-6 and np.diff(out[0,:,0]).min()>=-1e-6
        assert np.max(abs(out[:,:,1]*original[:,:,0]-original[:,:,1]*out[:,:,0]))<2e-6
    assert outputs[100][0,128,0]>original[0,128,0]>outputs[-100][0,128,0]
    if kind=='neutral':assert np.max(abs(outputs[100][0,-1]-original[0,-1]))<2e-6
call(op='shutdown');print('Skin light monotonicity, chromatic ratios and highlight protection passed.')
