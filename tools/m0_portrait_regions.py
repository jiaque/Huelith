"""M0 candidate-region failure probes; never automatic product retouching."""
import argparse
import json
from pathlib import Path

import cv2
import mediapipe as mp
import numpy as np
from PIL import Image
from skimage import data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    source = data.astronaut()
    options = mp.tasks.vision.FaceLandmarkerOptions(
        base_options=mp.tasks.BaseOptions(model_asset_path=str(args.assets/'face_landmarker.task')))
    with mp.tasks.vision.FaceLandmarker.create_from_options(options) as detector:
        faces = detector.detect(mp.Image(image_format=mp.ImageFormat.SRGB, data=source))
    if len(faces.face_landmarks) != 1:
        raise ValueError('known reference face not detected')
    edges = mp.tasks.vision.FaceLandmarksConnections.FACE_LANDMARKS_LIPS
    # Official connection ordering has the inner lip after the first 20 edges.
    # Derive its connected component from inner-lip seed 78, instead of using
    # the outer-lip hull (which would also include the lips themselves).
    indices = {78}
    while True:
        expanded = indices | {i for e in edges if e.start in indices or e.end in indices
                              for i in (e.start, e.end)}
        if expanded == indices:
            break
        indices = expanded
    points = np.array([[round(faces.face_landmarks[0][i].x*source.shape[1]),
                        round(faces.face_landmarks[0][i].y*source.shape[0])]
                       for i in sorted(indices)], np.int32)
    mask = np.zeros(source.shape[:2], np.uint8)
    cv2.fillConvexPoly(mask, cv2.convexHull(points), 255)
    x, y, w, h = cv2.boundingRect(points)
    crops, variants = [], []
    original_candidate = None
    for name, gain in [('original', 1.), ('dark', .25)]:
        image = np.uint8(source.astype(np.float32)*gain)
        hsv = cv2.cvtColor(image, cv2.COLOR_RGB2HSV)
        # Deliberately simple existing-library candidate, not an accepted detector.
        candidate = (mask > 0) & (hsv[:, :, 1] < 110) & (hsv[:, :, 2] > 140)
        overlay = image.copy()
        overlay[candidate] = np.uint8(.5*image[candidate]+.5*np.array([20,255,80]))
        bounds = (slice(max(0,y-8),y+h+8), slice(max(0,x-8),x+w+8))
        crops.append(np.concatenate([image[bounds], overlay[bounds]], axis=1))
        if original_candidate is None:
            original_candidate = candidate
        union = np.count_nonzero(candidate | original_candidate)
        overlap = np.count_nonzero(candidate & original_candidate)/max(union, 1)
        variants.append(dict(name=name, candidate_pixels=int(candidate.sum()),
                             iou_with_original=overlap))
    Image.fromarray(np.concatenate(crops, axis=0)).resize((800,480), Image.Resampling.NEAREST).save(
        args.output/'teeth-candidates.png')
    # Equal-looking spots can have different user intent: remove blemish, preserve freckle.
    patch = np.full((96,192,3), [190,145,120], np.uint8)
    for cx in [48,144]:
        cv2.circle(patch, (cx,48), 3, (105,75,60), -1)
    gray = cv2.cvtColor(patch, cv2.COLOR_RGB2GRAY)
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (11,11))
    candidate = cv2.morphologyEx(gray, cv2.MORPH_BLACKHAT, kernel) > 20
    overlay = patch.copy()
    overlay[candidate] = [255,30,30]
    Image.fromarray(np.concatenate([patch,overlay], axis=0)).resize((768,768), Image.Resampling.NEAREST).save(
        args.output/'blemish-preservation-counterexample.png')
    report = dict(sample='NASA astronaut, plus explicitly synthetic spots',
        teeth=variants, teeth_inner_lip_points=len(indices),
        blemish=dict(removable_spot_selected=bool(candidate[48,48]),
                     protected_freckle_also_selected=bool(candidate[48,144])),
        decision='Reject fixed HSV teeth threshold and dark-spot detector as automatic defaults.',
        limitations=['No labeled tooth/gum ground truth; candidate counts are not accuracy.',
            'Darkness probe reuses geometry to isolate the photometric threshold failure.',
            'Synthetic equal-looking spots demonstrate intent ambiguity, not real acne detection accuracy.',
            'Require visible editable masks and explicit preservation; real annotated cases still pending.'])
    (args.output/'region-results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report), flush=True)


if __name__ == '__main__':
    main()
