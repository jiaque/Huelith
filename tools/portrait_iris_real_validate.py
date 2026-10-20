from pathlib import Path
import io,base64
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-iris-fit';call(op='init',settings=str(OUT/'real-settings'))
for name in ['jessica-watkins','john-glenn','leroy-chiao']:
    source=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png';recipe=json.loads((ROOT/f'docs/test-evidence/M3/quality/{name}-recipe.json').read_text());p=recipe['people'][0]
    r=call(op='portrait_geometry',repairAnalysis=True,source=str(source),person=p)['irisMaterials'];print(name,{k:v for k,v in r.items() if k.startswith('geometry')})
    p['settings']['irisMaterials']=r;p['settings']['irisMaterialVersion']=2;p['settings']['parameters']={'makeup.iris.left':70,'makeup.iris.right':70}
    output=OUT/f'{name}-iris.png';call(op='portrait_geometry',source=str(source),recipe=recipe,size=list(Image.open(source).size),render=str(output))
    original=Image.open(source).convert('RGB');out=Image.open(output).convert('RGB');x,y,w,h=p['face'];box=(int(x*out.width),int(y*out.height),int((x+w)*out.width),int((y+h*.65)*out.height));a=original.crop(box);b=out.crop(box);board=Image.new('RGB',(a.width*2,a.height));board.paste(a);board.paste(b,(a.width,0));board.resize((a.width*4,a.height*2)).save(OUT/f'{name}-compare.png')
call(op='shutdown')
