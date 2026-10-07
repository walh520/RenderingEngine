# RenderingEngine

Vulkan 路径追踪与 ReSTIR 实验平台

> Windows x64 研究与工程演示平台；局部 Debug 验证有记录，完整性能与视觉验收仍需补齐。

[GitHub 仓库](https://github.com/walh520/RenderingEngine)

## 简介与公开范围

基于 Vulkan 1.3、C++ 与 HLSL 的渲染实验平台。公开仓库包括交互式 Debug 展示、CPU 参考工具、Vulkan smoke/validation 测试、Shader 校验、跨语言 ABI 契约与捕获元数据。定位为研究与工程演示，不承诺完整游戏引擎或生产渲染器能力。

## 本地工程与公开快照

已只读核对当前 RenderingEngine 工程与公开提交 `4f3cd6b138dc3`：公开树中的 519 个文件均在本地对应，273 个逐字节相同，246 个仅 CRLF/LF 换行不同，没有内容差异或缺失。

这一结果确认公开代码与当前工作目录的一致性，不是一次新构建或运行验证。下文保留原仓库的局部验收条件；本轮没有重新测量帧时、显存、收敛或长期稳定性。

## 演示

[作品总集](https://www.bilibili.com/video/BV1MVak6jEPv/) · [B 站主页](https://space.bilibili.com/1080308077)

目前提供的是作品总集入口，未将其中未核对的片段指定为本仓库独立演示。视频可能包含比当前仓库更多的内容；功能和验证范围以下述代码与记录为准。

## 实现与贡献

公开 README 将该项目描述为自建综合渲染引擎；本页只整理可在代码和文档中审阅的实现重点，不将通用算法或第三方库记为个人发明。

- 运行配置与能力表：分离场景、遍历、光传输、执行架构和重建选项，显式拒绝不支持的组合。
- Vulkan 渲染路径与版本化 C++/HLSL ABI：围绕数据布局、描述符和历史资源组织模块。
- ImGui 展示与 EXR/PNG/JSON 捕获：让配置、结果及验证证据可以对应。

## 核心功能

- PBR / Whitted 光传输；GGX、VNDF、NEE、MIS、透射与 Beer–Lambert 衰减。
- Linear、CPU 构建的 Flattened SAH BVH、Vulkan Ray Query 三类交互遍历路径。
- Staged、Megakernel、Wavefront 三类 GPU 执行架构。
- Current Frame、Progressive Mean、Temporal、Spatial A-Trous、SVGF 重建路径。
- ReSTIR DI 的初始采样、时空复用、Winner Visibility、诊断视图与统计回读。

## 方案与取舍

| 选择 | 目的与代价 |
| --- | --- |
| 用独立配置轴与 CapabilityTable 约束组合 | 便于同场景比较；不支持的组合会拒绝执行，不做隐式替代。 |
| CPU 参考与多条 GPU 路径并存 | 提供交叉核对入口；单个参考场景通过不能证明所有后端与场景。 |
| 用版本化 ABI 和确定性捕获组织实验 | 增加契约维护成本，换取参数与数据布局可追踪。 |
| 将未就绪资产路径设为门控 | Sponza 在许可、哈希和纹理 glTF 路径就绪前不可用。 |

源码核对入口：`CapabilityTable.cpp` 约束整组运行配置；`restir` 的蓄水池复用和可见性、`reconstruction` 的历史重投影与过滤分别有契约。GPU LBVH 与 RT Pipeline/SBT 的局部代码仍不能越过当前交互能力门控；不能用“源码目录存在”替代可选后端已打通。

## 代码阅读入口

1. [RuntimeConfig](RenderingEngine/src/app/RuntimeConfig.cpp) → [CapabilityTable](RenderingEngine/src/app/CapabilityTable.cpp)：先看可选维度与拒绝条件。
2. [renderers](RenderingEngine/src/renderers/) → [rt](RenderingEngine/rt/) → [integrators](RenderingEngine/integrators/)：理解遍历与执行的分工。
3. [reconstruction](RenderingEngine/reconstruction/) 与 [restir](RenderingEngine/restir/)：追踪历史数据和直接光复用。
4. [RuntimeConfig v2 契约](RenderingEngine/docs/contracts/runtime-config-v2.md)、[当前场景审阅](RenderingEngine/docs/current-scenes-and-algorithms-review.md)、[P0 验收记录](RenderingEngine/docs/handoffs/L0/p0-2-through-p0-4.md)：把实现与证据对应。

## 验证与性能

仓库记录了 Debug 构建、Shader/SPIR-V 校验、Vulkan validation、部分 CPU/GPU parity、固定种子 Film 一致性、ReSTIR ABI-v3 GPU oracle 与统计回读。这些属于原仓库记录，本次整理没有重新执行渲染程序或测试。

- Film 三架构一致性记录包含固定 Cornell / MIS / 640×360 / 三帧 / 最大反弹 4 的条件；不能外推动态历史或全部场景。
- ReSTIR 后续验证另有静态配置与短帧记录，需按 [P0 验收记录](RenderingEngine/docs/handoffs/L0/p0-2-through-p0-4.md) 的具体条件解释。
- 完整 1000/10000 灯性能基准、完整收敛与无偏性研究、Release 帧时/显存/长时质量仍需专门测量。
- 本页不提供未测得的 FPS、加速比或性能排名。

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

## 限制与来源许可

GPU LBVH 和 Vulkan RT Pipeline/SBT 仍属于声明或局部实现，不能表述为当前交互生产后端。Sponza 保留资产门控；完整视觉和性能验收尚未完成。

[第三方来源说明](RenderingEngine/THIRD_PARTY_NOTICES.md) 记录了 glTF、Filament、PBRT、GGX/VNDF 等参考；其中 PBR Neutral tone mapping 为 Khronos 参考实现的 HLSL 改写，保留其 Apache-2.0 来源说明。本次检查未见仓库根目录的统一 LICENSE，不据此声明整库采用某一开源许可。
