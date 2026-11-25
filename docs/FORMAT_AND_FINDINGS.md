# 格式与样本分析

## 样本来源

| 用户上传文件 | 本次解释 |
|---|---|
| ACPC.exe | 太平洋版程序，仅静态检查，不随工具分发 |
| ki45.zip | 用户确认其中主体为秘密战争 Bf 109，不能按目录名推断机型 |
| db3.zip | 用户确认资源混杂，分别保留旧 BMF 与已有 MSH |
| fw190.zip | 用户确认秘密战争 Fw 190；材质定义仍引用 planes/p40/ |
| avenger.zip | 用户确认真正太平洋版参考 |
| avenger(1).zip | 用户确认较干净的秘密战争样本；仍发现 planes/gotha/ 配置和与太平洋版相同的备用 MSH |

两份 Avenger 的 `cockpit.msh`、`cockpit.mtl`、`cockpit1.dds`、`cockpit2.dds` SHA-256 相同；这证明包中这些具体文件相同，不等于证明所有旧版模型都可以直接使用，也不证明文件夹内没有混用资源。

## 对实际问题的判断

旧式座舱的颜色网格与光照网格使用不同 UV 索引。太平洋版的座舱材质框架使用 MSH 的 UV0/UV1。单纯改后缀不能完成这一步；需要逐三角面、逐顶点角合并位置、法线、颜色 UV、光照 UV，并重建切线。

多个旧配置将 `COCKPIT`、`COCKPITMATERIAL` 设置为 `none`，或继续使用旧式 `COCKPITLIGTH` 两次引用。转换器修改 Appearance 中的模型/材质绑定，保留 Camera 中的 `COCKPIT x y z`。

DB-3 的已有 MSH 内部对象名为 `B25_mesh`，其材质指向 `planes/b25/`，并且与旧 BMF 的包围盒不同。Bf 109 包中也有两套不同几何体的座舱。因此成品分别命名 `1p_legacy_BMF` 与 `1p_supplied_MSH`，没有互相替代或混套 UV。

**三角面方向不能统一凭后缀猜测。** 真正太平洋版 Avenger 的 MSH 中绝大多数三角面的几何叉积与法线同向；旧 BMF 及部分备用 MSH 则相反。读取后保持原二进制数据；进入 Blender 或 OBJ 时按每面法线确定显示方向。新写出的 MSH 采用太平洋版参考方向；BMF 采用其原格式方向。未改动的 MSH 经 Blender 导出时保留记录的源面方向。

旧 DDS Alpha 不一律视作透明度。工具保留原 DDS Alpha；查看器默认按不透明颜色预览。OBJ 旧座舱另提供 `cockpit_rgb.png`，以免导入器把未知用途的 Alpha 误接为整体透明。

## 二进制布局

BMF 小端：4 个 uint32 为帧数 F、三角面数 T、位置数 V、纹理坐标数 U。之后 T×6 uint32，前三个为位置索引、后三个为 UV 索引；U×float2 UV；每帧为 7 个 float 的包围盒/半径，V×float3 位置、3T×float3 顶点角法线、3T×float3 切线、T×float4 面平面。文件长度为 `16 + 24T + 8U + F(28 + 12V + 88T)`。

MSH 标识为 `B3DMH\0` + ASCII `10`。20 字节文件头后为 64 字节材质名表与对象块；本次样本为静态网格对象类型 0。每对象的固定头 228 字节，随后 LOD 数；每 LOD 有 28 字节头、16-bit 三角面索引、按通道分离的顶点数组和材质范围。`0x33A` 包括位置、法线、切线、副切线、颜色 UV 和光照 UV。材质范围记录为起始索引、索引数量、材质号与可选骨索引表。本版拒绝不支持的对象/通道，避免静默丢弃蒙皮。

ACPC.exe 中静态核对到 `B3D_MODEL::LoadModelBMF`、`B3D_NMF_MODEL::CreateVertexIndexBuffers`、材质 `$MATERIAL/$DIFFUSE/$BUMP/$LIGHT` 解析和对应顶点通道。没有修改 EXE，也没有在完整游戏里运行验证。

## 检索现成工具

未找到能够公开验证、直接覆盖本次 ACPC BMF 与座舱 MSH 制作流程的完整官方 SDK。找到了 3Division 的 Workers & Resources Modelviewer，官方文档说明可 OBJ → NMF，但没有找到其官方公开源码或 ACPC 兼容保证。因此交付的是独立兼容实现，不能宣称已修改官方闭源 Modelviewer。

- 官方 Modding 文档：https://wiki.hoodedhorse.com/Workers_Resources_Soviet_Republic/General_modding
- 3Division 开发者讨论 Modelviewer：https://steamcommunity.com/app/784150/discussions/1/3034852313318993860/
- 3Division 技术/项目背景：https://www.3division.net/about/index.html
- 社区 ACPC 任务脚本记录：https://koopanique.neocities.org/video_games/air_conflicts/air_conflicts_pacific_carriers
- Autodesk Edit_Normals API：https://help.autodesk.com/cloudhelp/2017/ENU/MAXScript-Help/files/GUID-9F33B780-7A98-4638-BD00-A1725FA1DB70.htm

后续若提供官方 Modelviewer 的合法可修改源码，应作为单独的适配工作进行；本次没有取得该源码，未做其二进制改版。
