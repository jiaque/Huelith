from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/semantic-smooth-styles';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'frequency-settings'))
recipe=json.loads((ROOT/'docs/test-evidence/M3/semantic-body-smooth/recipe.json').read_text());source=ROOT/'docs/test-evidence/M3/semantic-limbs/mediapipe-pose.jpg';size=Image.open(source).size;p=recipe['people'][0];p['settings']['parameters']={'skin.bodySmooth':70};p['settings']['smoothVersion']=2
arrays=[]
for style in ['classic','texture','native']:
    p['settings']['smoothStyle']=style;path=OUT/(style+'-body.bin');call(op='portrait_geometry',source=str(source),recipe=recipe,size=list(size),render=str(path.with_suffix('.png')),linearRender=str(path));arrays.append(np.fromfile(path,np.float32))
assert max(np.max(abs(a-arrays[0])) for a in arrays)<2e-6
call(op='shutdown');print('Face smoothing style leaves independent body-only rendering unchanged')
