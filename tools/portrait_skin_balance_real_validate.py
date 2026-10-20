from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-skin-balance'
call(op='init',settings=str(OUT/'real-settings'))
for name in ['jessica-watkins','john-glenn','pose']:
    source=ROOT/('docs/test-evidence/M3/semantic-limbs/mediapipe-pose.jpg' if name=='pose' else f'docs/test-evidence/M3/quality/{name}-source.png')
    recipe=json.loads((ROOT/('docs/test-evidence/M3/semantic-body-smooth/recipe.json' if name=='pose' else f'docs/test-evidence/M3/quality/{name}-recipe.json')).read_text())
    settings=recipe['people'][0]['settings'];settings['parameters']={};settings['skinBalanceVersion']=2
    frames=[]
    for value in [0,100]:
        settings['parameters']={'color.balance':value};output=OUT/f'{name}-{value}.png'
        r=call(op='portrait_geometry',recipe=recipe,size=list(Image.open(source).size),source=str(source),render=str(output),colorMetrics=True)
        if value: assert r['maximumLuminanceError']<2e-6,r
        im=Image.open(output).convert('RGB');im.thumbnail((600,700));frames.append(im)
    board=Image.new('RGB',(sum(im.width for im in frames),max(im.height for im in frames)))
    board.paste(frames[0],(0,0));board.paste(frames[1],(frames[0].width,0));board.save(OUT/f'{name}-compare.jpg')
call(op='shutdown');print('Three real portraits retain luminance; comparison sheets saved')
