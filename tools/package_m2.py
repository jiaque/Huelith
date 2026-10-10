"""Build a portable M2 preview with runtime and default RAW model; no test EXE."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MODEL_SHA = 'da27509dab6a2915da67e988acd86cf71f9d5bbc8d1aa0ed32933578a887b901'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, default=ROOT / 'build/releases/M2-preview-20261010-navigation-r4')
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
    runtime_names = ('raw_r.dll', 'lcms2-2.dll', 'tiff.dll', 'jpeg62.dll', 'liblzma.dll',
                     'z.dll', 'onnxruntime.dll', 'onnxruntime_providers_shared.dll', 'DirectML.dll')
    for name in ('Huelith.exe', *runtime_names):
        if not (build / name).is_file():
            raise FileNotFoundError(build / name)
    destination.mkdir(parents=True)
    evidence = ROOT / 'docs/test-evidence/M2' / destination.name
    evidence.mkdir(parents=True, exist_ok=True)
    for name in ('Huelith.exe', *runtime_names):
        shutil.copy2(build / name, destination / name)
    deploy = subprocess.run([str(qt / 'bin/windeployqt.exe'), '--release', '--no-translations',
                             str(destination / 'Huelith.exe')], capture_output=True, text=True)
    (evidence / 'deploy.log').write_text(deploy.stdout + '\n' + deploy.stderr, encoding='utf-8')
    deploy.check_returncode()
    crt = Path('D:/Program Files/Microsoft Visual Studio/2022/BuildTools/VC/Redist/MSVC/14.44.35112/x64/Microsoft.VC143.CRT')
    for file in crt.glob('*.dll'):
        shutil.copy2(file, destination / file.name)
    (destination / 'models').mkdir()
    shutil.copy2(model, destination / 'models/model_bayer.onnx')
    (destination / 'translations').mkdir(exist_ok=True)
    shutil.copy2(qt / 'translations/qtbase_zh_CN.qm', destination / 'translations/qtbase_zh_CN.qm')
    (destination / 'README.txt').write_text(
        'Huelith M2 开发预览（缩放与画面操控更新，非 M2 完成版）\n'
        '双击本目录 Huelith.exe。不要只移动 exe；整个目录保留。\n'
        '文件 > 打开，选择 CR3 等 RAW 后进入独立显影窗口。\n'
        '右侧效果包含去薄雾。细节中 AI 降噪默认使用随包内置模型，无需浏览研究目录。\n'
        '修改降噪模型/后端后点击更新预览；普通调色自动刷新。\n'
        '100% 会在停止操作后细化；打开为源图层时生成全分辨率结果。\n'
        '样例：D:\\project\\wifi\\Huelith\\research\\m0\\samples\\r10-5871.CR3\n'
        '颜色混合器有通道选项卡；滤镜 > 智能效果管理调色/LUT与预设。\n'
        '选区组合按钮、像素直径、RAW蒙版工具和后台调色预览已更新。\n'
        '缩放工具上方可选放大/缩小；编辑 > 首选项 > 工具与画面操控可启用滚轮缩放。\n'
        'Alt临时反向，空格拖动，Ctrl+0适合窗口，Ctrl+1显示100%。\n'
        '尚非 Camera Raw 完整复刻；光学、完整白平衡、语义蒙版等仍在 M2 待办中。\n',
        encoding='utf-8')
    environment = os.environ.copy()
    for key in ('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'PYTHONPATH'):
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
