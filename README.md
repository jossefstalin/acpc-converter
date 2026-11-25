# Air Conflicts Pacific Workshop 0.2.0-alpha

《空中冲突：太平洋》模型查看、座舱转换和模型制作工具集。查看器基于独立实现的格式解析和渲染代码。

本项目含 Windows 桌面查看器、Blender 插件、3ds Max 的 MAXScript 适配器、离线 WebGL 查看器和 Python 座舱转换器。仓库只发布工具源码和自建测试数据。

## 当前验证状态

| 组件 | 支持内容 | 已验证范围 |
|---|---|---|
| Python 格式核心 | BMF 多帧、B3DMH10 MSH、双 UV、材质范围 | 用户样本 144 个 BMF、5 个 MSH 逐字节读写往返相同 |
| Blender 插件 | 模型查看、INI 静态装配、双 UV、BMF 动画、MSH/BMF 导出、颜色图集、DDS | Blender 4.3.2 实际运行；原版座舱与舵面动画往返测试通过 |
| Windows 查看器 | 独立窗口、BMF/MSH/OBJ、贴图、双 UV、LOD、动画滑块、OBJ + MTL | 通过 x64 构建，144 个 BMF 和 5 个 MSH 样本的网格、帧数和 LOD 与 Python 解析一致 |
| 网页查看器 | BMF/MSH/OBJ、DDS、BMF 帧滑块、MSH 覆盖包和当前帧 BMF 导出 | JavaScript 语法和格式读写交叉验证通过 |
| Max 适配器 | 静态模型导入导出、UV 通道 1/2、材质路径 | Python 交换层通过测试；MAXScript 未在实际 3ds Max 中运行，实验性 |
| 游戏内表现 | 相机、光照、玻璃、动画语义、损坏系统、完整飞机载入 | **未在完整 ACPC 游戏中实测** |

这是可维护的 alpha 工具，不是已完成飞控、武器和破损配置的整机自动移植器。

## Blender 安装和查看

1. 在 Releases 下载 `AirConflicts_Blender_Addon.zip`，不要先解压。
2. Blender → Preferences → Add-ons → Install from Disk，选择该 ZIP，启用插件。
3. File → Import → Air Conflicts，打开 `.bmf`、`.msh` 或飞机 `1.ini`。
4. 三维窗口侧栏 `Air Conflicts` 中提供导入、图集和导出按钮。切换 Material Preview 查看材质。

`UVMap` 是颜色 UV，`Lightmap` 是第二套光照 UV。BMF 多帧通过 Shape Keys 载入；INI 的装配包括机体、配置的舵面、起落架、螺旋桨和炮塔的默认姿态。飞机逻辑不会随模型自动生成。

## OBJ / 战争雷霆等外部模型制作路径

1. 用 Blender 自带的 Wavefront OBJ 导入外部资源，并确认尺度、前向、机身坐标和活动部件划分。
2. 多材质座舱可以直接导出 MSH。普通 3P BMF 只能配一个外部纹理集；先合并应为同一部件的几何体，再点击 **Build color atlas + DDS** 生成颜色图集。
3. 图集保留原 UV 的重复采样关系，再映射至各材质的独立区域。它只处理颜色图，不会自动解包其它游戏的 AO/roughness/metallic 合并贴图，也不会自动转换法线通道约定。
4. 导出座舱用 **Export cockpit MSH + MTL**；导出机体、舵面和起落架用 **Export body / moving part BMF**。
5. 动画 BMF 勾选 `Sample scene frame range`；要求帧间拓扑和 UV 一致。帧数、帧序与活动部件语义仍须对应原飞机模板。
6. 用原版相近飞机的 INI 配置模型路径、座舱相机、轮点、武器点和损坏分组。导出的 MIF 默认统一 ID 只是起始配置，不能代替完整破损设计。

MSH 每个对象最多 65,536 个渲染顶点（UV 接缝和法线接缝都会增加渲染顶点）；超过时插件报错，需合理分体。插件不自动减面。

新导出的 PNG/JPEG 纹理可转换为带 mipmap 的 RGBA32 DDS；已有 DXT1/3/5 DDS 保留压缩像素数据。RGBA32 比 DXT 占用更多显存，正式资产可再使用贴图工具压缩。

## 座舱转换器

安装 Python 3.10+，解压整个工具包，双击 `Start_Converter.bat`；或运行：

```sh
python converter_gui.py
python ac_formats.py port old_plane output_patch --target db3 --mode bmf
python ac_formats.py port old_plane output_patch --target fw190 --mode msh
```

