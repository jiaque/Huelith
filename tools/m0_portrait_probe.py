"""M0 face parsing/landmark and manual repair feasibility, not product effects."""
import argparse
import hashlib
import json
from pathlib import Path
import time

import cv2
import mediapipe as mp
import numpy as np
import onnxruntime as ort
from skimage import data
from PIL import Image


def save(path, image):
    Image.fromarray(image).save(path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(); args.output.mkdir(parents=True, exist_ok=True)
    # Public-domain NASA photograph distributed by scikit-image, no face identity processing.
    original = data.astronaut()
    save(args.output/'source.png', original)
    landmark_path = args.assets/'face_landmarker.task'
    parsing_path = args.assets/'face-parsing-resnet18.onnx'
    base = mp.tasks.BaseOptions(model_asset_path=str(landmark_path))
    options = mp.tasks.vision.FaceLandmarkerOptions(base_options=base, num_faces=4)
    ort_options = ort.SessionOptions(); ort_options.intra_op_num_threads=4
    parsing = ort.InferenceSession(str(parsing_path), ort_options, providers=['CPUExecutionProvider'])
    report = dict(sample='scikit-image astronaut / NASA public domain',
        sample_pixel_sha256=hashlib.sha256(original.tobytes()).hexdigest(),
        mediapipe=mp.__version__, opencv=cv2.__version__,
        landmark_sha256=hashlib.sha256(landmark_path.read_bytes()).hexdigest(),
        parsing_sha256=hashlib.sha256(parsing_path.read_bytes()).hexdigest(), variants=[])
    variants = {'original':original, 'dark':(original.astype(np.float32)*0.25).astype(np.uint8),
                'two_faces':np.concatenate([original, original[:,::-1]],axis=1)}
    face_crop=cv2.resize(original[25:220,140:320],(360,390))
    variants['two_large_faces']=np.concatenate([face_crop,face_crop[:,::-1]],axis=1)
    with mp.tasks.vision.FaceLandmarker.create_from_options(options) as detector:
        for name,image in variants.items():
            start=time.perf_counter()
            result=detector.detect(mp.Image(image_format=mp.ImageFormat.SRGB,data=np.ascontiguousarray(image)))
            record=dict(name=name, faces=len(result.face_landmarks), landmark_counts=[len(f) for f in result.face_landmarks],
                        landmark_seconds=time.perf_counter()-start)
            overlay=image.copy()
            for face in result.face_landmarks:
                for point in face:
                    cv2.circle(overlay,(round(point.x*image.shape[1]),round(point.y*image.shape[0])),1,(30,255,80),-1)
            save(args.output/(name+'-landmarks.png'),overlay)
            report['variants'].append(record); print(record,flush=True)
    # Crop is specified for this known sample only; automatic multi-person ROI association remains a separate requirement.
    face=original[45:185,165:285].copy()
    resized=cv2.resize(face,(512,512)).astype(np.float32)/255
    tensor=((resized-np.array([.485,.456,.406],np.float32))/np.array([.229,.224,.225],np.float32)).transpose(2,0,1)[None]
    start=time.perf_counter(); logits=parsing.run(None,{parsing.get_inputs()[0].name:np.ascontiguousarray(tensor)})[0]
    labels=cv2.resize(logits[0].argmax(axis=0).astype(np.uint8),(face.shape[1],face.shape[0]),interpolation=cv2.INTER_NEAREST)
    skin=np.isin(labels,[1,10]); overlay=face.copy(); overlay[skin]=(0.6*face[skin]+0.4*np.array([255,40,40])).astype(np.uint8)
    save(args.output/'skin-overlay.png',overlay); save(args.output/'skin-mask.png',skin.astype(np.uint8)*255)
    report['parsing']=dict(seconds=time.perf_counter()-start, class_count=int(logits.shape[1]),
        present_labels=np.unique(labels).tolist(), skin_pixels=int(skin.sum()),included_skin_labels=[1,10],
        limitations='19-class parser does not provide dedicated teeth, blemish or wrinkle classes; not accepted as those detectors.')
    # Controlled defect experiment with a clean reference. Float32 per-channel NS inpainting.
    clean=face.astype(np.float32)/255
    clean=np.where(clean<=0.04045,clean/12.92,((clean+0.055)/1.055)**2.4).astype(np.float32)
    mask=np.zeros(face.shape[:2],np.uint8)
    cv2.line(mask,(65,42),(83,45),255,2)
    damaged=clean.copy(); damaged[mask>0]*=0.45
    repaired=np.stack([cv2.inpaint(damaged[:,:,c],mask,3,cv2.INPAINT_NS) for c in range(3)],axis=-1)
    repaired[mask==0]=damaged[mask==0]
    empty=np.zeros_like(mask)
    neutral=np.stack([cv2.inpaint(clean[:,:,c],empty,3,cv2.INPAINT_NS) for c in range(3)],axis=-1)
    before=float(np.mean((damaged[mask>0]-clean[mask>0])**2)); after=float(np.mean((repaired[mask>0]-clean[mask>0])**2))
    report['manual_repair']=dict(before_mse=before,after_mse=after,improves_controlled_defect=after<before,
        outside_unchanged=bool(np.array_equal(repaired[mask==0],damaged[mask==0])),empty_neutral=bool(np.array_equal(neutral,clean)),
        limitations='Small synthetic scratch only; does not establish real wrinkle texture quality or automatic blemish detection.')
    for name,img in [('defect',damaged),('repair',repaired)]:
        srgb=np.where(img<=.0031308,img*12.92,1.055*np.maximum(img,0)**(1/2.4)-.055)
        save(args.output/(name+'.png'),np.uint8(np.clip(srgb,0,1)*255+0.5))
    # Protected warp: editable face mask moves, background is bit-identical.
    yy,xx=np.indices(face.shape[:2],dtype=np.float32)
    weight=np.exp(-((xx-60)**2+(yy-80)**2)/(2*20**2)).astype(np.float32)*skin
    warped=cv2.remap(face,xx+3*weight,yy,cv2.INTER_LINEAR,borderMode=cv2.BORDER_REFLECT_101)
    warped[~skin]=face[~skin]
    save(args.output/'protected-warp.png',warped)
    report['geometry']=dict(background_unchanged=bool(np.array_equal(warped[~skin],face[~skin])),max_shift_pixels=3,
        limitations='Small known-face warp only; large deformations, occlusions and body boundaries unvalidated.')
    (args.output/'portrait-results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report['manual_repair']),flush=True)


if __name__=='__main__': main()
