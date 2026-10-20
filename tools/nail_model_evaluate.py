"""Local, non-shipping evaluation of the pinned nail ONNX candidate.

Only image/model files are read; no photo upload or third-party app is involved.
Candidate provenance and hashes are written with every result.
"""
from pathlib import Path
import sys,json,time,hashlib
ROOT=Path(__file__).resolve().parents[1]
DATA=ROOT/'build/nail-evaluation'
sys.path.insert(0,str(DATA/'python'))
import numpy as np
import cv2
import onnxruntime as ort

EXPECTED='3d235feeef3e4d297ff1e5fc8617ef1eea4ddaaa86cb591c87065a8eed02ed3a'

def detect(session,image):
    height,width=image.shape[:2];edge=int(session.get_inputs()[0].shape[2]);scale=min(edge/width,edge/height)
    rw,rh=round(width*scale),round(height*scale);left,top=(edge-rw)//2,(edge-rh)//2
    canvas=np.full((edge,edge,3),114,np.uint8);canvas[top:top+rh,left:left+rw]=cv2.resize(image,(rw,rh))
    tensor=canvas[:,:,::-1].transpose(2,0,1)[None].astype(np.float32)/255
    start=time.perf_counter();raw,proto=session.run(None,{session.get_inputs()[0].name:tensor});elapsed=(time.perf_counter()-start)*1000
    prediction=raw[0].T;proto=proto[0];channels,mh,mw=proto.shape;classes=prediction.shape[1]-4-channels
    if classes!=1:raise ValueError(f'Expected one nail class, got {classes}')
    prediction=prediction[prediction[:,4]>.35];boxes=prediction[:,:4].copy();boxes[:,:2]-=boxes[:,2:]/2
    indices=np.asarray(cv2.dnn.NMSBoxes(boxes.tolist(),prediction[:,4].tolist(),.35,.45)).reshape(-1)
    records=[]
    for index in indices[:30]:
        x,y,w,h=boxes[index];logits=prediction[index,5:]@proto.reshape(channels,-1)
        probability=1/(1+np.exp(-np.clip(logits,-30,30)));probability=probability.reshape(mh,mw)
        yy,xx=np.mgrid[:mh,:mw];probability*=((xx>=x*mw/edge)&(xx<(x+w)*mw/edge)&(yy>=y*mh/edge)&(yy<(y+h)*mh/edge))
        probability=cv2.resize(probability,(edge,edge))[top:top+rh,left:left+rw];probability=cv2.resize(probability,(width,height))
        mask=np.uint8(probability>.5)*255
        count,labels,stats,_=cv2.connectedComponentsWithStats(mask)
        if count<2:continue
        label=1+np.argmax(stats[1:,cv2.CC_STAT_AREA]);mask=np.uint8(labels==label)*255
        contours,_=cv2.findContours(mask,cv2.RETR_EXTERNAL,cv2.CHAIN_APPROX_SIMPLE);contour=max(contours,key=cv2.contourArea)
        if cv2.contourArea(contour)<8:continue
        polygon=cv2.approxPolyDP(contour,1.2,True)[:,0,:].tolist()
        records.append({'confidence':float(prediction[index,4]),'pixels':int(np.count_nonzero(mask)),'polygon':polygon,'mask':mask})
    return records,elapsed

def main():
    model=DATA/'nail-seg.onnx';digest=hashlib.sha256(model.read_bytes()).hexdigest()
    if digest!=EXPECTED:raise ValueError('Unexpected candidate model hash')
    options=ort.SessionOptions();options.intra_op_num_threads=4
    session=ort.InferenceSession(str(model),options,providers=['CPUExecutionProvider'])
    output=ROOT/'docs/test-evidence/M3/nail-model-evaluation';output.mkdir(parents=True,exist_ok=True)
    sources=[DATA/'hand-1.jpg',DATA/'hand-2.jpg',ROOT/'docs/test-evidence/M3/quality/leroy-chiao-source.png']
    report={'modelSha256':digest,'source':'https://github.com/austingg/finger-nail-seg/tree/2cb37309fdea69de4fcc25cbbe1a1baf5e4b5d25','runtime':ort.__version__,'metadata':session.get_modelmeta().custom_metadata_map,'cases':[]}
    for source in sources:
        image=cv2.imread(str(source));records,elapsed=detect(session,image);overlay=image.copy();case={'file':source.name,'sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'inferenceMs':elapsed,'instances':[]}
        for i,record in enumerate(records):
            mask=record.pop('mask');cv2.imwrite(str(output/f'{source.stem}-{i+1}-mask.png'),mask)
            color=np.array([(53*i+40)%255,(89*i+180)%255,(137*i+100)%255]);active=mask>0;overlay[active]=overlay[active]*.55+color*.45
            cv2.polylines(overlay,[np.array(record['polygon'],np.int32)],True,(0,255,255),1);case['instances'].append(record)
        cv2.imwrite(str(output/f'{source.stem}-overlay.jpg'),overlay);report['cases'].append(case)
        print(source.name,'instances',len(records),'inference_ms',round(elapsed,1),flush=True)
    (output/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
if __name__=='__main__':main()
