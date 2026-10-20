"""Check local source distance and removal of a visible dark crease, not just pixel changes."""
from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0], __file__, 'exec'))
OUT = ROOT / 'build/nearby-repair'
OUT.mkdir(exist_ok=True)
call(op='init', settings=str(OUT/'settings'))
try:
    # A crease across a gently shaded skin surface with small texture variations.
    y, x = np.mgrid[:256, :256]
    base = 150 + x*.08 + 1.5*np.sin(x*1.8)*np.cos(y*1.3)
    pixels = np.stack([base+30, base, base-20], axis=2)
    pixels -= (32*np.exp(-((y-128)/1.8)**2)*(abs(x-128)<38))[..., None]
    source = OUT/'crease.png'
    Image.fromarray(np.uint8(pixels)).save(source)
    stroke = dict(version=1, points=[[v/256, .5] for v in range(96,161,4)],
                  radius=8/256, feather=.5, opacity=.7)
    r = call(op='portrait_geometry', source=str(source), healingPath=stroke,
             render=str(OUT/'repaired.png'))
    offsets = np.array(r['stroke']['sourceOffsets'])
    assert np.linalg.norm(offsets*256, axis=1).max() <= 48.001
    after = np.array(Image.open(OUT/'repaired.png').convert('RGB')).astype(float)
    before = np.array(Image.open(source).convert('RGB')).astype(float)
    def depth(a):
        return ((a[120,105:151]+a[136,105:151])*.5-a[128,105:151]).mean()
    assert depth(after) < depth(before)*.65, (depth(before), depth(after))
    # No clean nearby source: reject instead of silently jumping to distant skin.
    mask = Image.new('L', (256,256), 0)
    ImageDraw.Draw(mask).rectangle((190,70,250,190), fill=255)
    mask.save(OUT/'far-only.png')
    try:
        call(op='portrait_geometry', source=str(source), healingPath=stroke,
             sourceMask=str(OUT/'far-only.png'))
        raise AssertionError('Distant source must not be selected')
    except RuntimeError:
        pass
    def wait():
        end = time.monotonic()+40
        while time.monotonic()<end:
            call(op='pump', waitMs=20)
            state=call(op='portrait_state')
            if state['ready'] and not state['previewBusy']:
                return state
        raise AssertionError(state['status'])
    photo=ROOT/'docs/test-evidence/M3/quality/john-glenn-source.png'
    recipe=json.loads(photo.with_name('john-glenn-recipe.json').read_text(encoding='utf-8'))
    recipe['people'][0]['settings']['parameters']={}
    recipe['people'][0]['settings']['repairs']=[]
    call(op='portrait_window', path=str(photo), recipe=recipe)
    wait()
    call(op='portrait_state', workspaceTask=5, select=0, brushSize=12)
    wait()
    call(op='portrait_state', paint=[360/943,245/1200])
    state=wait()
    assert len(state['recipe']['people'][0]['settings']['repairs'])==1,state['status']
    call(op='portrait_state', repairSelect=0)
    call(op='portrait_state', paint=[390/943,245/1200])
    state=wait()
    assert len(state['recipe']['people'][0]['settings']['repairs'])==2,state['status']
    call(op='portrait_close')
    print('Nearby source bound, crease reduction and distant-source rejection passed',
          depth(before), depth(after))
finally:
    call(op='shutdown')
