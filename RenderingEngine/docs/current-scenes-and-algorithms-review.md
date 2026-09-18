# 当前实验场景与算法清单（评审稿）

> 基线：`D:\MyProjects\RenderingEngine_Current_20260908`，2026-09-09 源码状态。
> 本文用于检查“场景为什么这样搭、推荐组合是否合理”。“推荐”指 1280×720 下适合教学展示的稳定起点，不代表绝对最高画质或最高性能。

## 1. 如何阅读状态

- **交互可用**：普通 GLFW 窗口中可通过键位切换，且 `CapabilityTable` 有生产 Provider。
- **专用路径**：有实现，但不进入普通窗口的自由切换循环。
- **已声明/门控**：枚举、模块或测试代码存在，但当前生产运行时会拒绝，不能当作已展示结果。
- 数字键 `0–9` **只切换场景**；按 `F11` 才会原子恢复当前场景的教学推荐组合。
- `F10` 会在命令行打印当前组合、推荐组合、场景目的以及匹配状态。

## 2. 十个实验场景

所有推荐组合均使用 `Final` Debug View 与 `Physical` 阴影。场景 6 是保留的未来配置，当前不可运行。

| 键 | 场景与搭建思路 | 主要考察内容 | F11 教学推荐组合 | Bounce / 参考 SPP | 当前状态 |
|---:|---|---|---|---:|---|
| `0` | **Baseline Gallery**：保留 Wave 0 的棋盘地面、背景墙、多种材质球、光滑玻璃、金属球、两盏暖/冷球形面光源和程序化天空环境。 | Wave 0 视觉连续性；PBR/Whitted、材质、阴影与累积的非回归。 | Canonical Linear + PBR + Legacy Direct + Legacy Lights + Raw | 8 / 64 | 交互可用 |
| `1` | **Intersection & BVH Lab**：单三角形、共享边四边形、极薄/极小三角形、16×8 密集网格、三角簇和大尺度三角形，并配固定命中/未命中/非法射线语料。 | 相交鲁棒性、closest/any-hit、一致性、BVH 命中对照；不是画质场景。 | Flattened SAH + PBR + NEE + Uniform + Raw | 1 / 1 | 交互可用 |
| `2` | **Whitted Optics Room**：镜面板/镜面球、实心和嵌套玻璃球、玻璃薄板、三棱柱、遮挡物、顶部软箱；另有掠射角/TIR 机位。 | 确定性反射、折射、Fresnel、全反射、Beer–Lambert 吸收与 ray offset。 | Ray Query + Whitted + NEE + Uniform + Raw | 12 / 64 | 交互可用 |
| `3` | **Cornell Box**：固定相机的标准封闭漫反射箱和面光源，复用 canonical Cornell 数据。 | 漫反射多跳 GI、NEE/MIS、俄罗斯轮盘与收敛；CPU 参考也只接受此场景。 | Flattened SAH + PBR + MIS + Uniform + Raw | 8 / 4096 | 交互可用 |
| `4` | **GGX & MIS Material Lab**：3 行×5 列球阵列，分别覆盖 dielectric / 0.5 mixed metal / conductor 与 roughness 0.05–0.90；附 rough glass、小/大/掠射面光源和白炉机位。 | metallic-roughness、GGX/Smith/Fresnel、VNDF、粗糙透射、PDF/MIS、白炉能量检查。 | Ray Query + GPU Megakernel + MIS + Power + Raw | 8 / 1024 | 交互可用 |
| `5` | **Environment Sampling Dome**：漫反射、光泽、镜面三球放在基座上；程序化 128×64 HDR 经纬环境包含亚角度高亮太阳和宽地平线。 | Uniform Sphere 与 `luminance × sin(theta)` 环境重要性采样的方差差异，以及 BSDF/MIS 配合。 | Ray Query + PBR + MIS + Environment Importance + Raw | 8 / 1024 | 交互可用 |
| `6` | **Sponza Traversal Hall**：设计目标是大规模建筑几何、alpha mask、构建/遍历/显存与后端一致性。 | 大场景遍历、透明裁剪 any-hit 与 AS 压力。 | Ray Query + GPU Wavefront + MIS + Power + Raw | 8 / 256 | **门控**：缺固定授权资产、哈希归档、纹理 glTF 导入与生产 Provider；拒绝替代场景 |
| `7` | **Backend Parity Benchmark**：低模棱柱/盒体、中等细分球、6×6 密集重复盒体形成三种复杂度岛，并提供固定相机/固定射线。 | 先做命中正确性，再独立比较不同遍历后端的 build/trace；不能把画质差异和性能差异混为一谈。 | Canonical Linear + PBR + NEE + Uniform + Raw | 4 / 64 | 交互可用 |
| `8` | **Temporal Stability Corridor**：长走廊、重复细遮挡片、五条顶灯和带 current/previous transform 的横向移动面板；另有 disocclusion 观察机位。 | motion vector、重投影、遮挡显露、历史拒绝、Temporal/A-Trous/SVGF 稳定性。 | Ray Query + GPU Wavefront + MIS + Power + SVGF | 6 / 64 | 交互可用 |
| `9` | **Many Lights / ReSTIR Arena**：程序化生成 100/1000/10000 灯光档位，支持灯光、刚体遮挡物和相机运动。 | Uniform/Power/ReSTIR DI 等预算比较；reservoir 初始采样、时域/空域复用、历史身份与重影拒绝。 | Ray Query + GPU Wavefront + ReSTIR DI + Power + SVGF | 4 / 256 | 交互可用 |

