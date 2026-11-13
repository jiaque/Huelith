# Huelith

[English](README.md) | **简体中文**

Huelith 是一款面向 Windows 的桌面照片编辑器，提供基础修图、图层合成、RAW 显影、AI 图像增强和人像精修，可用于摄影后期、人像修饰、产品图片处理与创意合成。

支持简体中文和英文。安装时选择语言，也可在「首选项 → 界面与工作区 → 界面语言」中更改，重启后生效。

项目原创代码采用 [GNU GPL v3.0（GPL-3.0-only）](LICENSE)。分发基于这些代码的修改版或衍生软件时，须按 GPL-3.0 提供对应源码。第三方代码、模型和素材沿用各自许可。

## 功能

### 基础修图与图层编辑

- **常规编辑**：新建尺寸与分辨率预设、裁剪、旋转、缩放、自由变换、透视与网格变形。
- **绘画与修复**：画笔、橡皮擦、填充、渐变、仿制图章、修复画笔、污点修复、修补、减淡与加深，以及独立液化窗口。
- **选区与蒙版**：矩形与椭圆选区、套索、魔棒、对象选择、选区细化、图层蒙版和剪贴蒙版。
- **图层合成**：图层分组、混合模式、不透明度、图层样式、调整与滤镜、文字、形状和路径。
- **排版与工作区**：画板、切片、相框与布局模板、标尺、参考线、网格和吸附；多文档标签、浮动窗口、可调整侧栏及工作区布局保存。
- **颜色与输出预览**：RGB、灰度、Lab、CMYK 文档颜色设置，ICC 指定与转换、屏幕软校样、色域警告和打印。
- **编辑习惯**：撤销与历史记录、快捷键配置、工具预设，以及光标、画布、单位、缓存等首选项。

![主窗口：工具栏、照片画布、曝光调整、图层与蒙版](assets/screenshots/main-window.png)

*主窗口截图使用 NASA STS-47 舱内合影，展示实际程序中的照片编辑与分层操作。*

### RAW 显影

- 光线、白平衡、曲线、色彩混合器、校正、光学、细节与局部蒙版调整。
- 支持 Sony ARW / SR2 / SRF、Canon CRW / CR2 / CR3、Nikon NEF / NRW、Fujifilm RAF（含 X-Trans）、Panasonic RW2、DNG，以及内置 LibRaw 支持的其他格式。
- 普通调整自动预览，AI RAW 降噪单独执行；降噪支持范围取决于传感器排列与模型。
- 导出图片，或转存保留受支持 RAW 数据及显影设置的 DNG 副本，保留原片。

![RAW 显影：细节调整与降噪](assets/screenshots/raw-develop.png)

### AI 图像增强与照片合成

- 整图或单个图层的 2× / 4× 超分、RGB AI 降噪和深度选区。
- HDR 合并、全景拼接与景深合成。
- 超过 6400 万像素的大图分块处理、编辑和保存。

### 人像精修

- **面部与身材**：丰盈提拉、面部重塑、身材塑性、瘦脸瘦身、小头和液化。
- **皮肤修复**：磨皮、祛斑祛痘、祛皱、去油光和局部消除。
- **美妆与造型**：唇妆、眉妆、眼妆、腮红、修容、染发、发饰、面饰、纹身与美甲。
- **牙齿与眼部**：牙齿去黄和提亮、眼周修复、眼白去红、闪光红眼修正、美瞳与眼神光。
- 选人编辑、左右独立调整、手动画笔、范围修正、前后对比与撤销。

![美妆：按部位折叠设置，选择颜色并调整程度](assets/screenshots/portrait-makeup.png)

![眼部精修：美瞳材质、左右独立调整与范围修正](assets/screenshots/portrait-eyes.png)

### 生成式工作流 · ComfyUI

- 连接用户自行配置的本机或远程 ComfyUI 服务，程序不内建 ComfyUI。
- 导入工作流 JSON 与描述文件，在程序内配置参数，支持局部重绘、生成填充、扩图等工作流。
- 选择当前图层、选中图层或全部图层作为输入，并使用软件内的选区与蒙版。
- 查看任务进度、取消任务、获取最终候选图像；结果作为新图层加入，保留输入图层以便恢复。
- 检查服务节点与模型依赖；缺失依赖需在对应的 ComfyUI 服务上安装。

### 文件兼容与批量处理

- **工程与图片**：`.huelith` 可编辑工程，PNG / JPEG / TIFF 输出；批量导出支持尺寸、命名规则与导出预设，也可分别导出图层、画板和切片。
- **PSD / PSB**：导入与导出受支持的像素图层、分组和蒙版。部分原生属性会按像素外观导入，兼容范围以导入、导出提示为准。
- **PDF**：选择页面与分辨率，栅格化后导入编辑。
- **自动化动作**：编辑与播放 JSON 动作，支持尺寸调整、曝光、不透明度、重命名、条件和变量，并可批量处理照片。
- **元数据与凭证**：EXIF 边框、元数据导出、C2PA 内容凭证检查；签名需配置外部签名程序。

## 源码编译

### 环境

| 依赖 | 要求 |
| --- | --- |
| 系统 | Windows x64 |
| 编译器 | Visual Studio 2022 C++ 工具链，支持 C++20 |
| 构建工具 | CMake 3.25+、Ninja |
| Qt | 6.10.3，MSVC x64，包含 Widgets、Svg、PrintSupport、Network、Concurrent、WebSockets |
| vcpkg | 依赖及固定基线见 `vcpkg.json` |
| Python | Python 3，用于准备原生推理 SDK |
| 推理运行时 | ONNX Runtime DirectML 1.24.4、DirectML 1.15.4 |

### 准备资源

当前构建还需要仓库外的模型和运行文件。请按 `CMakeLists.txt` 指定的路径准备：

- `research/m3/openseeface/`：`lm_model3_opt.onnx`、`mnv3_detection_opt.onnx`。
- `research/m3/mediapipe-pose/`：`pose_estimation_mediapipe_2023mar.onnx`。
- `research/m3/mediapipe-person/`：`person_detection_mediapipe_2023mar.onnx`。
- `research/m3/selfie-multiclass/`：`selfie_multiclass.onnx`。
- `research/m2/metadata/runtime/exiftool-13.59_64/`：`exiftool(-k).exe` 和 `exiftool_files/`。

这些文件尚未随源码发布，缺少时 CMake 配置会失败。RAW 降噪、超分、RGB 降噪、美甲等功能还需相应模型。完整模型下载与资源准备脚本待补充。

### 构建命令

在 Visual Studio Developer PowerShell 中进入源码根目录，将以下环境变量改为实际安装路径：

```powershell
$env:VCPKG_ROOT = "C:/dev/vcpkg"
$env:QT_ROOT = "C:/Qt/6.10.3/msvc2022_64"

# 下载并校验 ONNX Runtime / DirectML 原生 SDK
python tools/setup_raw_sdk.py

# 配置并编译 Release
cmake --preset windows-release
cmake --build --preset windows-release --target Huelith
```

程序输出到 `build/windows-release/Huelith.exe`。运行时保留同目录的 DLL、Qt 插件、模型和工具目录。
