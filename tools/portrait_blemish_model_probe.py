"""Research-only independent detection head; never runs retouch/inpainting networks."""
import ast,hashlib,json,time,typing,sys
from pathlib import Path
import cv2,numpy as np,torch
from torch import nn
from torch.nn import functional as F
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
ASSETS=ROOT/'research/m3/blemish-model-evaluation'
OUT=ROOT/'docs/test-evidence/M3/blemish-model';OUT.mkdir(parents=True,exist_ok=True)
# Extract inspected classes directly from Apache-2.0 ModelScope v1.9.5.
# Initialization is disabled because every parameter comes from the checkpoint.
nodes=[]
for filename,name in [('modelscope_detection_module.py','ConvBNActiv'),('modelscope_detection_unet_in.py','DetectionUNet')]:
    tree=ast.parse((ASSETS/filename).read_text(encoding='utf-8'))
    nodes.extend(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name==name)
assert len(nodes)==2
scope={'torch':torch,'nn':nn,'F':F,'Optional':typing.Optional}
exec(compile(ast.Module(body=nodes,type_ignores=[]),'inspected_detection_definitions','exec'),scope)
torch.set_num_threads(4)
weights=ASSETS/'joint_20210926.pth'
checkpoint=torch.load(weights,map_location='cpu',weights_only=True)
model=scope['DetectionUNet'](3,1,init_weights=False).eval();model.load_state_dict(checkpoint['detection_net'],strict=True)
del checkpoint
if '--export' in sys.argv:
    sys.path.insert(0,str(ASSETS/'runtime'))
    import onnx
    output=ASSETS/'blemish_detection_modelscope.onnx'
    probe=torch.linspace(-1,1,768*768*3).reshape(1,3,768,768)
    torch.onnx.export(model,probe,str(output),opset_version=17,input_names=['image'],output_names=['logits'],dynamo=False)
    onnx.checker.check_model(str(output))
    with torch.inference_mode():expected=model(probe).numpy()
    net=cv2.dnn.readNetFromONNX(str(output));net.setInput(probe.numpy());actual=net.forward()
    error=float(np.abs(expected-actual).max());assert error<.001,error
    previous=cv2.dnn.readNetFromONNX(str(ASSETS/'blemish_detection.onnx'));previous.setInput(probe.numpy())
    previous_error=float(np.abs(previous.forward()-actual).max());assert previous_error<.0001,previous_error
    (OUT/'export.json').write_text(json.dumps(dict(path=str(output),sha256=hashlib.sha256(output.read_bytes()).hexdigest(),input=[1,3,768,768],inputRange=[-1,1],inputColor='RGB',output='logits; sigmoid required',maximumLogitError=error,containsInpainting=False),indent=2))
    print('Detection-only ONNX exported; maximum logit error',error,'previous export error',previous_error,'sha256',hashlib.sha256(output.read_bytes()).hexdigest())
    sys.exit(0)
records=[]
for name in ['acne-face','jessica-watkins','john-glenn']:
    src=ROOT/f'docs/test-evidence/M3/quality/{name}-source.png'
    rgb=np.asarray(Image.open(src).convert('RGB'));height,width=rgb.shape[:2]
    recipe=json.loads((src.parent/f'{name}-recipe.json').read_text());p=recipe['people'][0]
    x,y,w,h=p['face'];x0=max(0,int((x-w*.2)*width));x1=min(width,int((x+w*1.2)*width));y0=max(0,int((y-h*.2)*height));y1=min(height,int((y+h*1.2)*height))
    crop=rgb[y0:y1,x0:x1];resized=cv2.resize(crop,(768,768),interpolation=cv2.INTER_LINEAR)
    tensor=torch.from_numpy((resized.astype(np.float32)/127.5-1).transpose(2,0,1)[None].copy())
    start=time.monotonic()
    with torch.inference_mode():prob=torch.sigmoid(model(tensor))[0,0].numpy()
    elapsed=time.monotonic()-start
    full=np.zeros((height,width),np.float32);full[y0:y1,x0:x1]=cv2.resize(prob,(x1-x0,y1-y0),interpolation=cv2.INTER_NEAREST)
    np.save(OUT/f'{name}-probability.npy',full);Image.fromarray(np.uint8(full*255)).save(OUT/f'{name}-mask.png')
    overlay=rgb.copy().astype(float);mask=full>=.35;overlay[mask]=overlay[mask]*.5+[127.5,0,0]
    Image.fromarray(np.uint8(overlay)).save(OUT/f'{name}-overlay.png')
    record=dict(name=name,crop=[x0,y0,x1,y1],seconds=elapsed,areaFraction=float(mask.mean()),maximum=float(full.max()))
    if name=='acne-face':
        labels=json.loads((ROOT/'docs/test-evidence/M3/semantic-blemish-categories/visual-regression-regions.json').read_text())
        yy,xx=np.mgrid[:height,:width];regions=[]
        for kind in ['targets','protected']:
            for r in labels[kind]:
                region=((xx/width-r['x'])**2+(yy/height-r['y'])**2)<=r['radius']**2
                regions.append(dict(id=r['id'],kind=kind,maximum=float(full[region].max()),fractionAbove035=float(mask[region].mean())))
        record['reviewedRegions']=regions
    records.append(record);print(record,flush=True)
(OUT/'results.json').write_text(json.dumps(dict(weightSha256=hashlib.sha256(weights.read_bytes()).hexdigest(),head='detection_net only',threshold=.35,runs=records),indent=2))