### 场景 9 的 F11 固定参数

- 100 lights；Temporal-Spatial reuse；Explicitly Biased。
- 1 个 initial candidate；5 个 spatial neighbors；`M = 32`；history age = 20。
- comparison candidate/visibility budget = `8/1`。
- 灯光动画和刚体遮挡物动画关闭，以便先获得稳定、易复习的结果。

## 3. 当前算法总表

### 3.1 光线遍历 / 光追后端

| 算法 | 核心思路 | 当前接入状态 | 普通窗口切换 |
|---|---|---|---|
| Canonical Linear（`LegacyAnalyticGpu`） | GPU 对 canonical triangle stream 做线性遍历；结构简单，适合作为小场景基线。 | 交互可用 | `B` |
| CPU Brute Force | CPU 对每条射线测试全部 primitive，主要作为最朴素命中 oracle。 | 已声明/测试用途；无 mixed-runtime Provider | 否 |
| CPU SAH BVH | CPU 构建并遍历 Surface Area Heuristic BVH。 | 专用于 Cornell CPU Reference 的 headless tuple | 否 |
| GPU Flattened SAH BVH | CPU binned-SAH 构建后扁平化，compute shader 栈式遍历。 | 交互可用 | `B` |
| GPU LBVH | Morton code + GPU 层次构建，偏重快速动态 rebuild。 | 已声明/模块代码存在，生产 Provider 门控 | 否 |
| Vulkan Ray Query | 在 compute/shader 内联调用硬件 AS 遍历。 | 交互可用，要求设备支持相应 Vulkan 特性 | `B` |
| Vulkan RT Pipeline / SBT | 独立 raygen/miss/hit shader 与 Shader Binding Table。 | 已声明/模块代码存在，生产 Provider 门控 | 否 |

场景语料还覆盖 closest-hit、any-hit、miss、非法射线、共享边、极薄/极小三角形和 origin-inside-AABB 等边界，但它们是遍历语义，不是独立 UI 后端。

### 3.2 积分器

| 积分器 | 核心思路 | 当前接入状态 | 普通窗口切换 |
|---|---|---|---|
| PBR Path Tracer | 多跳 Monte Carlo 路径追踪，统一材质/BSDF、发光体与环境命中、直接光和间接光。 | 交互可用 | `I` |
| Whitted | 递归/迭代追踪确定性镜面反射和折射，适合镜面光学，不是漫反射 GI 参考。 | 交互可用 | `I` |
| CPU Reference Path Tracer | 独立、可复现的 CPU Lambert 路径追踪，用作数值/图像参考。 | 专用 headless：Cornell + CPU SAH + MIS + Uniform + Raw + 非零 SPP | 否 |
| GPU Megakernel | 一次 GPU dispatch 内完成完整 path loop；实现直接，但长路径与分支发散会降低占用率。 | 交互可用 | `I` |
| GPU Wavefront | 将 ray generation/intersect/shade/shadow/next bounce 分阶段，以队列压缩和 indirect dispatch 运行。 | 交互可用 | `I` |
| High-SPP CPU Reference | 使用更高 SPP 的 CPU 参考预算生成低噪声对照图。 | 展示/离线参考概念，不是第五个 RuntimeConfig integrator | 否 |

### 3.3 直接光估计

