# 当前 PBR / Whitted 传输与执行架构（RuntimeConfig v2）

核查日期：2026-09-12。本文描述当前源码路由，不把“共享 BSDF”当作全算法等价性证明。旧 Wave 0 的运行入口、重复场景结构及独立 PbrPathTrace / WhittedTrace Shader 已退役；历史构建结果只见于历史 handoff。

## 1. 唯一配置入口

应用与 Vulkan 渲染器直接接收 `RuntimeConfig v2`，不再经过 `RunOptions` 或旧 tuple 投影。

| 旧概念 | 当前明确语义 |
|---|---|
| PBR integrator | `--transport pbr --execution staged` |
| GPU Megakernel integrator | `--transport pbr --execution megakernel` |
| GPU Wavefront integrator | `--transport pbr --execution wavefront` |
| Whitted integrator | `--transport whitted --execution staged` |
| CPU Reference integrator | `--transport pbr --execution cpu-reference`，仅受限 Cornell/headless oracle |
| Uniform / Power proposal | `--light-selection uniform\|power`，离散选择光源身份 |
| Environment Importance proposal | `--environment-sampler uniform-sphere\|importance-map`，在环境灯内采样方向 |

这是迁移说明，不是兼容别名。旧 `--integrator`、`--light-proposal`、`--light-sampler` 直接报错。

完整取值与门控以 [RuntimeConfig v2](docs/contracts/runtime-config-v2.md)、[CLI v2](docs/contracts/cli-v2.md) 和 `CapabilityTable.cpp` 为准。Whitted 只接入 Staged，不能选择 Megakernel、Wavefront 或 ReSTIR；CPU reference 不能被当作普通交互 GPU 架构。Sponza、GPU LBVH、RT Pipeline 等未接入路径继续拒绝，不替换场景或后端。

## 2. 实际执行差异

| 核查项 | PBR / Staged | PBR / Megakernel | PBR / Wavefront |
|---|---|---|---|
| 路由 | RawSeed → 每 bounce Trace/Shade → Resolve；固定像素状态 | 一个像素线程循环完成整条路径 | RayGen → Intersect → Shade → Shadow/队列压缩 → NextBounce → Resolve |
| 主源码 | `pbr_wave2_raw_seed.hlsl`、`pbr_wave2_indirect_shade.hlsl`、`pbr_wave2_indirect_resolve.hlsl` | `pbr_canonical_megakernel.hlsl` | `integrators/wavefront/shaders/` |
| BSDF | 共享 `PbrBsdf.hlsli` + `pbr_material_bsdf.hlsli` | 同一共享实现 | 同一共享实现，经 `PbrWavefrontShading.hlsli` 接入 |
| 直接光 | `PbrEstimateDirectL6` 内执行采样与可见性 | 同一直接光函数 | `WfPbrBuildDirectShadow` 构造 shadow work，后续 TraceShadow 完成可见性 |
| 选灯/环境 | 共享 `pbr_light_sampling.hlsli`，选灯与环境方向两轴独立 | 同左 | 同左；shadow 调度代码不同，不能仅凭 include 宣称数值等价 |
| 终止 | 最大 bounce、无效/非有限采样、eta-aware RR | 同类判据；循环内计数 | 同类判据；另外有队列容量/身份/fatal 检查 |
| RR 参数 | 由 host 统一提供：第 3 个表面起，存活概率夹到 0.05–0.95 | 同左 | 同左 |
| Raw | 当前仍是逐样本在线均值 | 同左 | 同左 |
| Primary surface / 重建输入 | ABI-v2 surface + 五路 radiance signals | 同类输出 | 经 Wavefront reconstruction export 发布 |
| AOV 分解差异 | 主 Raw 路径按首次采样 lobe 将间接贡献整体归入 diffuse 或 specular | 维护 diffuse/specular 两个 throughput，用 BSDF 分量拆分 | 同类双 throughput 分解 |
| 计数/计时差异 | 分阶段计数可开启；可能按需额外运行选中的辅助 AOV | 当前生产完整路径强制 no-profile 变体，避免该驱动上的高代价全局原子计数 | 自身队列与逐 bounce 计数 |

