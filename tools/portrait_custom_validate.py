from pathlib import Path
import io,base64
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-skin-custom';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
for name in ['jessica-watkins','ellen-ochoa','leroy-chiao']:
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';size=list(Image.open(source).size);recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text());p=recipe['people'][0];p['settings']['skinCustomVersion']=2;p['settings']['skinColor']=[.5,.3,.2]
    x,y,w,h=p['face'];box=(int(x*size[0]),int(y*size[1]),int((x+w)*size[0]),int((y+h)*size[1]));cw,ch=box[2]-box[0],box[3]-box[1];sheet=Image.new('RGB',(cw*2,ch+25),(30,34,38));draw=ImageDraw.Draw(sheet)
    for col,value in enumerate([0,100]):
        p['settings']['parameters']={'color.custom':value};path=OUT/f'{name}-{value}.png';assert call(op='portrait_geometry',recipe=recipe,size=size,source=str(source),render=str(path),colorMetrics=True)['maximumLuminanceError']<2e-6;sheet.paste(Image.open(path).crop(box),(col*cw,25));draw.text((col*cw+4,5),str(value),fill='white')
    sheet.save(OUT/f'{name}-comparison.png')

call(op='shutdown');print('Custom skin chroma preserves linear luminance on real portraits.')
