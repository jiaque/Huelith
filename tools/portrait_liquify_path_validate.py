from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
OUT=ROOT/'docs/test-evidence/M3/liquify-path';OUT.mkdir(exist_ok=True)
source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';size=np.array(Image.open(source).size)
recipe=json.loads((ROOT/'docs/test-evidence/M3/quality/leroy-chiao-recipe.json').read_text())
for p in recipe['people']:p['settings']['parameters']={};p['settings'].pop('liquify',None)
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);s=call(op='portrait_state')
  if s['ready'] and not s['previewBusy'] and s['displayedRevision']==s['previewRevision']:return s
 raise AssertionError(s['status'])
call(op='init',settings=str(OUT/'settings'));fields=[];records=[]
fx,fy,fw,fh=recipe['people'][0]['face'];start=np.array([fx+fw*.2,fy+fh*.65]);end=start+[fw*.35,0]
for count,delay in [(2,0),(41,0),(41,35)]:
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',panel=4,select=0,brushSize=100);before=wait()
 call(op='portrait_state',paint=start.tolist());idle=wait();assert idle['recipe']==before['recipe'] and idle['history']==before['history'],'stationary push created an edit'
 path=[(start+(end-start)*t).tolist() for t in np.linspace(0,1,count)]
 state=call(op='portrait_state',paintPath=path,paintStepMs=delay);frames=state['paintFrames'];state=wait()
 assert state['recipe']['people'][0]['settings'].get('liquify'),state['status']
 assert state['history']==before['history']+1
 r=call(op='portrait_geometry',size=size.tolist(),recipe=state['recipe'],field=str(OUT/'field.bin'),source=str(source),render=str(OUT/f'{count}-{delay}.png'))
 w,h=r['meshSize'];field=np.fromfile(OUT/'field.bin',np.float32).reshape(h,w,4)[:,:,:2];fields.append(field)
 motion=float(np.linalg.norm((forward_points(field,np.array([(start+end)*.5]))-(start+end)*.5)*(size-1)));assert motion>2
 if delay:assert max(frames[:-1])>frames[0],'no frame during drag'
 records.append(dict(events=count,delayMs=delay,targetMotionPixels=motion))
 call(op='portrait_close')
maximum=max(float(np.max(np.linalg.norm((f-fields[0])*(size-1),axis=2))) for f in fields[1:])
# Same axis-aligned turn and reversal, subdivided at different event densities.
corner=end+[0,fh*.08];vertices=[start,end,corner,end,start]
corner_fields=[]
for subdivisions,ending in [(1,'release'),(13,'release'),(13,'ungrab')]:
 call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',panel=4,select=0,brushSize=100);wait()
 path=[vertices[0].tolist()]
 for a,b in zip(vertices,vertices[1:]):path.extend((a+(b-a)*t).tolist() for t in np.linspace(0,1,subdivisions+1)[1:])
 call(op='portrait_state',paintPath=path,paintEnd=ending);state=wait();assert state['recipe']['people'][0]['settings'].get('liquify'),state['status']
 r=call(op='portrait_geometry',size=size.tolist(),recipe=state['recipe'],field=str(OUT/'corner.bin'))
 w,h=r['meshSize'];corner_fields.append(np.fromfile(OUT/'corner.bin',np.float32).reshape(h,w,4)[:,:,:2])
 saved=state['recipe'];r=call(op='portrait_state',source=str(source),render=str(OUT/'saved.png'),save=str(OUT/'path.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
 call(op='portrait_close');call(op='portrait_window',project=str(OUT/'path.huelith'));assert wait()['recipe']==saved
 call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.array(Image.open(OUT/'saved.png')),np.array(Image.open(OUT/'reopened.png')))
 call(op='portrait_close')
corner_difference=max(float(np.max(np.linalg.norm((f-corner_fields[0])*(size-1),axis=2))) for f in corner_fields[1:])
assert corner_difference<.05,corner_difference
(OUT/'results.json').write_text(json.dumps(dict(runs=records,maximumFieldDifferencePixels=maximum,cornerDifferencePixels=corner_difference),indent=2))
call(op='shutdown');assert maximum<.05,maximum
print(records,'maximum field difference',maximum)
