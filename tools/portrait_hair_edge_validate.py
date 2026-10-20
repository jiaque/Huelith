from pathlib import Path
exec(compile(Path('tools/portrait_contour_validate.py').read_text(encoding='utf-8').split("call(op='init'")[0],str(Path('tools/portrait_contour_validate.py').resolve()),'exec'))
import copy,io,base64
OUT=ROOT/'docs/test-evidence/M3/semantic-hair-edge';OUT.mkdir(exist_ok=True);call(op='init',settings=str(OUT/'settings'))
def wait():
 end=time.monotonic()+60
 while time.monotonic()<end:
  call(op='pump',waitMs=20);r=call(op='portrait_state')
  if r['ready'] and not r['previewBusy']:return r
 raise AssertionError(r['status'])
def encoded(im):
 b=io.BytesIO();im.save(b,format='PNG');return base64.b64encode(b.getvalue()).decode()
real=json.loads((ROOT/'docs/test-evidence/M3/quality/leroy-chiao-recipe.json').read_text());recipe=copy.deepcopy(real);p=recipe['people'][0];s=p['settings'];s['parameters']={};s['hairEdgeVersion']=1;yy,xx=np.mgrid[:240,:320];alpha=np.clip((161+2*np.sin(yy*.22)-xx)/2,0,1);coarse=np.clip((164-xx)/8,0,1);s['automaticMasks']={'hair':encoded(Image.fromarray((coarse*255).astype('uint8')))}
for name,fg,bg in [('dark',.025,.8),('light',.8,.025)]:
 linear=alpha*fg+(1-alpha)*bg;srgb=np.where(linear<=.0031308,12.92*linear,1.055*linear**(1/2.4)-.055);source=OUT/(name+'.png');Image.fromarray(np.repeat(np.round(srgb*255).astype('uint8')[:,:,None],3,axis=2)).save(source)
 lighting=call(op='portrait_geometry',source=str(source),person=p,repairAnalysis=True)['hairLighting'];refined=Image.open(io.BytesIO(base64.b64decode(lighting['edgeAlpha']))).convert('L').resize((320,240),Image.Resampling.BILINEAR);refined.save(OUT/(name+'-alpha.png'));r=np.asarray(refined)/255.;area=np.s_[20:220,150:172];old=np.mean(abs(coarse[area]-alpha[area]));new=np.mean(abs(r[area]-alpha[area]));print(name,old,new,flush=True);assert new<old*.8
 assert np.max(r[:,190:])==0 and np.min(r[:,:130])==1
 s['hairLighting']=lighting;s['hairEdgeVersion']=1;s['parameters']={'hair.color':80};s['hairColorVersion']=2;s['colors']={'hair.color':[.2,.035,.02]};call(op='portrait_geometry',recipe=recipe,source=str(source),size=[320,240],render=str(OUT/(name+'-dyed.png')),linearRender=str(OUT/(name+'-dyed.bin')))
 s['parameters']['hair.color']=0;call(op='portrait_geometry',recipe=recipe,source=str(source),size=[320,240],render=str(OUT/(name+'-base.png')),linearRender=str(OUT/(name+'-base.bin')));dyed=np.fromfile(OUT/(name+'-dyed.bin'),np.float32).reshape(240,320,4);original=np.fromfile(OUT/(name+'-base.bin'),np.float32).reshape(240,320,4);change=dyed[:,:,:3]-original[:,:,:3];interior=change[100,100];expected=alpha[:,:,None]*interior;error=np.mean(abs(change[20:220,155:167]-expected[20:220,155:167]));assert error<.003,(name,error)
 assert np.array_equal(dyed[:,:,3],original[:,:,3]) and np.array_equal(dyed[:,190:],original[:,190:])
# Indistinguishable foreground/background must preserve the uncertain mask.
source=OUT/'ambiguous.png';Image.new('RGB',(320,240),(100,100,100)).save(source);lighting=call(op='portrait_geometry',source=str(source),person=p,repairAnalysis=True)['hairLighting'];fallback=Image.open(io.BytesIO(base64.b64decode(lighting['edgeAlpha']))).convert('L');s['hairEdgeVersion']=0;call(op='portrait_geometry',recipe=recipe,size=list(fallback.size),styleMask='hair.color',mask=str(OUT/'ambiguous-original-mask.png'));assert np.array_equal(np.asarray(fallback),np.asarray(Image.open(OUT/'ambiguous-original-mask.png')))
recipe=real;s=recipe['people'][0]['settings'];s['parameters']={'hair.color':60};s['hairColorVersion']=2;source=ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png';call(op='portrait_window',path=str(source),recipe=recipe);wait();call(op='portrait_state',select=0,panel=12);wait();call(op='portrait_state',styleSection=0);before=wait()['recipe'];call(op='portrait_state',click='portraitHairRefineEdges');after=wait()['recipe'];assert after['people'][0]['settings']['hairEdgeVersion']==1 and after['people'][0]['settings']['hairLighting']['edgeVersion']==2
call(op='portrait_state',history=-1);assert wait()['recipe']==before
call(op='portrait_state',history=1);assert wait()['recipe']==after
call(op='portrait_state',source=str(source),render=str(OUT/'automatic.png'));call(op='portrait_state',click='portraitHairEdgeBrush');call(op='portrait_state',rangeMode=2);beforePaint=wait()['recipe'];call(op='portrait_state',paintPath=[[.36,.15],[.42,.15],[.48,.15]]);afterPaint=wait()['recipe'];assert 'hair.edge' in afterPaint['people'][0]['settings']['masks']
call(op='portrait_state',history=-1);assert wait()['recipe']==beforePaint
call(op='portrait_state',history=1);assert wait()['recipe']==afterPaint
call(op='portrait_state',click='portraitHairEdgeReset');assert 'hair.edge' not in wait()['recipe']['people'][0]['settings']['masks']
call(op='portrait_state',history=-1);after=wait()['recipe'];assert after==afterPaint
r=call(op='portrait_state',source=str(source),render=str(OUT/'workflow.png'),save=str(OUT/'edge.huelith'),checkRegion=True);assert r['roundtrip'] and r['regionError']<2e-6
call(op='portrait_close');call(op='portrait_window',path=str(source),recipe=after);wait();call(op='portrait_state',source=str(source),render=str(OUT/'reopened.png'));assert np.array_equal(np.asarray(Image.open(OUT/'workflow.png')),np.asarray(Image.open(OUT/'reopened.png')))
call(op='portrait_close');call(op='shutdown');print('Hair boundary estimates improve both contrast polarities; UI, region and saving passed')
