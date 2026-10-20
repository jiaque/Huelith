"""Reproducible conversion of the official Torchvision instance model, not a test EXE.

Requires the verified local Torchvision wheel and ONNX converter dependencies.
The deployed graph returns box-local soft masks to avoid N full-size image tensors.
"""
import hashlib
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'research/m2/instances'
sys.path[:0] = [str(ASSETS / 'runtime'), str(ROOT / 'build/semantic-python')]
import numpy as np
import torch
import torchvision
import onnx
import onnxruntime as ort
from PIL import Image


class InstanceGraph(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.model = torchvision.models.detection.maskrcnn_resnet50_fpn_v2(
            weights=None, weights_backbone=None, min_size=800, max_size=800,
            box_score_thresh=0.5, box_detections_per_img=50)
        weights = ASSETS / 'weights.pth'
        assert hashlib.sha256(weights.read_bytes()).hexdigest().startswith('73cbd019')
        self.model.load_state_dict(torch.load(weights, map_location='cpu', weights_only=True))
        # Boxes already use the fixed 800x800 model coordinate space. Keep the
        # original 28x28 probabilities; the C++ caller maps them back to the source.
        self.model.transform.postprocess = lambda result, *_: result

    def forward(self, image):
        result = self.model([image[0]])[0]
        return result['boxes'], result['labels'], result['scores'], result['masks']


def image_tensor(path):
    image = Image.open(path).convert('RGB')
    scale = min(800 / image.width, 800 / image.height)
    image = image.resize((round(image.width * scale), round(image.height * scale)), Image.Resampling.BILINEAR)
    canvas = Image.new('RGB', (800, 800), (128, 128, 128))
    canvas.paste(image, (0, 0))
    return torch.from_numpy(np.array(canvas).transpose(2, 0, 1).copy()).float()[None] / 255


def main():
    torch.set_num_threads(4)
    model = InstanceGraph().eval()
    sample = image_tensor(ROOT / 'research/m0/portrait-astronaut.png')
    destination = ASSETS / 'maskrcnn-r50-v2.onnx'
    start = time.perf_counter()
    with torch.inference_mode():
        reference = model(sample)
        print('reference', [x.shape for x in reference], time.perf_counter() - start, flush=True)
        torch.onnx.export(model, sample, str(destination), opset_version=17, dynamo=False,
                          input_names=['image'], output_names=['boxes', 'labels', 'scores', 'masks'],
                          dynamic_axes={name: {0: 'instances'} for name in
                                        ['boxes', 'labels', 'scores', 'masks']})
    graph = onnx.load(str(destination))
    onnx.checker.check_model(graph)
    options = ort.SessionOptions()
    options.intra_op_num_threads = 4
    session = ort.InferenceSession(str(destination), options, providers=['CPUExecutionProvider'])
    start = time.perf_counter()
    actual = session.run(None, {'image': sample.numpy()})
    elapsed = time.perf_counter() - start
    errors = []
    for expected, value in zip(reference, actual):
        np.testing.assert_allclose(expected.numpy(), value, rtol=2e-3, atol=2e-3)
        errors.append(float(np.max(np.abs(expected.numpy() - value))))
    categories = torchvision.models.detection.MaskRCNN_ResNet50_FPN_V2_Weights.DEFAULT.meta['categories']
    report = dict(torch=torch.__version__, torchvision=torchvision.__version__,
                  onnx=onnx.__version__, onnxruntime=ort.__version__,
                  sha256=hashlib.sha256(destination.read_bytes()).hexdigest(),
                  bytes=destination.stat().st_size, seconds=elapsed, errors=errors,
                  shapes=[list(x.shape) for x in actual], categories=categories)
    (ASSETS / 'conversion.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report), flush=True)


if __name__ == '__main__':
    main()
