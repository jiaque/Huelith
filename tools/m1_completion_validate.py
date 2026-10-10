"""Fixed M1 interaction completion checks: Python -> production Qt, no test EXE."""
import argparse,ctypes,json,os,tempfile,time,traceback,shutil,zipfile,struct
from pathlib import Path
from PIL import Image,ImageDraw,ImageChops
ROOT=Path(__file__).resolve().parents[1]
CTRL=0x04000000;SHIFT=0x02000000;ALT=0x08000000

def main():
 p=argparse.ArgumentParser();p.add_argument('--build',default='windows-release');p.add_argument('--only');p.add_argument('--platform',default='windows');p.add_argument('--output',type=Path);args=p.parse_args()
 out=args.output or ROOT/('docs/test-evidence/M1/completion' if args.platform=='windows' else 'docs/test-evidence/M1/completion-offscreen');out.mkdir(parents=True,exist_ok=True)
 qt=Path(r'D:\Program Files\Qt\6.10.3\msvc2022_64');dlls=[os.add_dll_directory(str(qt/'bin')),os.add_dll_directory(str(ROOT/'build'/args.build))]
 os.environ['QT_PLUGIN_PATH']=str(qt/'plugins');os.environ['QT_QPA_PLATFORM']=args.platform
 lib=ctypes.CDLL(str(ROOT/'build'/args.build/'huelith_python_driver.dll'));fn=lib.huelith_python_command;fn.argtypes=[ctypes.c_char_p];fn.restype=ctypes.c_char_p
 commands=[];results=[]
 def call(op,**values):
  request=dict(op=op,**values);response=json.loads(fn(json.dumps(request,ensure_ascii=False).encode()));commands.append(dict(request=request,response=response))
  if 'error' in response:raise RuntimeError(response['error'])
  return response
 def key(k):return call('window_key',key=k)
 def action(text,**kw):return call('window_action',text=text,**kw)
 def probe(**kw):return call('window_probe',**kw)
 def state():return call('window_interaction_state')
 def scene(layers,**kw):return call('window_scene',layers=layers,**kw)
 def control(name,value):return call('window_control',name=name,value=value)
 def mouse(phase,**kw):return call('window_mouse',phase=phase,**kw)
 def click(x,y,**kw):mouse('press',x=x,y=y,**kw);mouse('release',x=x,y=y,**kw)
 def drag(x,y,ex,ey,modifiers=0):mouse('press',x=x,y=y,modifiers=modifiers);mouse('move',x=ex,y=ey,modifiers=modifiers);mouse('release',x=ex,y=ey,modifiers=modifiers)
 def layer(name):return next(x for x in probe()['layers'] if x['name']==name)
 def test(name,body):
  if args.only and args.only not in name:return
  print('RUN',name,flush=True)
  try:body();results.append(dict(name=name,passed=True))
  except Exception:results.append(dict(name=name,passed=False,error=traceback.format_exc()));print(results[-1]['error'],flush=True)
 with tempfile.TemporaryDirectory(prefix='huelith-m1-completion-') as tmp:
  folder=Path(tmp);call('init',settings=str(folder));call('window')
  transparent=Image.new('RGBA',(100,100));draw=ImageDraw.Draw(transparent);draw.ellipse((30,30,70,70),fill='red');shape=folder/'shape.png';transparent.save(shape)
  def hit():
   scene([dict(name='base',color='blue'),dict(name='shape',path=str(shape))]);click(10,10);assert layer('base')['id']==probe()['active']
   click(50,50);assert layer('shape')['id']==probe()['active']
   scene([dict(name='base',color='blue'),dict(name='group',kind=5,x=100,y=60,children=[dict(name='child',path=str(shape))])]);click(150,110);assert layer('child')['id']==probe()['active']
   scene([dict(name='base',color='blue'),dict(name='masked',path=str(shape),mask=0)]);click(50,50);assert layer('base')['id']==probe()['active']
  test('alpha_mask_and_nested_group_hit_testing',hit)
  def multi():
   scene([dict(name='a',color='red',width=60,height=40,x=40,y=40),dict(name='b',color='blue',width=60,height=40,x=200,y=100)])
   call('window_select',index=0);call('window_select',index=1,control=True);assert probe()['selected']==2
   old=state()['revision'];call('window_gesture',handle=4,scale=.5);assert probe()['pending'];key('Return')
   assert abs(layer('a')['sx']-.5)<1e-6 and abs(layer('b')['sx']-.5)<1e-6
   assert state()['revision']==old+1;key('Ctrl+Z');assert layer('a')['sx']==1 and layer('b')['sx']==1
   call('window_select',index=0);call('window_select',index=1,modifiers=SHIFT);assert probe()['selected']==2
  test('multi_layer_frame_shift_selection_one_transaction',multi)
  def flip():
   scene([dict(name='image',color='red',width=100,height=80,x=100,y=80)])
   call('window_gesture',handle=4,scaleX=-.5,scaleY=1,modifiers=SHIFT);key('Return');assert layer('image')['sx']<0 and layer('image')['sy']>0
   assert probe(x=80,y=100)['rgba'][3]>0;key('Ctrl+Z');assert layer('image')['sx']==1
  test('cross_zero_flip_preserves_source',flip)
  def distort():
   scene([dict(name='image',color='red',width=100,height=80,x=100,y=80)])
   a=mouse('press',handle=0,modifiers=CTRL);mouse('move',space='widget',x=a['x']+25,y=a['y']+12,modifiers=CTRL);mouse('release',space='widget',x=a['x']+25,y=a['y']+12,modifiers=CTRL)
   assert abs(layer('image')['m13'])+abs(layer('image')['m23'])>1e-8;key('Esc');assert layer('image')['m13']==0
   a=mouse('press',handle=0,modifiers=CTRL|ALT|SHIFT);mouse('move',space='widget',x=a['x']+25,y=a['y'],modifiers=CTRL|ALT|SHIFT);mouse('release',space='widget',x=a['x']+25,y=a['y'],modifiers=CTRL|ALT|SHIFT);key('Return');assert abs(layer('image')['m23'])>1e-8
  test('ctrl_distort_ctrl_alt_shift_perspective_cancel_commit',distort)
  def warp():
   scene([dict(name='image',path=str(shape),x=100,y=80)]);control('warpMode',True);a=mouse('press',warp=4);mouse('move',space='widget',x=a['x'],y=a['y']+25);mouse('release',space='widget',x=a['x'],y=a['y']+25)
   assert layer('image')['warped'] and layer('image')['warpCenterY']>.5;key('Return');assert layer('image')['warped']
   saved=folder/'warp.huelith';call('window_filename',path=str(saved));action('保存工程');deadline=time.monotonic()+8
   while not saved.exists() and time.monotonic()<deadline:call('pump')
   assert saved.exists();call('window_open',path=str(saved));assert layer('image')['warped'];control('warpMode',False)
  test('non_destructive_mesh_warp_save_reopen',warp)
  def mask():
   scene([dict(name='image',color='red',mask=255,linked=False)]);call('window_select',index=0,column=2);assert probe()['maskTarget']
   call('window_gesture',handle=4,scale=.5);key('Return');assert layer('image')['sx']==1 and abs(layer('image')['maskSx']-.5)<1e-6
   call('window_select',index=0,column=2,modifiers=SHIFT);assert not layer('image')['maskEnabled']
   call('window_select',index=0,column=2,modifiers=ALT);assert probe()['maskView']
   call('window_select',index=0);assert not probe()['maskTarget']
  test('mask_thumbnail_independent_transform_shift_alt',mask)
  def guides():
   scene([dict(name='image',color='red',width=40,height=40,x=30,y=30)])
   mouse('press',space='widget',x=200,y=10);mouse('move',x=100,y=90);mouse('release',x=100,y=90);assert abs(probe()['horizontal'][0]-90)<1e-6
   drag(150,90,150,120);assert abs(probe()['horizontal'][0]-120)<1e-6
   mouse('press',x=150,y=120);mouse('move',space='widget',x=200,y=5);mouse('release',space='widget',x=200,y=5);assert not probe()['horizontal']
   scene([dict(name='image',color='red',width=40,height=40,x=30,y=30)],vertical=[100]);drag(50,50,117,50);assert abs(layer('image')['tx']-100)<1e-6
   action('锁定参考线');drag(100,180,150,180);assert probe()['vertical']==[100];action('锁定参考线');action('显示参考线');assert not probe()['guidesVisible'];action('显示参考线');action('清除参考线');assert not probe()['vertical'];key('Ctrl+Z');assert probe()['vertical']==[100]
  test('rulers_guide_create_move_remove_and_snap',guides)
  def ants():
   scene([dict(name='image',color='white')]);key('M');drag(50,50,150,120);old=probe()['phase'];first=out/f'{args.build}-ants-a.png';second=out/f'{args.build}-ants-b.png';call('window_capture',path=str(first));deadline=time.monotonic()+1
   while probe()['phase']==old and time.monotonic()<deadline:call('pump')
   assert probe()['phase']!=old;call('window_capture',path=str(second));assert ImageChops.difference(Image.open(first).convert('RGB'),Image.open(second).convert('RGB')).getbbox()
  test('marching_ants_timer_actual_window_frames',ants)
  def ranges():
   scene([dict(name='image',color='white')]);key('M');drag(20,20,120,120);drag(70,70,170,170,SHIFT|ALT);assert probe(x=80,y=80)['range']>0 and probe(x=30,y=30)['range']==0
   action('变换选区…');call('window_gesture',handle=4,scale=.5);key('Return');assert probe(x=115,y=115)['range']==0;assert layer('image')['sx']==1
   key('Ctrl+D');key('M');mouse('press',x=100,y=100);mouse('move',x=160,y=130,modifiers=SHIFT);mouse('release',x=160,y=130,modifiers=SHIFT);assert probe(x=150,y=150)['range']>0
  test('selection_intersect_frame_and_shift_geometry',ranges)
  def pen():
   scene([dict(name='image',color='white')]);key('P');click(50,50);drag(150,50,170,80);click(150,150);key('Return');assert not probe()['pathClosed']
   action('隐藏当前路径');key('P');click(60,60);click(180,60);click(180,180);click(60,60);assert probe()['pathClosed']
   key('A');drag(180,60,200,70,CTRL);assert probe()['pathElements']>3
  test('pen_open_end_close_anchor_and_direct_selection',pen)
  def brush():
   scene([dict(name='paint',color='transparent')]);key('B');control('brushHardness',0);control('brushOpacity',50);control('brushFlow',100)
   before=state()['revision'];mouse('press',x=100,y=100);mouse('move',x=140,y=100);assert probe(x=120,y=100)['rgba'][3]>0 and state()['revision']==before;mouse('release',x=140,y=100);assert state()['revision']==before+1
   middle=probe(x=120,y=100)['rgba'][3];edge=probe(x=120,y=117)['rgba'][3];assert 0<edge<middle<=128,(middle,edge)
   key('E');control('brushOpacity',100);drag(100,100,140,100);assert probe(x=120,y=100)['rgba'][3]<middle
   key('Ctrl+Z');assert probe(x=120,y=100)['rgba'][3]==middle
  test('soft_brush_live_preview_opacity_edge_eraser_undo',brush)
  def clipboard():
   user32=ctypes.WinDLL('user32',use_last_error=True)
   if args.platform=='windows' and not user32.OpenClipboard(None):raise PermissionError(f'Windows OpenClipboard denied: {ctypes.get_last_error()}')
   user32.CloseClipboard()
   scene([dict(name='image',color='red',width=100,height=80,x=60,y=70)]);key('M');drag(80,90,120,130);key('Ctrl+X');assert probe(x=100,y=110)['rgba'][3]==0
   key('Ctrl+Shift+V');assert probe(x=100,y=110)['rgba'][3]>0;assert abs(probe()['layers'][-1]['tx']-80)<2
   key('M');drag(200,150,260,210);key('Ctrl+Alt+Shift+V');assert probe()['layers'][-1]['masked'] and not call('window_state')['selected']
  test('cut_in_place_and_paste_into_selection',clipboard)
  def placement():
   scene([dict(name='base',color='white')]);old=state()['revision'];call('window_open',path=str(shape),place=True);assert probe()['pending'] and len(probe()['layers'])==2;key('Esc');assert len(probe()['layers'])==1 and state()['revision']==old
   call('window_open',path=str(shape),place=True);key('Return');assert len(probe()['layers'])==2 and state()['revision']==old+1;key('Ctrl+Z');assert len(probe()['layers'])==1
  test('place_preview_cancel_and_commit_atomic',placement)
  def history():
   scene([dict(name='base',color='white')]);action('像素');action('像素');assert len(probe()['layers'])==3;control('historyList',0);assert len(probe()['layers'])==1;key('Ctrl+Shift+Z');assert len(probe()['layers'])==2
  test('history_row_navigation_and_redo',history)
  def align():
   scene([dict(name='a',color='red',width=50,height=30,x=40,y=30),dict(name='b',color='blue',width=70,height=50,x=210,y=170)]);call('window_select',index=0);call('window_select',index=1,control=True);action('右对齐');assert layer('a')['tx']==350 and layer('b')['tx']==330
   action('底对齐');assert layer('a')['ty']==270 and layer('b')['ty']==250;key('Ctrl+Z');assert layer('a')['ty']==30
  test('complete_alignment_different_sizes_and_undo',align)
  def export():
   scene([dict(name='image',path=str(shape))]);target=folder/'export16.png';action('导出 PNG / JPEG…',dialog={'accept':True,'exportDepth':1,'exportWidth':200,'exportHeight':150,'exportProfile':1,'path':str(target),'capture':str(out/f'{args.build}-export.png')});assert target.exists();image=Image.open(target);assert image.size==(200,150) and 'icc_profile' in image.info;assert target.read_bytes()[24]==16
  test('export_dialog_resize_16bit_png_icc',export)
  def shortcuts():
   assert action('设置菜单快捷键…',dialog={'conflict':True,'capture':str(out/f'{args.build}-shortcuts.png')})['conflictDetected'];shortcuts={x['text']:x['shortcut'] for x in state()['shortcuts']};assert shortcuts['复制']=='Ctrl+C' and shortcuts['自由变换']=='Ctrl+T'
  test('shortcut_dialog_conflict_cancel_preserves_bindings',shortcuts)
  def wand():
   sample=Image.new('RGB',(400,300),'black');d=ImageDraw.Draw(sample);d.rectangle((30,30,90,100),fill=(100,100,100));d.rectangle((90,30,130,100),fill=(115,115,115));d.rectangle((200,30,260,100),fill=(100,100,100));file=folder/'wand.png';sample.save(file)
   scene([dict(name='image',path=str(file))]);key('W');control('rangeAntialias',False);control('rangeTolerance',0);control('rangeContiguous',True);click(50,50);assert probe(x=50,y=50)['range']==255 and probe(x=210,y=50)['range']==0 and probe(x=110,y=50)['range']==0
   control('rangeTolerance',20);click(50,50);assert probe(x=110,y=50)['range']==255
   control('rangeContiguous',False);control('rangeAntialias',True);click(50,50);assert probe(x=210,y=50)['range']==255 and 0<probe(x=30,y=50)['range']<255
  test('wand_zero_tolerance_contiguous_color_threshold_antialias',wand)
  def navigation():
   scene([dict(name='image',color='red',width=40,height=40,x=30,y=30)]);key('Z');old=state()['zoom'];click(150,150);assert state()['zoom']>old;click(150,150,modifiers=ALT);assert abs(state()['zoom']-old)<1e-6
   key('V');action('网格');assert probe()['grid'];drag(50,50,118,50);assert abs(layer('image')['tx']-100)<1e-6;action('网格');action('标尺');assert not probe()['rulers'];action('标尺');assert probe()['rulers']
   call('window_capture',path=str(out/f'{args.build}-rulers.png'))
  test('zoom_tool_grid_snap_ruler_toggle',navigation)
  def pen_handles():
   scene([dict(name='image',color='white')]);key('P');click(50,60);drag(130,60,160,90);click(180,160);before=probe()['pathPoints'];drag(160,90,170,100,ALT);after=probe()['pathPoints'];assert before[2]==after[2] and before[4]!=after[4],(before,after)
  test('pen_alt_breaks_tangent_symmetry',pen_handles)
  def paint_targets():
   scene([dict(name='image',color='white',mask=255)]);call('window_select',index=0,column=2);key('B');key('D');control('brushHardness',100);control('brushOpacity',100);control('brushFlow',100);drag(90,90,120,90);assert probe(x=100,y=90)['rgba'][3]<10
   key('X');drag(90,90,120,90);assert probe(x=100,y=90)['rgba'][3]>245
   scene([dict(name='image',color='white')]);key('B');key('D');drag(90,90,120,90);assert probe(x=100,y=90)['rgba'][0]<10;key('Y');drag(90,90,120,90);assert probe(x=100,y=90)['rgba'][0]>245
   scene([dict(name='paint',color='transparent')]);key('B');control('brushPressure',True)
   for phase in ['press','release']:call('window_tablet',phase=phase,x=100,y=100,pressure=.4)
   assert probe(x=100,y=100)['rgba'][3]>0 and probe(x=114,y=100)['rgba'][3]==0
   for phase in ['press','release']:call('window_tablet',phase=phase,x=200,y=100,pressure=1)
   assert probe(x=214,y=100)['rgba'][3]>0
  test('mask_black_white_history_brush_and_synthetic_pressure',paint_targets)
  def tree():
   scene([dict(name='base',color='blue'),dict(name='shape',path=str(shape))]);call('window_select',index=0,offset=8);assert not layer('shape')['visible'];call('window_select',index=0,offset=8);assert layer('shape')['visible']
   call('window_select',index=0,offset=40,modifiers=CTRL);assert probe(x=50,y=50)['range']>0 and probe(x=10,y=10)['range']==0
   call('window_select',index=0,double=True,rename='renamed');assert layer('renamed')
   scene([dict(name='group',kind=5),dict(name='image',path=str(shape))]);call('window_select',index=0,drop=1);assert call('window_state')['treeRows']==1;key('Ctrl+Z');assert call('window_state')['treeRows']==2
  test('layer_eye_alpha_thumbnail_rename_and_drop_into_group',tree)
  def close_dialogs():
   scene([dict(name='image',color='white')]);action('像素');count=call('window_state')['count'];action('关闭图片',answer='cancel');assert call('window_state')['count']==count
   path=folder/'close-save.huelith';call('window_filename',path=str(path));action('关闭图片',answer='save');assert path.exists() and call('window_state')['count']==count-1
   call('window_new');action('像素');count=call('window_state')['count'];action('关闭图片',answer='discard');assert call('window_state')['count']==count-1
  test('document_close_save_discard_cancel',close_dialogs)
  def preference():
   scene([dict(name='image',color='white')]);action('保存工作区布局');old=call('window_layout');call('window_layout',hide=True);action('恢复已存工作区');assert call('window_layout')==old
   action('设置菜单快捷键…',dialog={'accept':True,'binding':{'name':'画笔 B','key':'F6'}});key('V');key('F6');assert state()['tool']==7;key('V');key('B');assert state()['tool']==0
   action('设置菜单快捷键…',dialog={'accept':True,'binding':{'name':'画笔 B','key':'B'}})
  test('workspace_restore_tool_shortcut_apply_old_key_removed',preference)
  def source_edit():
   scene([dict(name='source',kind=1,path=str(shape))]);old=state()['revision'];action('编辑嵌套合成…',answer='cancel');assert state()['revision']==old;action('编辑嵌套合成…',answer='accept');assert state()['revision']==old+1;key('Ctrl+Z');assert state()['revision']!=old+1
  test('source_editor_cancel_commit_single_undo',source_edit)
  def recovery():
   scene([dict(name='recovery fixture',color='white')]);saved=folder/'recovery-fixture.huelith';call('window_filename',path=str(saved));action('保存工程');deadline=time.monotonic()+8
   while not saved.exists() and time.monotonic()<deadline:call('pump')
   assert saved.exists();target=Path(call('window_recovery_file')['path']);assert target.suffix=='.huelith' and target.parent.name=='recovery' and not target.exists();target.parent.mkdir(parents=True,exist_ok=True)
   try:
    shutil.copy2(saved,target);before=call('window_state')['count'];action('恢复未保存文档…',dialog={'accept':False});assert target.exists() and call('window_state')['count']==before
    action('恢复未保存文档…',dialog={'accept':True});assert call('window_state')['count']==before+1 and call('window_state')['dirty'] and target.exists()
   finally:target.unlink(missing_ok=True)
  test('recovery_dialog_cancel_retains_open_dirty_copy',recovery)
  def legacy_schema():
   scene([dict(name='legacy layer',path=str(shape),x=70,y=60)]);current=folder/'schema3.huelith';call('window_filename',path=str(current));action('保存工程');deadline=time.monotonic()+8
   while not current.exists() and time.monotonic()<deadline:call('pump')
   with zipfile.ZipFile(current) as z:entries={name:z.read(name) for name in z.namelist()}
   # Strip later layer fields to construct a real schema-2 payload (one-layer fixture).
   grid=b'\0'+b''.join(struct.pack('<dd',x,y) for y in (0,.5,1) for x in (0,.5,1));payload=entries['document.bin'];assert payload.count(grid)==1
   version=struct.unpack_from('<I',payload)[0];start=payload.index(grid);end=start+len(grid)
   for _ in range((2 if version>=4 else 0)+(1 if version>=5 else 0)):
    size=struct.unpack_from('<I',payload,end)[0];end+=4
    assert size!=0xfffffffe,'Extended QByteArray length unexpected in small fixture'
    if size!=0xffffffff:end+=size
   if version>=6:end+=1 # Dedicated background flag; absent in legacy schema.
   assert end<=len(payload)
   entries['document.bin']=struct.pack('<I',2)+payload[4:start]+payload[end:];manifest=json.loads(entries['manifest.json']);manifest['schema']=2;entries['manifest.json']=json.dumps(manifest).encode();legacy=folder/'schema2.huelith'
   with zipfile.ZipFile(legacy,'w',compression=zipfile.ZIP_STORED) as z:
    for name,data in entries.items():z.writestr(name,data)
   call('window_open',path=str(legacy));assert layer('legacy layer')['tx']==70 and not layer('legacy layer')['warped'] and probe(x=120,y=110)['rgba'][3]>0
  test('schema2_layer_document_migrates_without_mesh',legacy_schema)
  def chrome():
   scene([dict(name='image',color='white')]);chrome=call('window_chrome');buttons=chrome['buttons'];assert len(buttons)>=14
   assert len({b['x'] for b in buttons})==2 and all(b['visible'] for b in buttons)
   for a in buttons:
    for b in buttons:
     if a['tool']!=b['tool']:assert a['x']+a['width']<=b['x'] or b['x']+b['width']<=a['x'] or a['y']+a['height']<=b['y'] or b['y']+b['height']<=a['y']
   for index in range(14):
    call('window_chrome',click=index);assert state()['tool']==index and state()['checkedTools']==1
   paths={m['text']:m['path'] for m in chrome['menus']}
   for text,segment in [('反相蒙版','/图层蒙版/'),('右对齐','/对齐/'),('水平分布','/分布/'),('羽化','/修改/'),('网格','/显示/'),('保存工作区布局','/工作区/'),('原位粘贴','/选择性粘贴/'),('数值变换…','/变换/'),('导出 PNG / JPEG…','/导出/')]:assert segment in paths[text],(text,paths.get(text))
   assert len(paths)==len(chrome['menus']), 'Duplicate menu command'
   assert paths['设置菜单快捷键…'].startswith('/编辑') and paths['恢复未保存文档…'].startswith('/文件')
   key('V');key('B');assert state()['tool']==7;action('像素');key('Ctrl+Z');assert len(probe()['layers'])==1
   (out/f'{args.build}-chrome.json').write_text(json.dumps(chrome,ensure_ascii=False,indent=2),encoding='utf-8')
  test('two_column_tools_click_shortcuts_and_menu_groups',chrome)
  def color_swatches():
   scene([dict(name='paint',color='transparent')]);before=state()['revision']
   call('window_color_button',name='foregroundSwatch',color='#e43b2c',capture=str(out/f'{args.build}-color-picker.png'));assert state()['foreground']=='#e43b2c'
   call('window_color_button',name='backgroundSwatch',color='#248fc6');assert state()['background']=='#248fc6'
   call('window_color_button',name='foregroundSwatch',color='#00ff00',accept=False);assert state()['foreground']=='#e43b2c' and state()['revision']==before
   key('B');control('brushHardness',100);control('brushOpacity',100);control('brushFlow',100);click(100,100);rgba=probe(x=100,y=100)['rgba'];assert all(abs(a-b)<=1 for a,b in zip(rgba,[228,59,44,255])),rgba
   call('window_color_button',name='swapColors');assert state()['foreground']=='#248fc6' and state()['background']=='#e43b2c';key('X');assert state()['foreground']=='#e43b2c'
   call('window_color_button',name='resetColors');assert state()['foreground']=='#000000' and state()['background']=='#ffffff'
   call('window_color_button',name='swapColors');key('D');assert state()['foreground']=='#000000' and state()['background']=='#ffffff'
  test('foreground_background_picker_cancel_swap_reset_and_actual_paint',color_swatches)
  def capture():
   product=Image.new('RGBA',(240,240));d=ImageDraw.Draw(product);d.rounded_rectangle((65,35,175,220),radius=16,fill='#d1a962');d.rectangle((82,15,158,46),fill='#35434f');d.rounded_rectangle((76,95,164,170),radius=5,fill='#e9dbb9');d.text((98,122),'HUELITH',fill='#35434f');asset=folder/'bottle.png';product.save(asset)
   scene([dict(name='背景',color='#34424c'),dict(name='产品 · 源图片',kind=1,path=str(asset),x=80,y=30,mask=255)]);key('V');call('window_capture',path=str(out/f'{args.build}-workspace.png'))
  test('compact_toolbar_layers_mask_visual_evidence',capture)
  call('shutdown')
 report=dict(build=args.build,platform=args.platform,passed=sum(x['passed'] for x in results),total=len(results),tests=results)
 (out/f'{args.build}-results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');(out/f'{args.build}-commands.json').write_text(json.dumps(commands,ensure_ascii=False,indent=2),encoding='utf-8')
 print(json.dumps(report,ensure_ascii=False,indent=2));return int(report['passed']!=report['total'])
if __name__=='__main__':raise SystemExit(main())
