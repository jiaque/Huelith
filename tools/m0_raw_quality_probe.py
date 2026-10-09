"""Same-display-transform RAW comparison; no clean-ground-truth quality claim."""
import argparse
import json
from pathlib import Path

import numpy as np
import rawpy
from PIL import Image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--raw', type=Path, required=True)
    parser.add_argument('--denoised', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    xyz_to_rec = np.array([[1.716651188, -.355670784, -.253366281],
                           [-.666684352, 1.616481237, .015768546],
                           [.017639857, -.042770613, .942103121]])
    xyz_to_srgb = np.array([[3.2404542, -1.5371385, -.4985314],
                            [-.969266, 1.8760108, .041556],
                            [.0556434, -.2040259, 1.0572252]])
    with rawpy.imread(str(args.raw)) as raw:
        matrix = np.asarray(raw.rgb_xyz_matrix[:3, :], np.float64)
        wb = np.asarray(raw.camera_whitebalance[:3], np.float64)
        wb /= wb[1]
        baseline = raw.postprocess(output_color=rawpy.ColorSpace.raw,
            output_bps=16, gamma=(1, 1), no_auto_bright=True,
            use_camera_wb=False, use_auto_wb=False, user_wb=[1, 1, 1, 1],
            user_flip=0, median_filter_passes=0,
            fbdd_noise_reduction=rawpy.FBDDNoiseReductionMode.Off).astype(np.float32)/65535
    denoised_rec = np.load(args.denoised)
    denoised_cam = np.einsum('...c,dc->...d', denoised_rec,
        (matrix @ np.linalg.inv(xyz_to_rec)).astype(np.float32))
    if denoised_cam.shape != baseline.shape:
        raise ValueError('orientation/size mismatch')
    # A single image-wide scalar compensates differing LibRaw/model normalization.
    # Never normalize individual channels or individual crops independently.
    gain = float(np.mean(baseline, dtype=np.float64) / np.mean(denoised_cam, dtype=np.float64))
    denoised_cam *= gain
    # LibRaw/dcraw normalizes rows of XYZ->camera * sRGB->XYZ before
    # inversion. Omitting this while applying camera WB double-counts
    # the matrix's illuminant scaling and causes visible color casts.
    srgb_to_camera = matrix @ np.linalg.inv(xyz_to_srgb)
    srgb_to_camera /= srgb_to_camera.sum(axis=1, keepdims=True)
    camera_to_srgb = np.linalg.inv(srgb_to_camera)
    neutral_error = float(np.max(np.abs(camera_to_srgb @ np.ones(3) - 1)))
    if neutral_error > 1e-10:
        raise ValueError('normalized camera matrix fails neutral-axis invariant')
    transform = (camera_to_srgb @ np.diag(wb)).astype(np.float32)
    before = np.einsum('...c,dc->...d', baseline, transform)
    after = np.einsum('...c,dc->...d', denoised_cam, transform)
    exposure = .6 / max(float(np.quantile(np.maximum(before, 0), .99)), 1e-6)

    def render(rgb):
        mapped = np.maximum(rgb, 0) * exposure
        mapped /= 1 + mapped
        encoded = np.where(mapped <= .0031308, mapped*12.92,
                           1.055*mapped**(1/2.4)-.055)
        return np.uint8(np.clip(encoded, 0, 1)*255+.5)

    left, right = render(before), render(after)
    h, w = left.shape[:2]
    Image.fromarray(np.concatenate([left, right], axis=1)).resize((1800, 600)).save(
        args.output/'comparison.jpg', quality=95)
    crops = []
    for name, cy, cx in [('center', h//2, w//2), ('upper-left', h//4, w//4)]:
        a = left[cy-256:cy+256, cx-256:cx+256]
        b = right[cy-256:cy+256, cx-256:cx+256]
        Image.fromarray(np.concatenate([a, b], axis=1)).save(args.output/(name+'-100percent.png'))
        crops.append(dict(name=name, bounds=[cx-256, cy-256, 512, 512]))
    report = dict(raw=args.raw.name, normalization_gain=gain,
        shared_exposure=exposure, camera_wb=wb.tolist(), crops=crops,
        normalized_matrix_neutral_error=neutral_error,
        camera_to_linear_srgb=camera_to_srgb.tolist(),
        layout='left: LibRaw AHD without denoising; right: UtNet2 joint denoise/demosaic',
        limitations=['Identical candidate color/display transform, not calibrated camera color.',
            'LibRaw demosaic and model joint demosaic differ; not a denoising-only isolation.',
            'No clean reference. Do not infer PSNR, skin/detail fidelity or universal quality.'])
    (args.output/'comparison.json').write_text(json.dumps(report, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
