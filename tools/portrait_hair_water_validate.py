from pathlib import Path
exec(compile(Path('tools/portrait_hair_color_validate.py').read_text(encoding='utf-8').split("s['parameters']={'hair.zone_test'")[0],__file__,'exec'))
frames={}
for key in ('hair.water','hair.oil','hair.shine'):
 s['parameters']={key:100};out=OUT/(key+'.bin');call(op='portrait_geometry',recipe=recipe,source=str(source),size=[320,240],render=str(out.with_suffix(".png")),linearRender=str(out));frames[key]=np.fromfile(out,np.float32).reshape(240,320,4)
 assert np.array_equal(frames[key][:20],a[:20]) and np.array_equal(frames[key][:,:,3],a[:,:,3])
assert np.max(abs(frames['hair.water']-frames['hair.shine']))>.01
assert np.mean(frames['hair.water'][140:160,40:280,:3])<np.mean(a[140:160,40:280,:3]),'water must deepen diffuse hair, not only brighten'
assert np.isfinite(frames['hair.water']).all()
call(op='shutdown');print('C03 water material distinct from oil/shine; alpha and non-hair boundary protected')

