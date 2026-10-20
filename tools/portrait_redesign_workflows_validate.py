from pathlib import Path
exec(Path('tools/portrait_redesign_validate.py').read_text(encoding='utf-8').split('\nwith tempfile.TemporaryDirectory')[0])
with tempfile.TemporaryDirectory(dir=R/'build',ignore_cleanup_errors=True) as tmp:
 checked(op='init',settings=tmp);base=json.loads((R/'docs/test-evidence/M3/quality/portrait-highres-recipe.json').read_text());source=R/'docs/test-evidence/M3/quality/portrait-highres-source.png'
 checked(op='portrait_window',path=str(source),recipe=base);settle();wait()
 for panel,count in [(11,5),(12,4),(13,3)]:
  checked(op='portrait_state',panel=panel)
  for tab in range(count):
   checked(op='portrait_state',styleTab=tab);wait();checked(op='portrait_state',capture=str(O/f'style-{panel}-{tab}.png'))
 checked(op='portrait_state',panel=11,styleTab=0,click='portraitSwatch_be4c5b');r=wait();assert r['recipe']['people'][0]['settings']['parameters']['makeup.lip']==50
 checked(op='portrait_state',styleTab=4,click='portraitMaterialCard_catchMaterial_ring');r=wait();assert r['recipe']['people'][0]['settings']['catchMaterial']=='ring'
 checked(op='portrait_state',panel=13,styleTab=1,click='portraitContourPaint',rangeVisible=False)
 face=r['recipe']['people'][0]['face'];x=face[0]+face[2]*.7;y=face[1]+face[3]*.65
 path=[[x+i*.001,y] for i in range(20)];r=checked(op='portrait_state',stroke=path);wait();mask=r['recipe']['people'][0]['settings']['masks']['contour.paint']
 checked(op='portrait_state',click='portraitContourLock');checked(op='portrait_state',click='portraitContourPaint');r=checked(op='portrait_state',stroke=[[x+.1,y],[x+.15,y]])
 assert r['recipe']['people'][0]['settings']['masks']['contour.paint']==mask
 checked(op='portrait_state',click='portraitSwatch_e4c7a0');r=wait();assert r['recipe']['people'][0]['settings']['masks']['contour.paint']==mask
 checked(op='portrait_state',panel=15);before=checked(op='portrait_state')['recipe'];checked(op='portrait_state',presetCard=0);r=wait();assert r['recipe']!=before
 checked(op='portrait_state',panel=5);r=wait();assert r['recipe']['people'][0]['settings']['parameters']['skin.smooth']==20
 checked(op='portrait_state',history=-1);r=wait();assert r['recipe']==before,'preset undo'
 checked(op='portrait_state',panel=1,parameter='feature.eyeSize',value=30);wait();h=checked(op='portrait_state')['history'];recipe=checked(op='portrait_state')['recipe']
 checked(op='portrait_state',hold='portraitGroupEnabled_1_眼睛',pressed=True);wait();checked(op='portrait_state',capture=str(O/'group-compare.png'))
 checked(op='portrait_state',hold='portraitGroupEnabled_1_眼睛',pressed=False);r=wait();assert r['history']==h and r['recipe']==recipe
 checked(op='portrait_state',source=str(source),render=str(O/'render.png'),save=str(O/'roundtrip.huelith'),checkRegion=True);recipe=r['recipe'];checked(op='portrait_close')
 checked(op='portrait_window',path=str(source),recipe=recipe);settle();wait();r=checked(op='portrait_state',source=str(source),render=str(O/'reopened.png'));assert np.array_equal(np.array(Image.open(O/'render.png')),np.array(Image.open(O/'reopened.png')))
 checked(op='portrait_state',comparePressed=True);checked(op='pump',waitMs=300);checked(op='portrait_state',capture=str(O/'entry-compare.png'));checked(op='portrait_state',comparePressed=False)
 print('PASS styles, locked mask, preset undo, group compare, roundtrip',flush=True)
 checked(op='portrait_close');checked(op='shutdown')

