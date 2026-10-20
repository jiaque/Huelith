"""Bounded, offscreen acceptance of the shared portrait workspace (A01)."""
import os
os.environ['QT_QPA_PLATFORM'] = 'offscreen'
from pathlib import Path
task_temp=Path.cwd()/'build'/'portrait-a01-temp'
task_temp.mkdir(exist_ok=True)
os.environ['TEMP']=os.environ['TMP']=str(task_temp)
os.environ['HUELITH_AI_WORKER_EXE']=str(Path.cwd()/'build/windows-release/Huelith.exe')
exec(Path('tools/m3_geometry_validate.py').read_text(encoding='utf-8').split('with tempfile.TemporaryDirectory() as tmp:')[0])

O = R / 'docs/test-evidence/M3/workspace-a01'
if os.environ.get('QT_SCALE_FACTOR'):O=O/('dpi-'+os.environ['QT_SCALE_FACTOR'])
O.mkdir(parents=True, exist_ok=True)
source = R / 'docs/test-evidence/M3/quality/portrait-highres-source.png'
recipe = json.loads((source.parent / 'portrait-highres-recipe.json').read_text())
report = {}
with tempfile.TemporaryDirectory(dir=R/'build', ignore_cleanup_errors=True) as tmp:
    checked(op='init', settings=tmp)
    try:
        entered=time.monotonic()
        checked(op='portrait_window', path=str(source), recipe=recipe,
                models=str(R/'build/windows-release/models'))
        report['openSeconds']=round(time.monotonic()-entered,3)
        # Measure responsiveness while preparation runs, before settling.
        pump_times=[]
        for _ in range(8):
            started=time.monotonic()
            checked(op='pump',waitMs=16)
            pump_times.append(time.monotonic()-started)
        report['initialPumpMaxSeconds']=round(max(pump_times),3)
        assert max(pump_times)<1, 'initial preparation blocked event processing'
        settle()
        checked(op='portrait_state', select=0, resize=[1600,1000], workspaceTask=3)
        checked(op='pump', waitMs=100)
        initial = checked(op='portrait_state', workspaceLayout=True)
        assert len(initial['workspaceTasks']) == 15
        assert initial['workspaceLayout']['portraitTasks'][2] == 224
        assert initial['workspaceLayout']['portraitInspector'][2] == 448
        assert initial['workspaceLayout']['portraitBottomBar'][3] == 52
        report['layout'] = initial['workspaceLayout']
        checked(op='portrait_state', capture=str(O/'face-frame.png'))
        checked(op='portrait_state', parameter='face.width', value=15)
        before = checked(op='portrait_state')['recipe']
        # Cover every navigation destination; no changes to the saved recipe.
        for task in range(15):
            checked(op='portrait_state', workspaceTask=task)
            checked(op='pump', waitMs=40)
            state = checked(op='portrait_state', workspaceLayout=True)
            assert state['workspaceTask'] == task, (task, state['workspaceTask'])
            assert state['workspaceLayout']['portraitView'] == initial['workspaceLayout']['portraitView'], task
            if task in (4,9,13):checked(op='portrait_state',capture=str(O/('frame-task-'+str(task)+'.png')))
            # Background analysis may add derived masks. User parameters must survive.
            assert state['recipe']['people'][0]['settings']['parameters'] == before['people'][0]['settings']['parameters'], task
        # Mix geometry, a potentially unavailable body part and manual liquify.
        checked(op='portrait_state', workspaceTask=8, parameter='body.waist', value=20)
        checked(op='portrait_state', workspaceTask=9, liquify=True)
        checked(op='portrait_state', workspaceTask=3)
        drag = checked(op='portrait_state', dragParameter='face.width',
                       dragValues=[10,15,20,25,20,15,10], stepMs=80)
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            checked(op='pump', waitMs=50)
            state = checked(op='portrait_state')
            if not state['previewBusy'] and state['ready']:
                break
        assert state['ready'] and not state['previewBusy'], 'preview/analysis did not settle'
        frames=drag['dragFrames']
        assert len({frame['frames'] for frame in frames})>1, 'no live updates during drag'
        assert len({tuple(frame['viewport']) for frame in frames})==1, 'viewport moved during drag'
        report['dragPreviewUpdates']=len({frame['frames'] for frame in frames})
        final = checked(op='portrait_state', workspaceLayout=True)
        assert final['workspaceLayout']['portraitView'] == initial['workspaceLayout']['portraitView']
        assert 'portraitRepairDelete' not in final['visibleControls'], 'repair controls leaked into face page'
        report['previewFrames'] = final['previewFrames']
        checked(op='portrait_state', capture=str(O/'after-drag.png'))
        for width in [1280,1600]:
            checked(op='portrait_state', resize=[width,1000])
            checked(op='pump', waitMs=50)
            state=checked(op='portrait_state',workspaceLayout=True)
            assert state['workspaceLayout']['portraitInspector'][2] == (400 if width==1280 else 448)
        report['roundtrip'] = checked(op='portrait_state', source=str(source),save=str(O/'workspace.huelith'))['roundtrip']
        assert report['roundtrip']
        # Saved non-face scope: hand tools must not require face landmarks.
        checked(op='portrait_close')
        scope=json.loads(json.dumps(recipe))
        scope['people']=scope['people'][:1]
        person=scope['people'][0]
        person['face']=[0,0,1,1]
        person['landmarks']=[]
        person['bodyMask']=''
        person['settings']={'partialSkin':True,'imageScope':True,'protectBackground':False}
        checked(op='portrait_window',path=str(source),recipe=scope)
        settle()
        checked(op='portrait_state',workspaceTask=9,liquify=True)
        deadline=time.monotonic()+60
        while time.monotonic()<deadline:
            checked(op='pump',waitMs=40)
            state=checked(op='portrait_state')
            if not state['previewBusy']:break
        assert state['displayedRevision']==state['previewRevision'], state['status']
        assert state['recipe']['people'][0]['settings'].get('liquify')
        rendered=checked(op='portrait_state',source=str(source),render=str(O/'image-scope-liquify.png'))
        assert not rendered['identity'], 'non-face liquify was ignored'
        report['nonFaceLiquify']=True
        checked(op='portrait_state',workspaceTask=10,paintPath=[[.45,.45],[.46,.45],[.47,.45]])
        state=checked(op='portrait_state')
        assert state['recipe']['people'][0]['settings'].get('repairs'), state['status']
        report['nonFaceRepair']=True
        report['result'] = 'passed' 
    finally:
        (O/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        checked(op='portrait_close')
        checked(op='shutdown')
print(json.dumps(report,ensure_ascii=False))
