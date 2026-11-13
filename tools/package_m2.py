"""Build a portable M2 preview with runtime and default RAW model; no test EXE."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MODEL_SHA = 'da27509dab6a2915da67e988acd86cf71f9d5bbc8d1aa0ed32933578a887b901'
SEMANTIC_SHA = '367115635ad3f5ffba4cb0244e4ce93bb36bcff1ece97fca8728ffb952c32695'
INSTANCE_SHA = '19455030e0a5e56e80476bdc5a3dcdf0ced79a5871ddc34fd3abee600e2ead76'


def production_runtime_files(build, required):
    """Follow actual PE imports, including Lensfun's transitive runtime libraries."""
    compilers = Path('D:/Program Files/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC')
    inspectors = sorted(compilers.glob('*/bin/Hostx64/x64/dumpbin.exe'))
    if not inspectors:
        raise FileNotFoundError('MSVC dumpbin is required to verify package dependencies')
    available = {p.name.lower(): p for p in
                 (ROOT/'build/vcpkg_installed/x64-windows/bin').glob('*.dll')}
    available.update({p.name.lower(): p for p in build.glob('*.dll')})
    pending = [build/'Huelith.exe', *(build/name for name in required)]
    selected = {}
    while pending:
        file = pending.pop()
        key = file.name.lower()
        if key in selected:
            continue
        if not file.is_file():
            raise FileNotFoundError(file)
        selected[key] = file
        output = subprocess.run([str(inspectors[-1]), '/nologo', '/dependents', str(file)],
                                capture_output=True, text=True, check=True).stdout
        for name in re.findall(r'^\s+([\w.-]+\.dll)\s*$', output, re.MULTILINE | re.IGNORECASE):
            dependency = available.get(name.lower())
            if dependency is not None:
                pending.append(dependency)
    return list(selected.values())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, default=ROOT / 'build/releases/M2-preview-20261011-crop-retouch-blur-r18')
    parser.add_argument('--stage', choices=['M2','M3'], default='M2')
    args = parser.parse_args()
    destination = args.output.resolve()
    release_root = (ROOT / 'build/releases').resolve()
    if destination == release_root or not destination.is_relative_to(release_root):
        raise ValueError('Output must be a package folder inside build/releases')
    if destination.exists():
        raise FileExistsError('Choose a fresh package directory to preserve existing previews')
    build = ROOT / 'build/windows-release'
    qt = Path('D:/Program Files/Qt/6.10.3/msvc2022_64')
    model = build / 'models/model_bayer.onnx'
    if hashlib.sha256(model.read_bytes()).hexdigest() != MODEL_SHA:
        raise ValueError('Built-in model checksum mismatch')
    semantic_model = build/'models/pp-mobileseg-base.onnx'
    if hashlib.sha256(semantic_model.read_bytes()).hexdigest() != SEMANTIC_SHA:
        raise ValueError('Built-in scene model checksum mismatch')
    instance_model = build/'models/maskrcnn-r50-v2.onnx'
    if hashlib.sha256(instance_model.read_bytes()).hexdigest() != INSTANCE_SHA:
        raise ValueError('Built-in instance model checksum mismatch')
    runtime_names = ('raw_r.dll', 'lcms2-2.dll', 'tiff.dll', 'jpeg62.dll', 'liblzma.dll',
                     'z.dll', 'onnxruntime.dll', 'onnxruntime_providers_shared.dll', 'DirectML.dll')
    runtime_files = production_runtime_files(build, runtime_names)
    if not (build/'lensfun').is_dir():
        raise FileNotFoundError('Built-in Lensfun database missing')
    metadata_manifest=json.loads((ROOT/'research/m2/metadata/manifest.json').read_text(encoding='utf-8'))
    for name,digest in metadata_manifest['files'].items():
        path=build/'tools/exiftool'/name
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=digest:
            raise ValueError('Pinned ExifTool runtime mismatch: '+name)
    destination.mkdir(parents=True)
    shutil.copy2(ROOT/'LICENSE', destination/'LICENSE')
    evidence = ROOT / 'docs/test-evidence' / args.stage / destination.name
    evidence.mkdir(parents=True, exist_ok=True)
    for file in runtime_files:
        shutil.copy2(file, destination/file.name)
    shutil.copytree(build/'lensfun', destination/'lensfun')
    shutil.copytree(build/'tools/exiftool', destination/'tools/exiftool')
    if not (build/'tools/c2patool/c2patool.exe').is_file():
        raise FileNotFoundError('C2PA tool missing; run tools/prepare-c2patool.ps1 and rebuild')
    shutil.copytree(build/'tools/c2patool', destination/'tools/c2patool')
    licenses = destination/'licenses'
    licenses.mkdir()
    shutil.copy2(ROOT/'research/m2/metadata/manifest.json',licenses/'ExifTool-runtime.json')
    for license_file in (ROOT/'licenses').glob('*.txt'):
        shutil.copy2(license_file, licenses/license_file.name)
    for copyright_file in (ROOT/'build/vcpkg_installed/x64-windows/share').glob('*/copyright'):
        shutil.copy2(copyright_file, licenses/(copyright_file.parent.name+'.txt'))
    (licenses/'Lensfun-database.txt').write_text(
        'Lensfun 0.3.4 lens database, copyright the Lensfun contributors.\n'
        'Source: https://github.com/lensfun/lensfun/tree/v0.3.4/data\n'
        'Database: Creative Commons Attribution-ShareAlike 3.0; original XML files retained.\n'
        'License: https://creativecommons.org/licenses/by-sa/3.0/\n'
        'Library: LGPL-3.0, dynamically linked; see lensfun.txt.\n', encoding='utf-8')
    deploy = subprocess.run([str(qt / 'bin/windeployqt.exe'), '--release', '--no-libraries', '--no-translations',
                             '--dir', str(destination), *(str(qt/'bin'/name) for name in
                             ('Qt6Widgets.dll','Qt6Svg.dll','Qt6Network.dll','Qt6PrintSupport.dll'))], capture_output=True, text=True)
    (evidence / 'deploy.log').write_text(deploy.stdout + '\n' + deploy.stderr, encoding='utf-8')
    deploy.check_returncode()
    crt = Path('D:/Program Files/Microsoft Visual Studio/2022/BuildTools/VC/Redist/MSVC/14.44.35112/x64/Microsoft.VC143.CRT')
    for file in crt.glob('*.dll'):
        shutil.copy2(file, destination / file.name)
    (destination / 'models').mkdir()
    shutil.copy2(model, destination / 'models/model_bayer.onnx')
    shutil.copy2(semantic_model, destination/'models/pp-mobileseg-base.onnx')
    shutil.copy2(instance_model, destination/'models/maskrcnn-r50-v2.onnx')
    enhancement_manifest = json.loads((ROOT/'research/m2/enhancement/manifest.json').read_text(encoding='utf-8'))
    for entry in enhancement_manifest['models']:
        path = build/'models'/entry['file']
        if hashlib.sha256(path.read_bytes()).hexdigest() != entry['sha256']:
            raise ValueError('Enhancement model checksum mismatch: '+entry['file'])
        shutil.copy2(path, destination/'models'/entry['file'])
    if args.stage == 'M3':
        portrait_models=json.loads((ROOT/'research/m3/openseeface/manifest.json').read_text(encoding='utf-8'))
        portrait_models.append(json.loads((ROOT/'research/m3/nail-segmentation/manifest.json').read_text(encoding='utf-8')))
        shutil.copy2(ROOT/'research/m3/nail-segmentation/NOTICE.md',licenses/'nail-segmentation-NOTICE.md')
        shutil.copy2(ROOT/'research/m3/nail-segmentation/model-card.md',licenses/'nail-segmentation-model-card.md')
        portrait_models.append(json.loads((ROOT/'research/m3/face-landmarks/manifest.json').read_text(encoding='utf-8')))
        portrait_models.append(json.loads((ROOT/'research/m3/blemish-model-evaluation/manifest.json').read_text(encoding='utf-8')))
        shutil.copy2(ROOT/'research/m3/blemish-model-evaluation/ModelScope-LICENSE',licenses/'ModelScope-blemish.txt')
        shutil.copy2(ROOT/'research/m3/blemish-model-evaluation/DETECTION-NOTICE.md',licenses/'ModelScope-blemish-NOTICE.md')
        shutil.copy2(ROOT/'research/m3/blemish-model-evaluation/model-card.md',licenses/'ModelScope-blemish-model-card.md')
        for directory,filename in [('mediapipe-person','person_detection_mediapipe_2023mar.onnx'),('mediapipe-pose','pose_estimation_mediapipe_2023mar.onnx'),('selfie-multiclass','selfie_multiclass.onnx')]:
            entry=json.loads((ROOT/'research/m3'/directory/'manifest.json').read_text(encoding='utf-8'))
            entry['file']=filename;portrait_models.append(entry)
        for entry in portrait_models:
            path=build/'models'/entry['file']
            if hashlib.sha256(path.read_bytes()).hexdigest()!=entry['sha256']:
                raise ValueError('Portrait model checksum mismatch: '+entry['file'])
            shutil.copy2(path,destination/'models'/entry['file'])
        (licenses/'portrait-models.json').write_text(json.dumps(portrait_models,indent=2),encoding='utf-8')
        # Ship user instructions only; design mockups, research and local
        # validation records are not runtime dependencies or release content.
        (destination/'M3').mkdir()
        for name in ('usage.md', 'layer-enhancement.md'):
            shutil.copy2(ROOT/'docs/m3'/name, destination/'M3'/name)
    (licenses/'enhancement-models.json').write_text(json.dumps(enhancement_manifest,indent=2),encoding='utf-8')
    shutil.copy2(ROOT/'research/m2/instances/runtime/torchvision-0.28.0.dist-info/LICENSE', licenses/'Torchvision.txt')
    (licenses/'Torchvision-instance-model.txt').write_text(
        'Mask R-CNN ResNet-50 FPN V2, Torchvision COCO_V1 weights.\n'
        'https://docs.pytorch.org/vision/main/models/generated/torchvision.models.detection.maskrcnn_resnet50_fpn_v2.html\n'
        'Source weights: https://download.pytorch.org/models/maskrcnn_resnet50_fpn_v2_coco-73cbd019.pth\n'
        'Torchvision source: BSD-3-Clause (Torchvision.txt); pretrained weights may carry dataset-derived terms.\n'
        'COCO dataset terms: https://cocodataset.org/#termsofuse\n'
        'Converted to ONNX opset 17; unchanged learned weights; box-local 28x28 masks.\n'
        'No training images or evaluation samples are distributed in this package.\n'
        'Model SHA256: '+INSTANCE_SHA+'\n', encoding='utf-8')
    shutil.copy2(ROOT/'research/m2/semantic/pp-mobileseg/LICENSE', licenses/'PaddleSeg.txt')
    (licenses/'PaddleSeg-model.txt').write_text(
        'PP-MobileSeg Base ADE20K, PaddlePaddle/PaddleSeg, Apache-2.0.\n'
        'https://github.com/PaddlePaddle/PaddleSeg/tree/release/2.10/configs/pp_mobileseg\n'
        'Original export: https://bj.bcebos.com/paddleseg/dygraph/ade20k/pp_mobileseg_base/export_model.zip\n'
        'Converted with paddle2onnx 1.3.1; replaced dynamic VIM export with ordinary\n'
        'bilinear upsampling of unchanged learned logits and ArgMax labels.\n'
        'Model SHA256: '+SEMANTIC_SHA+'\n', encoding='utf-8')
    (destination / 'translations').mkdir(exist_ok=True)
    shutil.copy2(qt / 'translations/qtbase_zh_CN.qm', destination / 'translations/qtbase_zh_CN.qm')
    (destination / 'README.txt').write_text(
        'Huelith\n双击 Huelith.exe。移动软件时请保留整个目录。\n\n'
        '支持图层编辑、RAW 显影、人像精修、蒙版、文字与形状、批量导出。\n'
        '滤镜 > 人像精修：打开人像编辑窗口。\n'
        '空格拖动画布，Ctrl+0 适合窗口，Ctrl+1 显示 100%。\n'
        '生成式工作流需要连接自行配置的本机或远程 ComfyUI 服务。\n'
        '工程保存保留可编辑内容；PSD/PSB 兼容范围以导入导出提示为准。\n'
        '语言：首选项 > 界面与工作区 > 界面语言，重启后生效。\n\n'
        'Launch Huelith.exe. Keep this entire folder when moving the application.\n'
        'Features: layers, RAW development, portrait retouching, masks, text, shapes, and batch export.\n'
        'Filter > Portrait Retouch opens the portrait workspace.\n'
        'Space-drag to pan; Ctrl+0 fits the image; Ctrl+1 shows 100%.\n'
        'Generative workflows require your own local or remote ComfyUI service.\n'
        'Save a project to retain editable content. PSD/PSB compatibility is explained during import/export.\n'
        'Language: Preferences > Interface and Workspace > Interface Language. Restart to apply.\n', encoding='utf-8')
    environment = os.environ.copy()
    for key in ('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_QPA_PLATFORM', 'PYTHONPATH'):
        environment.pop(key, None)
    environment['PATH'] = str(Path(environment.get('SystemRoot', 'C:/Windows')) / 'System32')
    smoke = subprocess.run([str(destination / 'Huelith.exe'), '--smoke-test'], cwd=destination,
                           env=environment, capture_output=True, text=True, timeout=30)
    report = dict(package=str(destination), startup_exit=smoke.returncode, stderr=smoke.stderr,
                  model_sha256=MODEL_SHA,
                  files={str(p.relative_to(destination)): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in destination.rglob('*') if p.is_file()})
    (evidence / 'package.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    smoke.check_returncode()
    print(json.dumps(dict(package=str(destination), startup_exit=smoke.returncode,
                          files=len(report['files']), size_mb=sum(p.stat().st_size for p in destination.rglob('*')
                                                                if p.is_file()) / 1024**2), ensure_ascii=False))


if __name__ == '__main__':
    main()