| 算法 | 核心思路 | 当前接入状态 | 键位 |
|---|---|---|---|
| Legacy Analytic Direct | Wave 0 兼容直接光路径。 | 交互可用 | `L` |
| BSDF-only | 只从 BSDF 方向提议直接光路径；小光源通常噪声很高。 | 交互可用 | `L` |
| NEE | 每个非 delta 顶点显式抽一盏灯并做可见性射线。 | 交互可用 | `L` |
| MIS | 将 BSDF proposal 与 light proposal 用兼容 PDF 和 power heuristic 合并。 | 交互可用 | `L` |
| ReSTIR DI | 以 reservoir 对大量直接光候选做重要性重采样，并可复用时域/空域候选。 | 交互可用；与普通 NEE/MIS 对 primary direct light 互斥 | `L` |

### 3.4 光源 proposal 分布

| 分布 | 核心思路 | 当前接入状态 | 键位 |
|---|---|---|---|
| Legacy Analytic Lights | Wave 0 兼容灯光选择。 | 交互可用 | `Ctrl+L` |
| Uniform One-light | 在灯光集合中等概率选择一盏，并除以选择 PDF。 | 交互可用 | `Ctrl+L` |
| Power-weighted One-light | 按光功率构建离散分布，减少亮度差异很大时的方差。 | 交互可用 | `Ctrl+L` |
| Environment Importance | 按环境贴图 `luminance × sin(theta)` 分布抽样方向。 | 交互可用 | `Ctrl+L` |

### 3.5 重建 / 降噪

| 模式 | 核心思路 | 当前接入状态 | 键位 |
|---|---|---|---|
| Raw | 不做时域或空间重建，直接显示当前路径追踪累积结果。 | 交互可用 | `N` |
| Temporal Accumulation | 用 motion/几何身份重投影并累计兼容历史；disocclusion 和 tuple 变化必须拒绝旧历史。 | 交互可用 | `N` |
| Fixed A-Trous | 以固定多尺度 5×5 B3-spline schedule 做 edge-aware 空间滤波。 | 交互可用 | `N` |
| SVGF | Temporal moments + variance estimate + A-Trous，并拆分/重调制 diffuse/specular 信号。 | 交互可用 | `N` |

说明：`Temporal Fixed A-Trous` 在当前枚举中是一个完整展示模式；它属于统一的重建资源链并使用边缘停止/方差指导，不能把它理解成普通的单次图像模糊。

### 3.6 PBR / BSDF 与路径传输细节

以下是 PBR 路径内部实际采用的算法，不是独立的顶层“积分器按钮”：

| 模块 | 当前算法 |
|---|---|
| 材质参数化 | glTF 风格 metallic-roughness；base color、metallic、roughness、emissive、transmission、IOR、attenuation |
| 漫反射 | Lambert，cosine-weighted hemisphere sampling |
| 粗糙反射 | Cook–Torrance microfacet；GGX/Trowbridge-Reitz NDF |
| 几何遮蔽 | height-correlated Smith visibility / masking-shadowing |
| Fresnel | dielectric F0 + Schlick approximation；金属由 base color 控制 F0 |
| GGX 采样 | Heitz isotropic GGX Visible Normal Distribution Function（VNDF） |
| 光滑介质 | delta reflection/refraction、Snell、Fresnel、TIR |
| 粗糙介质 | rough dielectric reflection/transmission，包含 half-vector Jacobian 与 radiance transport `eta²` 处理 |
| 吸收 | Beer–Lambert attenuation |
| 路径终止 | 最大 bounce + Russian roulette |
| 发光体命中 | emitter/environment hit weighting，并与 NEE/MIS 所有权去重 |
| 随机序列 | counter-based Philox，维度按 sample/bounce/用途固定分配 |
| 灯光离散采样 | Uniform 或 power-weighted alias/discrete distribution |
| 输出信号 | direct/indirect × diffuse/specular 拆分，并导出 primary surface 给重建/ReSTIR |

### 3.7 ReSTIR DI 子算法

| 子阶段 | 作用 |
|---|---|
| RIS / Initial Sampling | 从新抽取的 light candidates 中按 target/proposal 权重保留一个样本和 reservoir 统计量。 |
| Temporal Reuse | 重投影上一帧兼容 reservoir；相机移动、disocclusion、scene/config generation 改变时拒绝陈旧历史。 |
| Spatial Reuse | 从几何兼容邻域合并 reservoir；邻居数量和 bias mode 必须明确。 |
| Visibility | 对最终 winner 执行阴影/可见性测试，并发布 direct diffuse/specular。 |
| Bias modes | Explicitly Biased 用于实时稳定展示；Unbiased Reference 作为单独标识的参考模式，二者不能混写。 |