上述 AOV 差异直接见 `pbr_wave2_indirect_shade.hlsl` 的 `state.accumulatedSignal.w` 与 `Shade.hlsl` 的 `nextDiffuse/nextSpecular`。因此不要将两种“indirect diffuse”图片误当逐像素同定义的证据；需要先统一分解契约，再比较每个分量。

共享 BSDF、相同场景和相同参数是必要条件，不是“速度差只来自调度”的充分证据。比较时还需固定随机身份、后端、bounce、重建、AOV/debug、profiler 和预算，并有误差/方差结果。P0-1 不声称已经完成这种等价性验收。

## 3. Whitted 当前到底做了什么

当前 `Whitted / Staged` 先用完整材质计算局部直接光，再把后续采样允许的 lobe 限定为理想镜面反射/透射；漫反射和粗糙光泽表面不继续间接链。源码入口为 `pbr_wave2_indirect_shade.hlsl` 的 `PBR_L6_TRANSPORT_WHITTED` 分支。

它仍使用 BSDF 随机数、一次采样选择反射/透射，以及 RR。因此准确含义是 **Whitted 风格的镜面链传输**，不是“每次同时穷举反射与折射分支”的确定性递归树。它不是 PBR 的别名，也不是完整漫反射 GI 参考。经典光学场景推荐使用 NEE；其他估计器的能量一致性不能仅由配置可接受推出。

## 4. 光源联合 PDF 的配置归属

- `lightSelection` 决定离散光源 PMF，光源集合可以包含环境灯，不是仅对有限灯有效。
- `environmentSampler` 只决定环境灯内方向采样与对应 PDF；只有选到环境灯时才影响这一条件分布。
- GPU 参数分别为 `sampling.z` 与 `environment.w`；`output.z` 只表示传输模型，不承担执行架构。
- 两者应通过 `PbrSampleOneLightL6` / `PbrSelectedLightPdfL6` 合成为相同测度下的联合采样概率。
- ReSTIR 接管主表面的直接光；后续表面仍有普通 MIS。不能把“选择 ReSTIR”解释成全路径禁用 NEE/MIS。
- Sample/Eval/PDF 积分、细太阳、极点/接缝及 eta 数学复核属于后续工作，P0-1 没有用结构拆轴代替这些验证。

## 5. 当前源代码索引

| 责任 | 文件 |
|---|---|
| Host 配置、推荐配置 | `include/app/RuntimeConfig.hpp`、`src/app/RuntimeConfig.cpp` |
| Provider / tuple 拒绝边界 | `src/app/CapabilityTable.cpp` |
| Canonical 材质/几何记录 | `include/contracts/SceneAbiV0.hpp`、`src/scene/CanonicalScene.cpp` |
| 实验场景唯一构造器 | `src/scene/ExperimentScenes.cpp` |
| 生产常量、资源与架构 dispatch | `src/renderers/Wave2Runtime.cpp` |
| GPU BSDF / Eval / PDF / Sample | `resources/shaders/include/bsdf/PbrBsdf.hlsli` |
| 材质桥、eta 上下文、介质衰减 | `resources/shaders/integrators/pbr_material_bsdf.hlsli` |
| 离散选灯与环境方向采样 | `resources/shaders/integrators/pbr_light_sampling.hlsli` |
| GLFW / 同一帧循环 / 显示 | `src/renderers/VulkanWhittedRenderer.cpp` |
| HDR 显示变换 | `resources/shaders/whitted/Present.hlsl` |

## 6. P0-1 验收边界

本阶段验证配置拆分、旧入口移除、输入独立性、F11 原子性、合法/非法组合以及 Debug 构建和有界 Vulkan 运行。它不代表所有场景、算法数学、ReSTIR、重建或动态历史质量已完成验收。

Raw 与 CurrentFrame 的信号区分、相机运动历史、ReSTIR 诊断和数学 oracle 不能算本阶段完成项。CPU reference 仍只对已声明的 Cornell Lambert 传输范围提供参考，不能给所有 GGX/玻璃/多灯组合背书。
