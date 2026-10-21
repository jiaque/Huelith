"""Portrait navigation ordering and complete icon/text card layout."""
from pathlib import Path
exec(compile(Path('tools/portrait_interaction_review_validate.py').read_text().split("try:\n # Deformation")[0],__file__,'exec'))
OUT=ROOT/'build/navigation-layout';OUT.mkdir(exist_ok=True)
try:
 open_window();s=call(op='portrait_state',workspaceLayout=True)
 expected=['面部重塑','小头','丰盈提拉','身材塑性','瘦脸瘦身','祛斑祛痘','祛皱','磨皮','去油光','眼部精修','牙齿','美妆','美发','修容笔','消除']
 assert s['workspaceTasks']==expected,s['workspaceTasks']
 for task in range(15):
  call(op='portrait_state',workspaceTask=task);wait();assert call(op='portrait_state',workspaceLayout=True)['workspaceTask']==task
 checked=0
 def check():
  global checked
  s=call(op='portrait_state',cardLayout=True)
  for c in s['cards']:
   assert c['width']>=c['requiredWidth'] and c['height']>=c['requiredHeight'],c
   checked+=1
  return s
 for window in [[1100,720],[1600,1000]]:
  call(op='portrait_state',resize=window,workspaceTask=2);wait()
  for g,pages in [(7,[0]),(8,[0]),(1,[0,1]),(2,list(range(6))),(0,[0,1,2]),(3,[0,1,2]),(4,[0,1,2]),(5,[0,1,2,3]),(6,[0])]:
   group(g)
   if g<6:
    for page in pages:
     call(op='portrait_state',makeupPage=page);wait();check()
   else:check()
  state=check();order=[int(x['name'].replace('portraitMakeupGroup','')) for x in sorted(state['makeupHeadings'],key=lambda x:x['y'])];assert order==[7,8,1,2,0,3,4,5,6],order
  for task in [0,1,8]:
   call(op='portrait_state',workspaceTask=task);wait();check()
  call(op='portrait_state',workspaceTask=0,volumeMode=1);wait();check()
  call(op='portrait_state',workspaceTask=8,bodyMode=2);wait();check()
 call(op='portrait_state',workspaceTask=2);wait();group(2);call(op='portrait_state',makeupPage=4);wait();call(op='portrait_state',capture=str(OUT/('makeup-'+os.environ.get('QT_SCALE_FACTOR','1')+'.png')))
 group(4);call(op='portrait_state',makeupPage=0);wait();call(op='portrait_state',click='portraitMakeupToPaint');wait();assert call(op='portrait_state',workspaceLayout=True)['workspaceTask']==12
 call(op='portrait_state',workspaceTask=14);wait();call(op='portrait_state',click='portraitEyeWrinkles');wait();assert call(op='portrait_state',workspaceLayout=True)['workspaceTask']==6
 print('Navigation order, both internal jumps and card layout passed',checked,flush=True)
finally:call(op='shutdown')
