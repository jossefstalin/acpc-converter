# Changelog

## 0.2.0-alpha

- 新增 Windows x64 原生模型查看器，可通过独立窗口打开和拖入 BMF、MSH、OBJ。
- 新增桌面贴图预览、两套 UV 检视、MSH LOD 切换、BMF 动画滑块和逐帧操作。
- 新增桌面 BMP 保存与当前帧 OBJ、法线、UV0、MTL 导出。
- 提供便携 exe 和 ZIP、Visual Studio 与 MinGW-w64 构建脚本，以及 Windows 启动测试。

## 0.1.0-alpha

- BMF 多帧与 B3DMH10 MSH 格式解析、双 UV、格式校验。
- Blender 导入导出、INI 部件装配、动画帧采样、颜色图集与 DDS 输出。
- MaxScript 静态桥接适配器（待 Max 实机验证）。
- 离线 WebGL 查看器与静态模型导出。
- 旧式 BMF 座舱合并光照 UV 的转换器。
- 区分旧 BMF / 部分 MSH 与太平洋版参考面方向；保留未修改源 MSH 的方向记录。
- CI 单元测试与可复现 Release 打包。

完整游戏内载入、飞控、破损和官方 Modelviewer 二进制适配不在本次已验证范围内。
