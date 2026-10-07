# RenderingEngine

Vulkan 路径追踪与 ReSTIR 实验平台

> 在同一运行时中探索光传输、遍历后端、GPU 执行架构与时域重建。

[GitHub 仓库](https://github.com/walh520/RenderingEngine)

## 项目简介

基于 Vulkan 1.3、C++ 与 HLSL 的 Windows x64 渲染实验平台。项目将交互式展示、CPU 参考实现、多条 GPU 渲染路径和捕获工具放在同一套配置体系中，便于比较算法与追踪结果。

仓库包含渲染代码、Vulkan smoke/validation 测试、Shader 校验、跨语言 ABI 契约和捕获元数据。

## 实现与贡献

项目实现围绕渲染路径组织、数据契约和实验工具展开：

- 运行配置与能力表：分离场景、遍历、光传输、执行架构和重建选项，集中管理可用组合。
- Vulkan 渲染路径与版本化 C++/HLSL ABI：组织数据布局、描述符和跨帧历史资源。
- ImGui 交互展示与 EXR/PNG/JSON 捕获：记录运行参数、输出结果和验证数据。

## 核心功能

- PBR / Whitted 光传输；GGX、VNDF、NEE、MIS、透射与 Beer–Lambert 衰减。
- Linear、CPU 构建的 Flattened SAH BVH、Vulkan Ray Query 三类交互遍历路径。
- Staged、Megakernel、Wavefront 三类 GPU 执行架构。
- Current Frame、Progressive Mean、Temporal、Spatial A-Trous、SVGF 重建路径。
- ReSTIR DI 的初始采样、时空复用、Winner Visibility、诊断视图与统计回读。

## 方案与取舍

| 选择 | 作用 |
| --- | --- |
| 独立配置轴与 CapabilityTable | 在同一场景下比较不同算法，并明确各组合的运行条件。 |
| CPU 参考与多条 GPU 路径并存 | 提供像素、采样和遍历结果的交叉核对入口。 |
| 版本化 ABI 与确定性捕获 | 追踪参数、数据布局和跨帧状态，支持复现实验。 |
| 资产加载门控 | 在资源许可、哈希和加载路径就绪后开放对应场景。 |

当前交互后端以 CapabilityTable 为准。GPU LBVH 与 Vulkan RT Pipeline/SBT 处于局部实现阶段；Sponza 的资产与纹理 glTF 加载路径仍保留门控。

## 代码阅读入口

1. [RuntimeConfig](RenderingEngine/src/app/RuntimeConfig.cpp) → [CapabilityTable](RenderingEngine/src/app/CapabilityTable.cpp)：运行配置与可用组合。
2. [renderers](RenderingEngine/src/renderers/) → [rt](RenderingEngine/rt/) → [integrators](RenderingEngine/integrators/)：遍历、光传输与执行架构。
3. [reconstruction](RenderingEngine/reconstruction/) 与 [restir](RenderingEngine/restir/)：历史数据、重建与直接光复用。
4. [RuntimeConfig v2 契约](RenderingEngine/docs/contracts/runtime-config-v2.md)、[当前场景与算法](RenderingEngine/docs/current-scenes-and-algorithms-review.md)、[P0 验证记录](RenderingEngine/docs/handoffs/L0/p0-2-through-p0-4.md)。

## 验证状态

仓库已有 Debug 构建、Shader/SPIR-V 校验、Vulkan validation、部分 CPU/GPU parity、固定种子 Film 一致性、ReSTIR ABI-v3 GPU oracle 与统计回读记录。

- Film 三架构一致性记录采用 Cornell / MIS / 640×360 / 三帧 / 最大反弹 4。
- ReSTIR 的静态配置与短帧测试条件见 [P0 验证记录](RenderingEngine/docs/handoffs/L0/p0-2-through-p0-4.md)。
- 完整 1000/10000 灯基准、收敛与无偏性研究，以及 Release 帧时、显存和长期画质评估仍待完成。

## 依赖与运行方式

- Windows x64；Visual Studio 的 v145 C++ 工具集与 Windows SDK。
- Vulkan SDK，并设置 `VULKAN_SDK`；GPU/驱动支持 Vulkan 1.3、dynamic rendering、synchronization2 与 RGBA32F storage image。
- 依赖版本以 [vcpkg.json](vcpkg.json) 与 [vcpkg-configuration.json](vcpkg-configuration.json) 为准，含 GLFW、VMA、cgltf、stb、tinyexr、ImGui、Catch2。
- 正式构建入口是 Visual Studio solution / MSBuild，仓库不使用 CMake。

在仓库根目录执行，MSBuild 路径按本机安装位置调整：

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\RenderingEngine.sln /m:1 /p:BuildInParallel=false /p:UseMultiToolTask=false /p:CL_MPCount=1 /p:Configuration=Debug /p:Platform=x64
.\bin\x64\Debug\RenderingEngine.exe --frames 120
.\RenderingEngine\tools\Validate-Pbr.ps1
```

交互：W/A/S/D、Q/E 移动；B 切遍历；I / Ctrl+I 切光传输 / 执行架构；N 切重建；0–9 选场景；R 重置历史；P/O 暂停/单步；F4 捕获；F5 重载 Shader。完整按键以当前仓库说明为准。


## 来源与许可

[第三方来源说明](RenderingEngine/THIRD_PARTY_NOTICES.md) 列出了 glTF、Filament、PBRT、GGX/VNDF 等参考。PBR Neutral tone mapping 为 Khronos 参考实现的 HLSL 改写，保留 Apache-2.0 来源声明。仓库根目录暂未提供统一 LICENSE。