### 3.8 阴影与调试输出

- 阴影模式：PCF、PCSS、Physical，使用 `Alt+L` 切换。教学推荐全部采用 Physical。
- 已接入 Debug View：Final、Base Color、Normal、Roughness、Metallic、Emissive，使用 `V` 切换。
- 已声明但当前生产渲染器未附着：Reservoir M、Weight、Light ID、Source、Reuse、Rejection、Winner Visibility；选择这些资源应 fail-closed，而不是伪造图像。

## 4. 推荐组合合理性初评

| 场景 | 初评 | 需要注意的解释边界 |
|---:|---|---|
| 0 | 合理 | 这是 Wave 0 视觉基线，不是现代采样器最佳质量组合；Legacy Direct/Legacy Lights 是刻意保留。 |
| 1 | 合理 | Bounce=1 能隔离遍历问题。若做严格 backend parity，应固定相机/seed 后再用 `B` 对比，而不是只看推荐后端。 |
| 2 | 合理 | Whitted + 高 bounce 最能放大镜面/折射链；Ray Query 只负责加速，不改变光学模型。 |
| 3 | 合理 | Cornell + MIS 是经典 GI 收敛组合；4096 参考 SPP 是离线参考预算，不能要求交互立即达到。 |
| 4 | 合理 | Megakernel 便于观察完整 BSDF/MIS 路径，Power proposal 适合三盏功率/尺寸不同的灯。白炉严谨检查仍应切专用 variant/参考记录。 |
| 5 | 合理 | 场景本身专门构造高动态太阳，Environment Importance 是核心被测项。 |
| 6 | 设计合理但未落地 | 在资源和 alpha-texture glTF 链完成前，任何“能切出来”的替代画面都不应算 Sponza 结果。 |
| 7 | 基线合理 | F11 选 Canonical Linear 是 reference anchor，不是宣称它最快；本场景的完整教学动作必须继续用 `B` 循环后端。 |
| 8 | 合理 | F11 直接给 SVGF 是“稳定展示”入口；研究滤波得失时必须用 `N` 对照 Raw/Temporal/A-Trous。 |
| 9 | 合理 | 100 灯、静态负载先隔离 ReSTIR 本身；要检验重影与历史拒绝，再显式开灯光/遮挡动画，而不是把动画默认打开。 |

总体上，当前推荐表适合作为“每个场景第一次打开时不出错、且主题清晰”的教学起点。它不是自动 benchmark 矩阵：场景 1/7 的后端比较、场景 3/4/5 的采样比较、场景 8 的重建比较、场景 9 的灯数和复用比较，都仍需在固定条件下手动切换单一轴。

## 5. 最短使用流程

1. 按 `0–9` 进入场景。
2. 按 `F11` 恢复该场景教学推荐组合。
3. 按 `F10` 检查当前组合、推荐 stable ID 和匹配/偏离状态。
4. 每次只改一个轴：`B` 后端、`I` 积分器、`L` 直接光、`Ctrl+L` proposal、`Alt+L` 阴影、`N` 重建、`V` Debug View。
5. 反向循环使用对应的 `Shift` 组合；`Home` 只恢复场景相机；`R` 清 accumulation/temporal/reservoir 历史。
6. 做公平比较时用 `K` 锁定相机、seed 和动画原点，并记录 SPP、分辨率、bounce 与运行 generation。

## 6. 事实来源

- 场景注册与几何：`src/scene/ExperimentScenes.cpp`、`include/scene/ExperimentScenes.hpp`
- 顶层算法枚举：`include/app/RuntimeConfig.hpp`
- 生产可用性：`include/app/CapabilityTable.hpp`、`src/app/CapabilityTable.cpp`
- 模块接入说明：`src/app/IntegratedModuleRegistry.cpp`
- 推荐组合：`docs/contracts/scene-recommendations-v1.md`
- 按键：`src/ui/ActionMap.cpp`
- PBR/BSDF：`resources/shaders/include/bsdf/PbrBsdf.hlsli`、`resources/shaders/integrators/pbr_megakernel.hlsl`
- 重建与 ReSTIR：`resources/shaders/reconstruction/`、`resources/shaders/restir/`
