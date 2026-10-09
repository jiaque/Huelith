"""Versioned M0 camera/display transforms, shared by the RAW research probes."""
import numpy as np

XYZ_TO_SRGB = np.array([[3.2404542,-1.5371385,-.4985314],
                        [-.969266,1.8760108,.041556],
                        [.0556434,-.2040259,1.0572252]], dtype=np.float64)
XYZ_TO_REC2020 = np.array([[1.716651188,-.355670784,-.253366281],
                           [-.666684352,1.616481237,.015768546],
                           [.017639857,-.042770613,.942103121]], dtype=np.float64)
PIPELINE = 'm0-normalized-camera-wb-v2'


def camera_to_srgb(matrix, white_balance):
    matrix = np.asarray(matrix, np.float64)
    wb = np.asarray(white_balance[:3], np.float64)
    if matrix.shape != (3,3) or not np.isfinite(matrix).all() or not np.isfinite(wb).all() or np.any(wb <= 0):
        raise ValueError('invalid camera transform')
    srgb_to_camera = matrix @ np.linalg.inv(XYZ_TO_SRGB)
    sums = srgb_to_camera.sum(axis=1, keepdims=True)
    if np.any(np.abs(sums) < 1e-10):
        raise ValueError('singular camera normalization')
    normalized = np.linalg.inv(srgb_to_camera / sums)
    if np.max(np.abs(normalized @ np.ones(3)-1)) > 1e-10:
        raise ValueError('neutral-axis mismatch')
    return normalized @ np.diag(wb / wb[1])


def camera_to_rec2020(matrix, white_balance):
    return XYZ_TO_REC2020 @ np.linalg.inv(XYZ_TO_SRGB) @ camera_to_srgb(matrix, white_balance)