- `bmf`：将 `cockpit.bmf` 与 `cockpitlight.bmf` 合并，写成含颜色 UV 和光照 UV 的 MSH；两者拓扑不一致时拒绝转换。
- `msh`：保留已有 MSH，修复材质目录和座舱启用配置。适合已有兼容 MSH 的资源包。
- 输出必须是独立的新目录；不会直接修改源文件或游戏。仅自动修改已经指向目标飞机目录的 INI，跨机型配置列入 `skipped_foreign_ini`。
- `--offset x y z` 使用游戏原生坐标，仅在明确需要调整模型位置时使用；默认不移动网格。
- 覆盖包只含座舱相关变更，要求目标飞机原来的机体、声音、物理配置和公共贴图已经存在。

## Windows 模型查看器

下载 `AirConflicts_Viewer_Windows_x64.zip`，解压后双击 `AirConflicts_Viewer_Windows_x64.exe`。整套工具包里也可以从 `viewer/windows/` 启动。桌面窗口由 Win32 C 实现，直接调用 Windows 系统库。

- 将 `.bmf`、`.msh` 或 `.obj` 拖进窗口。按 Ctrl+O 可以打开文件。左键旋转，右键平移，滚轮缩放，双击复位。
- 预览颜色贴图、灰模、线框、颜色 UV 棋盘、法线、光照贴图和光照 UV 棋盘。MSH 可切换文件内 LOD，BMF 动画可播放和逐帧检查。
- 贴图可按同目录或声明的 MTL 路径载入。支持 DDS、PNG、JPEG 和 BMP。选中材质后可手动指定颜色贴图。
- 保留资源原目录结构。旧版座舱同目录的 `cockpitlight.bmf` 在几何对应时提供第二套 UV。
- 从“文件”菜单保存 BMP，或导出当前帧和 LOD 的 OBJ、颜色 UV、法线及 MTL。贴图链接指向原纹理文件。

Windows 上使用带 Windows SDK 的 Visual Studio C 编译工具运行 `viewer\native\build_windows.cmd`。Linux 上安装 MinGW-w64 后执行 `sh viewer/native/build_mingw.sh`。两个脚本默认输出至 `dist/`。

## 网页查看器

解压 `AirConflicts_Viewer.zip`，双击 `index.html`，用现代 Edge/Chrome 打开。它不需要安装 Python 或访问外部 CDN。

选择一个飞机资源文件夹，或多选模型、MTL 和 DDS/PNG/JPEG 贴图。选择单个飞机目录，避免同名贴图来自不同飞机。可旋转、缩放、看法线/棋盘/线框和 BMF 帧；提供一个不含游戏素材的内置校验模型。

- MSH 导出生成 ZIP，包含 MSH、游戏格式 MTL 和被引用的颜色/光照纹理。
- BMF 导出当前帧，且仅接受单材质模型；多材质请先在 Blender 或 Max 中制作图集。
- 当前查看器显示 LOD0；不模拟原游戏的 FX 着色器、阴影、玻璃与损坏逻辑。

## 3ds Max 安装

完整解压工具包，在 Max 中执行 `max/AirConflictsWorkshop.ms`。选择本机 `python.exe`（3.10+）或 `blender.exe`（3.6+）作为格式桥接运行环境。

- MAXScript 使用 Max 2015 时代可用的 mesh / Edit_Normals / .NET API 设计；**没有 Max 实机测试**。
- 导入 BMF/MSH 建立可编辑网格，颜色 UV 在通道 1、光照 UV 在通道 2。
- 导出选择一个对象；静态 3P BMF 需一个材质和已烘焙图集，MSH 允许多材质。
- 当前 Max 适配器不负责 BMF 动画重建。动画制作与往返优先使用已实测的 Blender 插件。
- 导出前 Reset XForm，避免未应用的负缩放和非均匀缩放影响方向与法线。

## 测试与维护

```sh
python -m unittest discover -s tests
node tests/test_codec.js
python -m py_compile ac_formats.py max_bridge.py converter_gui.py
```

`validate_blender.py` 是使用本次用户样本的集成测试，需要在本地准备相应 `analysis/` 和 `deliverables/` 目录；原始样本不公开。CI 使用仓库内人工构造的测试几何体。

本次 OBJ 成品保留文件内面数，不减面。Blender 自带 OBJ 导入器会移除部分退化面：Bf 109 机体样本从 2,141 面导入后为 2,137 面；使用本插件直接导入 BMF 可绕开这条 OBJ 导入路径。

格式依据、发现的样本混用、软件检索见 `docs/FORMAT_AND_FINDINGS.md`。软件代码采用 MIT 许可证；游戏名称、商标与游戏资产不属于该许可。
