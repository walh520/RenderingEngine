# Vulkan 现代光追小型渲染引擎：并行开发总计划

> 文档状态：实施基线 v1  
> 日期：2026-08-23  
> 上游目标文档：`C:\Users\nitong\Downloads\Vulkan_Modern_RayTracing_Agent_Roadmap.md`  
> 当前工程：`C:\Users\nitong\source\repos\RenderingEngine`

---

## 1. 项目定位

本项目是一套面向渲染算法学习、验证、对比和作品集展示的小型 Vulkan 光追渲染引擎。

它不是游戏引擎，也不追求玩法、复杂关卡、华丽后期或生产级资产管线。开发优先级固定为：

```text
数学与估计器正确
    > 可重复验证
    > 后端结果可对照
    > GPU 性能可测量
    > 算法展示清晰
    > 一般易用性
    > 美术包装与可玩性
```

每个典型算法都必须有一个小而明确的展示空间。展示空间只承担以下职责：

- 暴露算法解决的问题；
- 提供对照模式；
- 显示中间数据；
- 输出正确性和性能证据；
- 在固定相机、固定随机种子下复现结果。

---

## 2. 已冻结的总体决策

除非后续通过 Architecture Decision Record（ADR）明确修改，所有开发线路都必须遵守以下决定。

### 2.1 平台与工具链

- 第一阶段平台：Windows x64。
- 图形 API：Vulkan 1.3。
- 第一基准设备：NVIDIA GeForce RTX 4070 Laptop GPU。
- 窗口与输入：GLFW。
- Shader：HLSL，经 Vulkan SDK DXC 编译为 SPIR-V。
- 默认交互分辨率：1920 x 1080。
- 实时阶段最低目标：30 FPS；优化目标：60 FPS。
- 性能测试必须固定插电状态、性能模式、驱动版本、分辨率、场景版本和随机种子。

### 2.2 引擎范围

- 第一版 Primary Visibility 和 GBuffer 由 ray hit 生成。
- 不把完整 Raster Renderer 作为主线依赖；以后可加入独立 Raster 对照路径。
- 第一条硬件光追路径为 Vulkan Ray Query。
- RT Pipeline / SBT 是后续对照路径，不阻塞主线。
- Reference Path Tracer 是独立 CPU 实现，不与 GPU shader 共用核心积分代码。
- 保留 GPU Megakernel Path Tracer，作为 Wavefront 的性能与正确性基线。
- ReSTIR DI 第一版只处理 primary-hit direct lighting。
- ReSTIR DI 第一版先实现明确标注的 biased 模式；随后增加 unbiased/reference validation 模式。

### 2.3 场景与材质范围

第一阶段支持：

- glTF 2.0 静态三角网格；
- 刚体 instance 与上一帧 transform；
- position、normal、tangent、UV、index；
- base color、metallic、roughness、normal map、emissive；
- alpha mask；
- MVP 支持 smooth dielectric / glass；完整第一阶段再加入 rough dielectric BTDF；
- emissive triangle；
- point、directional、spot、finite area light；
- HDR environment map。

第一阶段明确不做：

- skeletal animation；
- morph target；
- alpha blend 透明排序；
- 毛发、布料、体积、参与介质；
- spectral rendering；
- ReSTIR GI、Path Guiding、BDPT、MLT、VCM；
- 完整材质编辑器、关卡编辑器、脚本系统和游戏逻辑。

### 2.4 依赖策略

允许使用基础设施库，核心算法保持自研。

可使用：

- GLFW：窗口、surface、输入；
- Vulkan Memory Allocator：显存分配；
- cgltf：glTF 解析；
- stb_image / TinyEXR：纹理读取与测试图像输出；
- Dear ImGui：Debug UI；
- Catch2：CPU 单元测试。

L0 必须在发布 `abi-v0` 前把 GLFW、VMA、cgltf、stb、TinyEXR、Dear ImGui、Catch2 的精确版本和 imported target 名写入 manifest。Feature 线路不得自行换库或新增替代实现。

必须自研并能够解释数学与数据流：

- Ray/Triangle、Ray/AABB；
- CPU SAH BVH；
- Flattened BVH traversal；
- GPU LBVH；
- BSDF Sample/Evaluate/PDF；
- Path Tracing、NEE、MIS、Russian Roulette；
- Wavefront queues 与 compaction；
- Temporal reprojection、SVGF；
- Reservoir/RIS/ReSTIR DI。

依赖必须由可复现的 manifest 固定版本。禁止在正常 build 中临时下载未锁定内容。

---

## 3. 最终架构与模式正交性

### 3.1 主数据流

```text
GLFW / CLI / Test Harness
          |
          v
     RuntimeConfig
          |
          v
Scene Asset -> Canonical Scene -> GPU Scene
                                |
                                v
                      Traversal Backend
                                |
                                v
Integrator -> Light Sampler -> Reconstruction -> Display / Debug / Capture
                                |
                                v
                         Metrics / Profiler
```

### 3.2 四个必须分离的算法维度

#### Traversal Backend

```text
CPU Brute Force
CPU SAH BVH
GPU Flattened SAH BVH
GPU LBVH
Vulkan Ray Query
Vulkan RT Pipeline
```

#### Integrator

```text
Whitted
CPU Reference Path Tracer
GPU Megakernel Path Tracer
GPU Wavefront Path Tracer
```

#### Light Sampling

```text
BSDF Only
NEE + Uniform Light Proposal
NEE + Power-weighted Light Proposal
NEE + MIS + Power-weighted Proposal
ReSTIR DI
```

内部配置必须把 `DirectLightingEstimator` 与 `LightProposalDistribution` 分开；`L` 键只循环常用、可复现的组合 preset。ImGui 高级面板和 CLI 可以分别指定 estimator 与 proposal，不能让 “NEE” 隐含一个未记录的 light distribution。

#### Reconstruction

```text
Raw
Temporal Accumulation
Temporal + Fixed A-Trous
SVGF
```

Backend、Integrator、Light Sampling、Reconstruction 不得被编码成一个大枚举。UI 和 CLI 操作同一个 `RuntimeConfig`，并由 capability table 判断组合是否合法。

例如：

```text
Vulkan Ray Query + Wavefront PT + MIS + Raw
GPU Flattened SAH + Megakernel PT + NEE + Temporal
Vulkan Ray Query + Wavefront PT + ReSTIR DI + SVGF
```

CPU Reference 允许只在低分辨率、离屏或固定 SPP 模式运行，不要求 1080p 交互。

---

## 4. 并行开发前必须冻结的共享合同

各 Wave 开始大量编码前，由线路 0 按 4.7 的 ABI gate 建立并版本化该 Wave 所需合同。v0 不提前猜测 v2/v3 的高级结构；其他线路不得自行修改已冻结合同含义。

### 4.1 坐标、颜色与数值合同

- 右手坐标系，`+Y` 为上，摄像机默认观察 `-Z`。
- 世界单位为米。
- CPU/GPU 矩阵存储和乘法顺序必须写入测试。
- 线性工作色域为 linear Rec.709；最终呈现才做 tone map 和 sRGB encoding。
- Radiance 不允许为负；NaN/Inf 必须被检测、计数并在 Debug 模式高亮。
- 禁止用无记录的 radiance clamp 掩盖 firefly 或错误。
- Ray origin offset 使用统一 robust helper；不能在各 shader 散落 magic epsilon。

### 4.2 Ray/Hit 合同

CPU 同步接口与 GPU 批处理接口必须分开：

```cpp
struct ICpuIntersector
{
    virtual CpuHit TraceClosest(const CpuRay&) const = 0;
    virtual bool TraceAny(const CpuRay&) const = 0;
};

struct IGpuTraversalBackend
{
    virtual void BuildOrUpdateScene(...) = 0;
    virtual void RecordTraceClosestBatch(...) = 0;
    virtual void RecordTraceAnyBatch(...) = 0;
};
```

GPU shader 内部再统一 `TraceClosest` / `TraceAny` 语义。禁止通过逐 ray dispatch/readback 模拟共同接口。

Ray/Hit ABI 至少规定：

- origin、direction、`tMin`、`tMax`；
- pixel/path index、bounce、RNG state；
- hit distance、barycentric、instance ID、primitive ID、material ID；
- front face、geometric normal、shading normal；
- miss 和 invalid 的固定表示；
- 对等命中时的稳定 tie-break 规则；
- C++/HLSL `sizeof`、alignment 和字段 offset 测试。

### 4.3 BSDF 与 LightSample 合同

每个 BSDF sample 返回：

```text
direction
value
pdf
measure
lobe flags
eta
isDelta
isValid
```

统一约定：

- radiance transport mode；
- reflection/transmission 半球；
- rough transmission 的 half-vector 和 Jacobian；
- `eta^2` 缩放；
- geometric/shading normal correction；
- lobe selection probability 必须进入总 PDF。

每个 light sample 返回：

```text
light ID / primitive ID
sample position or direction
radiance
distance
discrete light PDF
conditional area or solid-angle PDF
combined solid-angle PDF
isDelta
stable sample identity
```

### 4.4 Frame/GBuffer/History 合同

至少包含：

- current/previous view-projection；
- current/previous instance transform；
- jitter 和非 jitter 矩阵；
- linear view depth；
- world position；
- geometric/shading normal；
- albedo、roughness、metallic；
- motion vector，明确为 `previousUV - currentUV`；
- material、instance、primitive ID；
- direct diffuse、direct specular、indirect diffuse、indirect specular；
- first/second luminance moments、variance、history length；
- reservoir ping-pong 资源在 `abi-v3` 冻结，不进入 v0。

### 4.5 可重复性合同

- 第一版使用固定、可测试的 PCG32/hash RNG。
- CLI 必须接受 `--seed`。
- 同一 scene、camera、seed、resolution、SPP 必须产生可重复统计结果。
- 更换相机、场景、后端、积分器、采样器、bounce 或分辨率时自动清空相关 history。
- 算法对照必须使用相同 ray/candidate budget，而不是只比较画面。

### 4.6 Descriptor Set 逻辑所有权

为避免并行线路各自占用 binding，先冻结逻辑 set registry。精确 binding 由 L0 在 ABI 中发布：

```text
set 0  Frame / Camera
set 1  Scene / Material / Texture / Light
set 2  Traversal Backend
set 3  Wavefront Queues
set 4  GBuffer / Temporal / SVGF
set 5  ReSTIR
set 6  Debug / Profiler
```

模块可以拥有私有 binding，但不得跨越其他模块的 set。任何 bindless 或 descriptor buffer 迁移都必须先通过 ADR，不能由单个 feature 分支悄悄改变。

### 4.7 ABI 分阶段冻结

不在开工时猜完所有高级结构，而是按依赖冻结：

```text
ABI v0  坐标/颜色、Scene、Material、Light、Frame、Ray、Hit、BSDF/LightSample 数学语义、RuntimeConfig、descriptor registry
ABI v1  GPU records、GPU Scene、BSDFSample/LightSample layout、PathState、Ray/Hit/Shadow queues、IGpuTraversalBackend
ABI v2  PrimarySurface、Motion、GBuffer、History metadata
ABI v3  Reservoir、persistent LightSample、ReSTIR history
```

版本 gate 固定为：Wave 0 发布 v0；Wave 2 启动前发布 v1；Wave 3 启动前发布 v2；Wave 4 的 GPU ReSTIR 接线前发布 v3。同一时间只允许一个 ABI change in flight，模块私有结构不应提前进入公共合同。

---

## 5. 建议目录与所有权

目录按职责拆分，避免继续把所有 Vulkan、窗口、资源、积分器堆进单个 renderer 文件。

```text
RenderingEngine/
├── include/
│   ├── contracts/
│   ├── core/
│   ├── platform/
│   ├── vulkan/
│   ├── scene/
│   ├── rt/
│   │   ├── cpu/
│   │   ├── software_gpu/
│   │   └── hardware/
│   ├── bsdf/
│   ├── integrators/
│   ├── reconstruction/
│   ├── restir/
│   ├── demos/
│   └── ui/
├── src/                         # 与 include 镜像
├── resources/shaders/
│   ├── include/
│   ├── traversal/
│   ├── integrators/
│   ├── reconstruction/
│   ├── restir/
│   ├── debug/
│   └── present/
├── assets/
│   ├── generated/
│   ├── manifests/
│   └── licenses/
├── tests/
│   ├── unit/
│   ├── gpu/
│   ├── statistical/
│   ├── golden/
│   └── scenes/
├── tools/
├── docs/
│   ├── contracts/
│   ├── adr/
│   ├── proposals/L0...L10/
│   ├── handoffs/L0...L10/
│   └── benchmarks/
└── CMakeLists.txt
```

根 `CMakeLists.txt`、依赖 manifest、`include/contracts/`、`resources/shaders/include/contracts/`、正式 `docs/adr/` 和本文档只由线路 0 合并。每条线路拥有本模块的 `CMakeLists.txt`、shader manifest、test registration、`docs/proposals/Lx/` 和 `docs/handoffs/Lx/`。共享合同提案先写入自己的 proposals 子目录，由 L0 审批并编号为正式 ADR。

---

## 6. 并行线路总览

下表是 11 条长期逻辑所有权线路，不代表同时运行 11 条对话。推荐任一时刻最多并发 3～4 条实现线路，其余等待 contract/integration gate。

| 线路 | 建议分支 | 主要职责 | 可开始条件 | 最终目标 |
|---|---|---|---|---|
| L0 | `codex/rt-integration-contracts` | 基线、构建、共享 ABI、集成 | 立即 | 所有线路可稳定编译和合并的唯一合同基线 |
| L1 | `codex/rt-platform-glfw` | GLFW、Vulkan Core、资源与同步 | L0 `abi-v0` + platform seam | 可 resize、可验证、可 profile 的 GLFW Vulkan 壳 |
| L2 | `codex/rt-scene-assets` | glTF、材质、纹理、实例、场景注册 | L0 `abi-v0` | 稳定 ID 的 canonical scene 与十个展示空间 |
| L3 | `codex/rt-cpu-reference` | CPU brute force、SAH、独立 PT reference | L0 `abi-v0` | 可重复、可统计的独立正确性 oracle |
| L4 | `codex/rt-software-gpu` | Flattened SAH、Compute traversal、GPU LBVH | `abi-v1` + L2 triangle + L3 hit oracle | 可与 CPU/HW 对照的软件 GPU 后端 |
| L5 | `codex/rt-hardware` | BLAS/TLAS、Ray Query、后续 RT Pipeline | `abi-v1` + L1 Vulkan Core + L2 Scene | 稳定 Ray Query 主路径与 RT Pipeline 对照路径 |
| L6 | `codex/rt-pbr-sampling` | GPU BSDF、PT、NEE、MIS、RR、environment | `abi-v1` traversal mock + L2 lights + L3 oracle | 正确 GPU megakernel PT |
| L7 | `codex/rt-wavefront` | queues、compaction、indirect dispatch | L4/L5 traversal + L6 PT | 收敛一致且可 profile 的 Wavefront PT |
| L8 | `codex/rt-svgf` | GBuffer、motion、temporal、A-Trous、SVGF | `abi-v2` + L2 prev transform + L6 signals | 动态刚体场景稳定的 1 SPP 重建 |
| L9 | `codex/rt-restir-di` | Reservoir、RIS、temporal/spatial reuse | CPU tests 可提前；GPU 接线需 `abi-v3` + L6/L8 | 100/1k/10k 灯可验证 ReSTIR DI |
| L10 | `codex/rt-showcase-qa` | ImGui、展示空间、debug、capture、benchmark | `abi-v0` headless/mock RuntimeConfig；随后贯穿全程 | 一键切换、对照、采集证据的作品集外壳 |

线路编号是长期所有权，不代表所有线路应在第一天同时开始。实际按第 7 节的波次启动。

---

## 7. 各线路详细步骤与完成标准

## L0 — 集成、基线与共享合同

### 独占范围

- 根构建文件和依赖 manifest；
- `src/app/`、`include/app/`、`Main.cpp`、`RuntimeConfig`、capability/feature registry 和最终 renderer/pass composition；
- 迁移期的 `src/core/`、`include/core/` 与旧单体接线；
- `include/contracts/`；
- shader contract headers；
- 正式 `docs/contracts/`、`docs/adr/` 审批与版本；
- 主集成分支和 release tag。

### 步骤

1. 将当前可运行 Vulkan 状态固化为 checkpoint，保留所有用户改动。
2. 修正 Whitted/PBR 默认路径的文档漂移。
3. 建立 CMake Presets + 固定版本依赖；迁移结束后 Visual Studio solution 由 CMake 生成，各线路不得直接编辑 `.sln/.vcxproj/.filters`。
4. 预建所有模块 target、模块内 `CMakeLists.txt`、shader/test manifest 空壳；以后各线路只维护自己的模块清单。
5. 从旧单体抽出 app composition、`IWindow/IPlatformHost`、legacy Win32 adapter 和 raw input seam，确保 L1 无需修改旧单体即可实现 GLFW adapter。
6. 拆出 `RuntimeConfig`、capability table、scene/ray/hit/material/light/frame ABI，提供 headless/mock RuntimeConfig，并发布 `abi-v0`。
7. 为 `abi-v1` 提供 legacy analytic traversal adapter、固定 Hit fixture 和 `IGpuTraversalBackend` mock，使 L6 不依赖未合并的 L4/L5 branch。
8. 为 C++/HLSL record 建立 size/alignment/offset 验证。
9. 建立 contract version；不兼容修改必须提升版本并提供迁移说明。
10. 建立统一 CLI、退出码、日志、capture 和 benchmark 输出目录规范。
11. 各线路只提交 module factory/descriptor，由 L0 负责最终注册、composition 和集成。
12. 按依赖顺序集成其他线路，并维护兼容矩阵。

### 最终目标

任何一条算法线路都能只依赖稳定合同开发；合并后 Debug/Release 均构建，基础 smoke test 通过，不需要在 feature branch 重写底层 ABI。

### 合并门槛

- clean checkout 可复现配置与构建；
- Whitted baseline 与已有截图行为未无故退化；
- validation warning/error 被测试识别为失败；
- contract tests 全部通过；
- 共享文件变更附 ADR。

---

## L1 — GLFW、Vulkan Core 与资源基础

### 独占范围

- `platform/`；
- `vulkan/`；
- `resources/shaders/present/`；
- GLFW callback、raw event queue 和物理 `InputState`；不拥有语义 ActionMap；
- GPU timestamp、debug labels 的底层实现。

### 步骤

1. 用 GLFW 替换 Win32 window/raw-input 路径。
2. 通过 `glfwGetRequiredInstanceExtensions` 创建 surface，保留 Vulkan 1.3 检查。
3. 实现 cursor capture、raw mouse（平台支持时）、focus、resize、minimize、DPI 和 raw key/mouse event；不在本线路绑定 `B/I/L/N/V` 语义。
4. 抽离 Instance、Device、Queue、Swapchain、Command、Descriptor、Pipeline、Buffer、Image。
5. 引入 VMA，并区分 upload、device-local、readback、AS scratch 生命周期。
6. 建立 synchronization2 barrier helper 和每帧资源所有权。
7. 建立 shader module/pipeline cache 与 F5 shader reload。
8. 建立 timestamp query、debug utils labels、GPU marker。
9. 为后续 AS 增加 feature/property chain 和 device address 能力查询。

### 最终目标

得到一个不包含具体光追算法的稳定 GLFW Vulkan 运行壳：可 resize、可切换全屏/窗口、可热重载 shader、可输出 GPU pass timing，validation clean。

### 合并门槛

- 连续 resize/minimize/restore 测试通过；
- GLFW focus/cursor 状态无卡死；
- 两帧以上 in flight 无资源复用错误；
- Debug validation 零 error；
- 无算法代码依赖 Win32 消息。

---

## L2 — Scene、资产与展示空间数据

### 独占范围

- `scene/`；
- glTF/纹理导入；
- canonical CPU scene；
- scene registry 与 camera preset 数据；
- 资产 manifest、checksum、license。

### 步骤

1. 实现 canonical mesh、primitive、material、texture、light、instance 数据。
2. 生成稳定 `instanceID/primitiveID/materialID/lightID`。
3. 加载 glTF 2.0 静态网格、纹理、normal map、metal-roughness、emissive、alpha mask。
4. 计算缺失 normal/tangent；验证 index、NaN、退化 triangle 和空 bounds。
5. 保存 current/previous instance transform，支持刚体动画。
6. 从 emissive primitive 建立稳定 light identity，但不在本线路实现 ReSTIR estimator。
7. 加载 HDR environment，提供 luminance 数据给 L6 构建 importance distribution。
8. 实现第 11 节十个展示空间的程序化/资产配置和固定相机。
9. 所有外部资产记录来源、许可、版本和 hash。

### 最终目标

同一 canonical scene 能被 CPU reference、Software GPU、Ray Query 和 RT Pipeline 消费；重新加载后 ID 稳定，Sponza 和程序化测试空间均可复现。

### 合并门槛

- glTF fixture golden metadata 通过；
- malformed asset 明确失败而不崩溃；
- current/previous transform 测试通过；
- alpha-mask 和 two-sided 语义写入合同；
- 不在 loader 内绑定特定 traversal backend。

---

## L3 — 独立 CPU Reference 与数学验证

### 独占范围

- `rt/cpu/`；
- `bsdf/reference_cpu/` 与 CPU reference integrator；
- CPU sampling/statistical tests；
- reference EXR 与 convergence 报告。

### 步骤

1. 实现 double/float 可配置的 Ray/Triangle、Ray/AABB brute force。
2. 建立固定随机 ray corpus，覆盖平行、盒内、退化、极远距离和共享边。
3. 实现 median BVH，再实现 canonical CPU binned SAH 和 CPU traversal；GPU flattened layout 与转换器归 L4。
4. 比较 brute force 与 CPU BVH 的 hit/miss、ID、`t`、barycentric。
5. 独立实现 Lambert、GGX conductor、GGX dielectric reflection、smooth glass；随后补 rough dielectric transmission 与对应 Jacobian/eta 测试。
6. 为 Sample/Evaluate/PDF、white furnace、Fresnel 和 RR 建立统计测试；Reservoir/RIS 测试全部归 L9。
7. 实现 CPU Reference PT：emissive triangle、environment、NEE、MIS、RR。
8. 输出 EXR 和 JSON/CSV：SPP、RMSE、PSNR、seed、耗时、场景 hash。
9. 生成 Cornell、Glossy、Mirror/Glass 的 reference 集。

### 最终目标

形成独立于 GPU 实现的 correctness oracle。GPU 算法必须与它比较，不能把同一 shader 的高 SPP 输出称为唯一 Ground Truth。

### 合并门槛

- PDF histogram/积分误差在约定阈值内；
- white furnace 不无故增能；
- 1→4096 SPP 误差总体下降；
- CPU SAH 与 brute force 在固定 ray corpus 上等价；
- 相同 seed 的输出和报告可复现。

---

## L4 — Software GPU RT、Flattened SAH 与 LBVH

### 独占范围

- `rt/software_gpu/`；
- `resources/shaders/traversal/software_*`；
- software traversal counters/debug。

### 步骤

1. 定义 GPU-friendly flattened node/primitive layout，以及从 L3 canonical CPU BVH 到该布局的转换器。
2. 上传 CPU SAH 结果，实现 closest-hit/any-hit Compute traversal。
3. 实现 bounded local stack 或 stackless 方案，并检测 overflow。
4. 对固定 GPU ray buffer 与 CPU brute force 做 readback parity。
5. 增加 node tests、triangle tests、stack depth、leaf occupancy counters。
6. 实现 Morton code、radix sort、重复 Morton key tie-break。
7. 实现 Karras binary radix tree 和 bottom-up bounds。
8. 处理零尺寸 scene bounds、重复 centroid、单 primitive 和空 scene。
9. 对 CPU SAH、GPU LBVH 分别测 build time、trace time、memory、Mrays/s。

### 最终目标

提供两条稳定软件 GPU 路径：CPU SAH 上传后的高质量 traversal，以及 GPU LBVH 动态 rebuild 路径；两者与 CPU/HW hit semantics 一致。

### 合并门槛

- 固定 ray corpus parity 达标；
- overflow/invalid counter 为零或显式报告；
- 结果比较使用容差和稳定 ID，不要求随机图像逐 bit 相同；
- build 与 traversal 时间分开报告；
- 不修改积分器语义。

---

## L5 — Vulkan Hardware RT

### 独占范围

- `rt/hardware/`；
- BLAS/TLAS 资源；
- Ray Query adapter；
- 后续 RT Pipeline/SBT。

### 步骤 A：主线 Ray Query

1. 查询并启用 acceleration structure、ray query、deferred host operations、buffer device address。
2. 加载相关 function pointers 和 properties/limits。
3. 建立 vertex/index/transform/instance buffer device address。
4. 建立 BLAS size query、scratch、build、barrier、compaction 和销毁生命周期。
5. 建立 TLAS、刚体 instance update/refit/rebuild 策略。
6. 在 Compute shader 中实现 Ray Query closest-hit/any-hit。
7. 统一 alpha-mask candidate confirmation、two-sided/front-face 语义。
8. 与 CPU brute force、CPU SAH、Software GPU 对比固定 ray corpus。

### 步骤 B：后续 RT Pipeline 对照

1. 建立 raygen/miss/closest-hit/any-hit shader stages。
2. 建立 shader group、SBT alignment/stride/record layout。
3. 控制 payload/attribute 大小和 recursion depth。
4. 接入同一 GPU Ray/Hit queue ABI。
5. 与 Ray Query 比较结果、build、trace、显存和 shader 调度成本。

### 最终目标

Ray Query 成为第一条稳定硬件主路径；RT Pipeline 作为独立对照，不改变场景和材质语义。

### 合并门槛

- AS build/update 后同步正确；
- validation 零 error；
- CPU/Software/HW hit parity 达标；
- 动态刚体只更新预期 BLAS/TLAS；
- Ray Query 完成后才允许 RT Pipeline 阻塞自己的分支，不阻塞主线。

---

## L6 — PBR、经典采样与 GPU Megakernel PT

### 独占范围

- `resources/shaders/include/bsdf/` 的 GPU 侧；
- `integrators/megakernel/`；
- `resources/shaders/integrators/pbr_*`；
- light distribution GPU 数据。

### 步骤

1. 按共享合同实现 BSDF Evaluate/Sample/PDF，禁止三者漂移。
2. 实现 Lambert、GGX VNDF conductor、GGX dielectric reflection 和 smooth glass；经典 PT 稳定后补 rough dielectric transmission。
3. 实现 uniform/cosine/GGX sampling 及离线 shader readback tests。
4. 建立 one-light selection，不再在每个 shading point 线性循环全部 lights。
5. 实现 point/directional/spot/area/emissive triangle/environment sampling。
6. 明确 discrete、area、solid-angle PDF 的乘积与转换。
7. 实现 NEE + power heuristic MIS，正确处理 delta、emitter hit 和 environment hit。
8. 实现 multiple bounce、RR、NaN/Inf/negative-PDF counters。
9. 保留 Megakernel，输出 direct/indirect、diffuse/specular 分离信号。
10. 与 CPU Reference 在 Cornell/Glossy/Glass/HDRI 场景比较收敛。

### 最终目标

得到一条数学正确、可高 SPP 收敛、可选择 Software GPU 或 Ray Query traversal 的 GPU Megakernel Path Tracer。

### 合并门槛

- Sample/Evaluate/PDF 统计测试通过；
- NEE/MIS 在相同 ray budget 下优于 BSDF-only 基线；
- emitter 不重复计数；
- 没有静默 clamp；
- CPU/GPU 的 RMSE/PSNR 收敛趋势一致。

---

## L7 — GPU Wavefront Path Tracing

### 独占范围

- `integrators/wavefront/`；
- queue/compaction/indirect dispatch shader；
- queue counters 和 wavefront profiler。

### 步骤

1. 消费并验证已发布的 `abi-v1` Ray/Hit/Shadow queue records；`MaterialWorkItem` 和容量策略先保持 L7 私有，确需进入公共 ABI 时提交 `docs/proposals/L7/` 提案。
2. 实现 RayGen、Intersect、Shade、TraceShadow、NextBounce 阶段。
3. 实现 atomic append 的最小正确版本。
4. 增加 prefix sum/stream compaction 与 indirect dispatch barrier。
5. 同时支持 Software GPU 与 Ray Query traversal adapter。
6. 检测 queue overflow，不允许越界后继续渲染。
7. 记录每 bounce active paths、queue occupancy、dispatch count 和各 pass GPU ms。
8. 用相同 RNG dimension mapping 与 Megakernel 比较收敛。
9. 再评估 SoA/AoS、material sorting、persistent threads，禁止提前堆优化。

### 最终目标

Wavefront 与 Megakernel 收敛到同一参考图像，并能用 profiler 证明在目标场景上的收益或代价；不预设 Wavefront 在所有场景都更快。

### 合并门槛

- queue overflow 为零；
- indirect dispatch 同步通过 validation；
- 各 bounce active path 数可视化；
- 与 Megakernel 在相同 scene/seed/SPP/ray budget 下比较；
- Megakernel 基线不得删除。

---

## L8 — Temporal Infrastructure 与 SVGF

### 独占范围

- `reconstruction/`；
- GBuffer/history/moments/variance/A-Trous shader；
- temporal debug views。

### 步骤

1. 从 primary ray hit 输出约定 GBuffer。
2. 计算 camera 和 rigid instance motion vector。
3. 建立 history ping-pong，处理 frames-in-flight。
4. 实现 screen bounds、depth、normal、material/object ID validation。
5. 处理 camera cut、FOV、resolution、scene/backend/integrator 改变。
6. 实现 temporal accumulation、history length、first/second moments。
7. 实现短 history variance bootstrap 和 spatial variance estimate。
8. 实现 A-Trous normal/depth/luminance edge stopping。
9. 对 diffuse/specular 与 albedo demodulation/remodulation 使用明确策略。
10. 在静态、相机运动、刚体运动和 disocclusion 场景测量误差与 ghosting。

### 最终目标

在 1 SPP 输入下得到可解释、可拆解、动态稳定的 SVGF 输出，并保留 Raw、Temporal-only、A-Trous-only 对照。

### 合并门槛

- motion vector 符号和 jitter 合同有测试；
- camera cut/resize 不复用旧 history；
- 薄几何和 disocclusion 不出现大面积漏光/拖影；
- 输出 history length、moments、variance、accept/reject 原因；
- 与相同输入和高 SPP reference 比较 RMSE/PSNR。

---

## L9 — Reservoir、RIS 与 ReSTIR DI

### 独占范围

- `restir/`；
- Reservoir CPU 单测；
- ReSTIR initial/temporal/spatial shader；
- reservoir debug 和 many-lights benchmark。

### 步骤

1. 实现等权/加权 Reservoir CPU 统计测试。
2. 冻结 candidate identity、target、proposal PDF、support 和 correction weight。
3. 第一版 target 不含昂贵 visibility，最终 selected sample 做 visibility。
4. 实现 uniform、power-weighted、emissive triangle、environment candidates。
5. 实现 initial reservoir，并与 RIS reference distribution 比较。
6. 实现 temporal reprojection、normal/depth/ID/disocclusion validation。
7. 限制 `M`、history age，定义动态/删除 light 的失效规则。
8. 实现 spatial neighbor reuse、geometric similarity 和薄几何保护。
9. 第一版实现并明确标注 biased estimator。
10. 增加 unbiased/reference validation 模式并记录 visibility 成本。
11. 只作用于 primary-hit direct lighting，明确与 NEE/MIS/emitter hit 的互斥关系。
12. 比较 100、1,000、10,000 lights 下的误差、GPU ms、visibility rays 和显存。

### 最终目标

在大量动态灯、移动相机和刚体物体下，以少量 candidates/visibility rays 获得稳定 direct lighting，并能从 debug view 解释每个 reservoir 的来源与复用情况。

### 合并门槛

- Reservoir 选择频率符合理论分布；
- biased/unbiased 模式不可混名；
- target/proposal/correction weight 有公式说明；
- temporal/spatial invalidation 可视化；
- 与 uniform、power-weighted、high-SPP reference 使用相同预算比较。

---

## L10 — GLFW 操作层、展示空间、Debug UI 与 QA

### 独占范围

- `demos/`；
- `ui/`；
- semantic ActionMap、action binding 和 help overlay 的上层定义；
- screenshot、benchmark orchestration、作品集报告。

### 步骤

1. 将第 10 节键位映射到统一 `RuntimeConfig` action。
2. 实现 ImGui Algorithm、Scene、Debug、Profiler、Capture 面板。
3. 显示当前完整模式 tuple、capability 和非法组合原因。
4. 消费 L2 提供的 scene registry、固定 camera preset 和稳定 scene ID；本线路只实现选择 UI、算法说明卡和展示编排，不复制场景数据。
5. 接入所有 debug resources，但不在本线路伪造算法数据。
6. 实现一键固定 seed/camera、A/B capture、EXR/PNG、JSON/CSV 报告。
7. 实现自动 benchmark sequence 和 warm-up/median/p95 统计。
8. 维护算法完成矩阵、已知限制、资产许可和最终视频 shot list。

### 最终目标

用户无需进入代码即可切换后端、积分器、采样、重建和 debug view；每个展示空间都能一键生成可复现的正确性与性能证据。

### 合并门槛

- UI 与 CLI 走同一配置路径；
- 切换模式不会遗留错误 history；
- 不支持组合会明确禁用并解释；
- capture 文件包含完整 metadata；
- 所有算法仍可在无 ImGui/headless 测试模式运行。

---

## 8. 实际启动波次与依赖关系

### Wave 0 — 只允许先完成一次

```text
L0 baseline checkpoint + module/build skeleton + platform/composition seam + abi-v0
```

Wave 0 未完成前，其他对话只能只读研究并在对话回复中返回 proposal 草案，不写仓库。baseline checkpoint 完成、独立 feature worktree 建立后，才可把草案写入各自的 `docs/proposals/Lx/`。

### Wave 1 — 基础并行

```text
L1 GLFW / Vulkan Core
L2 Scene / Assets
L3 CPU Reference
L10 UI / Harness skeleton
```

集成门：GLFW 壳能加载 canonical triangle scene；CPU 能输出第一张 Cornell reference；CLI/capture 可用。随后 L0 根据已实现的 L1/L2/L3 最小切片发布 `abi-v1`，其中包含 GPU records、`IGpuTraversalBackend` mock/legacy adapter 和固定 Hit fixture。

### Wave 2 — 双后端 MVP 并行

```text
L4 Flattened Software GPU
L5 Hardware Ray Query
L6 GPU Megakernel PT + MIS
L10 Debug / Profiler integration
```

L6 只依赖 `abi-v1` 的 traversal mock/fixture 开发，不得直接 merge 或引用尚未集成的 L4/L5 feature branch。

集成门：同一 scene/ray corpus 在 CPU SAH、Software GPU、Ray Query 上 hit parity；Cornell/Sponza 可用 Ray Query 和 Software GPU 渲染。L0 再基于稳定 primary signals、current/previous transforms 发布 `abi-v2`。

### Wave 3 — GPU 架构与实时重建并行

```text
L4 GPU LBVH
L5 RT Pipeline / SBT
L7 Wavefront
L8 Temporal / SVGF
```

若有空闲并发槽，L9 可在自己的私有目录提前完成 CPU Reservoir/RIS 测试并提交 v3 proposal，但不得接入 GPU 公共 ABI。

集成门：Wavefront 与 Megakernel 收敛一致；1 SPP Raw/Temporal/SVGF 对照完整；Software/HW build 与 trace profiler 完整。L0 审批 L9 proposal 后发布 `abi-v3`。

### Wave 4 — Many Lights

```text
L9 ReSTIR DI
L10 Many Lights UI / capture / report
```

GPU ReSTIR 只从 `abi-v3` 开始接线。集成门：100/1k/10k 动态灯的 uniform、power-weighted、ReSTIR、reference 对照完成。

### Wave 5 — 收口

```text
所有线路修复集成问题
L10 统一展示、报告和视频
L0 release audit
```

最终 release 不引入新算法，只修 correctness、stability、documentation 和 presentation 问题。

---

## 9. 多条对话并行开发规范

### 9.1 工作区隔离

每条对话必须使用独立 Git worktree 和独立 `codex/` 分支。禁止多条对话同时编辑同一个物理 checkout。

当前 active Vulkan 源码仍包含大量 modified/untracked 文件，因此 **L0 完成 baseline checkpoint 之前禁止创建 feature worktree**。必须先证明 checkpoint 能构建、运行和回退，再让所有线路从同一个明确 commit 分叉。

迁移期的三个单体文件暂时由 L0 独占，其他线路不得继续向其中堆功能：

```text
RenderingEngine/src/renderers/VulkanWhittedRenderer.cpp
RenderingEngine/resources/shaders/whitted/WhittedTrace.hlsl
RenderingEngine/resources/shaders/pbr/PbrPathTrace.hlsl
```

L0 负责把其中代码逐步搬到已分配模块；功能线路只向自己的新目录提交实现。

建议命名：

```text
RenderingEngine-worktrees/
├── integration
├── platform-glfw
├── scene-assets
├── cpu-reference
├── software-gpu
├── hardware-rt
├── pbr-sampling
├── wavefront
├── svgf
├── restir-di
└── showcase-qa
```

### 9.2 单一集成所有者

- 只有 L0 对话负责合并到 integration/main。
- Feature 对话不自行合并其他 feature branch。
- Feature 对话不得清理、重置或覆盖其他线路的改动。
- 中央 build、contracts、公共 enum 使用 append-only ID；破坏性修改必须走 ADR。
- GPU benchmark、截图验收和性能采样串行执行；禁止多个 renderer 同时抢占同一 GPU 后再比较数据。
- 每个 worktree 使用自己的 out-of-source build 目录，禁止共享中间产物。

### 9.3 每条线路的交付包

每次请求合并必须附：

```text
1. 实现范围
2. 明确未实现范围
3. 修改文件
4. Contract version
5. Build 证据
6. Unit/statistical test 证据
7. Vulkan runtime/validation 证据
8. Visual/capture 证据
9. 性能数据与测试条件
10. 已知风险和回滚方式
```

写入本线路独占的 `docs/handoffs/Lx/<milestone>.md`。

### 9.4 冲突处理

- 需要共享合同变化时，feature 分支只提交 `docs/proposals/Lx/<proposal>.md`；正式 ADR 编号和 `docs/adr/` 只由 L0 写入。
- L0 接受后先合并 contract 变更并发布新 contract tag。
- 各 feature 分支再 rebase/merge 新 contract，不允许各自实现不同 ABI。
- Shader include 与 C++ struct 必须同时修改并通过 layout test。
- 对算法选择有争议时，保留两个显式命名模式，用 reference 数据决定，不凭视觉争论。

### 9.5 对话启动模板

给每条新对话的首条消息建议使用：

```text
你负责《VULKAN_RT_PARALLEL_DEVELOPMENT_PLAN.md》的线路 Lx。
只修改该线路独占目录；共享 contracts、根构建文件和其他线路文件只读。
开始前读取当前 contract version、相关 ADR 和上一个 handoff。
先完成该线路当前 Wave 的最小验收，不提前实现下一 Wave。
交付时写 docs/handoffs/Lx/<milestone>.md，区分静态、构建、运行、数值和视觉证据。
不要清理工作树中不属于本线路的改动。
```

---

## 10. GLFW 键位设计

键位目标是快速算法对照，而不是游戏操作。所有切换必须映射到 `RuntimeConfig`；CLI 和 UI 使用同一套状态。

### 10.1 相机与应用

| 键位 | 功能 |
|---|---|
| `W/A/S/D` | 前后左右移动 |
| `Q/E` | 下移/上移 |
| `Left Shift` | 快速移动 |
| `Left Ctrl` | 精细慢速移动 |
| 鼠标 | 捕获时观察方向 |
| 滚轮 | 调整移动速度 |
| `Alt + 滚轮` | 调整垂直 FOV |
| `Tab` | 捕获/释放鼠标 |
| `Esc` | 释放鼠标，不直接退出 |
| `Alt + F4` | 退出程序 |
| `Home` | 回到当前展示空间的固定相机 |
| `P` | 暂停/继续场景动画 |
| `O` | 暂停时推进一帧 |

### 10.2 正交算法模式

| 键位 | 正向循环 | `Shift + 键位` 反向循环 |
|---|---|---|
| `B` | Traversal Backend | 上一个 Backend |
| `I` | Integrator | 上一个 Integrator |
| `L` | Light Sampling | 上一个 Light Sampling |
| `N` | Reconstruction | 上一个 Reconstruction |
| `V` | Debug View | 上一个 Debug View |

窗口标题和 ImGui 顶部始终显示：

```text
Scene | Backend | Integrator | LightSampler | Reconstruction | Debug
Resolution | Seed | Frame | SPP | Bounce | GPU ms
```

### 10.3 调试、比较与参数

| 键位 | 功能 |
|---|---|
| `R` | 清空 accumulation、temporal history 和 reservoir history |
| `K` | 锁定/解除固定 camera + RNG seed 的 A/B 比较模式 |
| `[` / `]` | 减少/增加 maximum bounce |
| `-` / `=` | 降低/提高 exposure |
| `PageDown` / `PageUp` | 降低/提高内部 render scale |
| `F1` | 键位帮助和当前算法说明 |
| `F2` | Algorithm/Mode 面板 |
| `F3` | GPU/CPU Profiler 面板 |
| `F4` | 保存 PNG + EXR + metadata JSON |
| `F5` | 重载 shader；失败时保留旧 pipeline |
| `F6` | 开关 fixed-seed split-screen A/B |
| `F7` | 开关 debug overlay legend |
| `F8` | 执行当前场景的短 benchmark |
| `F9` | 执行当前算法的 reference comparison |

### 10.4 展示空间快捷键

| 键位 | 展示空间 |
|---|---|
| `0` | Baseline Gallery |
| `1` | Intersection & BVH Lab |
| `2` | Whitted Optics Room |
| `3` | Cornell Box |
| `4` | GGX & MIS Material Lab |
| `5` | Environment Sampling Dome |
| `6` | Sponza Traversal Hall |
| `7` | Backend Parity Benchmark |
| `8` | Temporal Stability Corridor |
| `9` | Many Lights / ReSTIR Arena |

数字键只更换 scene 和默认相机，不偷偷改变 Backend、Integrator、Light Sampling 或 Reconstruction。若当前组合不支持新场景，保持原选择并显示明确错误，不得静默切换算法。

`K` 固定的是 base seed、相机和动画起点；sample/frame index 仍必须推进 RNG sequence。禁止实时模式每帧重复同一个 1-SPP sample。

### 10.5 GLFW 与 ImGui 输入仲裁

- Scene、Backend、Integrator、Light Sampling、Reconstruction 等离散切换只响应 `GLFW_PRESS`，不响应 `GLFW_REPEAT`。
- `ImGuiIO::WantCaptureKeyboard` 为 true 时，数字场景键、`B/I/L/N/V`、参数键和相机键不得穿透。
- `ImGuiIO::WantCaptureMouse` 为 true 时，鼠标观察、滚轮速度/FOV 不得穿透。
- 鼠标已由 GLFW capture 时，ImGui 不接收相机相对移动；释放 capture 后才允许面板交互。
- 输入回调只写入 `InputState/ActionQueue`，真正的 mode 变化在每帧固定位置统一应用和 reset。

### 10.6 自动 Reset 规则

Reset 按资源分级：

```text
A   Progressive radiance accumulation
T   Temporal/SVGF history、moments、variance
Q   ReSTIR reservoirs
P   Profiler rolling statistics
AS  Acceleration structures
```

| 事件 | Reset |
|---|---|
| Scene 切换/重新加载 | `A + T + Q + P + AS` |
| Backend 切换 | `A + T + Q + P`，并建立对应 `AS` |
| Integrator 或 Light Sampling 切换 | `A + T + Q + P` |
| Reconstruction 切换或 SVGF 参数改变 | `T + P`，保留独立 Raw sample stream |
| Resolution/render scale/FOV 改变 | `A + T + Q + P` |
| Camera bookmark、Home、瞬移 | `A + T + Q + P` |
| Progressive Reference 模式下普通相机移动 | `A` |
| Realtime Temporal/SVGF 模式下连续相机移动 | 不全清；依靠 reprojection/history validation |
| 稳定 ID 的刚体、灯光连续运动 | 不全清；依靠局部 history/reservoir validation |
| 增删物体/灯、改变稳定 ID、材质拓扑改变 | `A + T + Q + P`，必要时 `AS` |
| 手动突变 base color/roughness/emission/light intensity/environment | `A + T + Q + P` |
| Seed、bounce、SPP、candidate 数改变 | `A + T + Q + P` |
| 任意 shader reload 成功 | 保守执行 `A + T + Q + P` |
| Exposure/tone map/debug/profiler/help/capture | 不 reset |

连续相机和刚体运动不能每帧清空 temporal history，否则无法验证 reprojection、SVGF 和 ReSTIR temporal reuse。UI 必须提示本次操作清空了哪些资源。

---

## 11. 典型算法展示空间

## 0 — Baseline Gallery

### 目的

保存当前解析球/平面场景，作为 GLFW/Vulkan 重构的非回归基线。

### 展示

- Whitted reflection/refraction；
- PBR sphere material；
- physical/legacy shadow 对照；
- tone mapping 和 accumulation。

### 验收

- 重构前后固定相机结果无无故变化；
- resize、input、capture、shader reload 稳定。

---

## 1 — Intersection & BVH Lab

### 空间设计

- 单 triangle、共享边 triangle、薄长 triangle；
- origin 在 AABB 内/外；
- 平行 ray、负零方向、极小/极大尺度；
- 高密度 triangle grid 和重复 centroid cluster。

### 展示算法

- brute force；
- CPU SAH；
- Flattened SAH；
- GPU LBVH；
- BVH depth/leaf occupancy/node tests/triangle tests。

### 验收

- 固定 ray corpus hit parity；
- build 和 traversal 分开计时；
- 错误 primitive 用高亮 ray 显示。

---

## 2 — Whitted Optics Room

### 空间设计

- 平面镜、镜面球；
- 空心/实心玻璃球和玻璃 slab；
- 可观察 TIR 的斜视结构；
- 一个小面积光源和清晰 occluder。

### 展示算法

- reflection/refraction；
- Fresnel/TIR；
- shadow ray；
- Beer-Lambert；
- max depth 与 ray origin offset。

### 验收

- 镜面路径和折射路径可单独 debug；
- 无 self-intersection acne；
- 不同 backend 命中一致。

---

## 3 — Cornell Box

### 空间设计

- 标准 diffuse Cornell；
- 一个 area emitter；
- 两个 block；
- 固定官方/自生成尺寸与相机。

### 展示算法

- diffuse GI 和 color bleeding；
- BSDF-only、NEE、MIS；
- RR 与 path length；
- 1→4096 SPP convergence。

### 验收

- CPU/GPU reference 对比；
- 相同 ray budget 下 NEE/MIS 方差比较；
- RMSE/PSNR 曲线随 SPP 总体改善。

---

## 4 — GGX & MIS Material Lab

### 空间设计

- roughness 横轴、metallic 纵轴的 material sphere grid；
- dielectric/conductor/glass 对照；
- 小 area light、大 area light、掠射角光源；
- white furnace 子场景。

### 展示算法

- GGX NDF/Smith/Fresnel；
- NDF sampling 与 VNDF sampling 对照；
- lobe PDF；
- light sampling/BSDF sampling/MIS；
- energy test。

### 验收

- PDF histogram；
- white furnace 不无故增能；
- roughness/highlight 变化单调且有 reference。

---

## 5 — Environment Sampling Dome

### 空间设计

- HDR dome；
- glossy、diffuse、mirror objects；
- 含小面积高亮太阳的 HDRI；
- 可旋转 environment。

### 展示算法

- uniform sphere sampling；
- luminance x sin(theta) importance sampling；
- environment PDF；
- MIS with BSDF sampling。

### 验收

- 采样 histogram 与理论 distribution 比较；
- 同预算下 firefly/variance 对比；
- environment rotation 正确重建 PDF。

---

## 6 — Sponza Traversal Hall

### 空间设计

- 固定版本 Sponza；
- texture、normal map、alpha mask；
- 多 instance 和少量刚体移动物体。

### 展示算法

- glTF scene path；
- CPU SAH、Flattened SAH、LBVH、Ray Query；
- BLAS/TLAS build/update；
- 大场景 indirect lighting；
- memory/profiler。

### 验收

- 资产 hash 和许可固定；
- alpha mask 在 Software/Ray Query/RT Pipeline 语义一致；
- build、refit、trace、memory 分项报告。

---

## 7 — Backend Parity Benchmark

### 空间设计

- 小、中、大三套程序化 mesh；
- 相同固定 ray buffer；
- 静态、刚体更新、完全 rebuild 三种 workload。

### 展示算法

- CPU brute force；
- CPU SAH；
- GPU Flattened SAH；
- GPU LBVH；
- Vulkan Ray Query；
- Vulkan RT Pipeline。
- GPU Megakernel 与 Wavefront；
- active path、queue occupancy、compaction 和 indirect dispatch。

### 验收

- hit/miss、IDs、`t`、barycentric 差异可视化；
- Mrays/s、build/refit time、memory、median/p95；
- 正确性表和性能表分开。

---

## 8 — Temporal Stability Corridor

### 空间设计

- 高对比灯光；
- 薄柱、栅栏、depth discontinuity；
- 横向移动 camera；
- 前后交叉的刚体物体；
- 反复出现的 disocclusion 区域。

### 展示算法

- motion vector；
- history validation；
- moments/variance；
- temporal-only；
- A-Trous；
- SVGF。

### 验收

- camera cut、resize、FOV change 正确清 history；
- accepted/rejected history 原因可视化；
- Raw/Temporal/A-Trous/SVGF/Reference 同屏对照。

---

## 9 — Many Lights / ReSTIR Arena

### 空间设计

- 100、1,000、10,000 emissive triangles；
- 小、大、强、弱 lights 混合；
- light position/color 动画；
- moving camera 和 rigid occluders。

### 展示算法

- uniform one-light；
- power-weighted sampling；
- RIS；
- ReSTIR initial、temporal、spatial；
- biased/unbiased validation；
- final visibility。

### 验收

- reservoir M/weight/light ID/source/reuse 可视化；
- 相同 candidate/visibility budget 对比；
- 动态 light 删除/新增不复用失效 sample；
- 误差、GPU ms、visibility rays、显存完整报告。

---

## 12. CLI 与自动化合同

交互键位不能成为唯一入口。所有关键模式都必须支持 CLI：

```text
--scene cornell
--backend ray-query
--integrator megakernel
--light-sampler mis
--light-proposal power
--reconstruction raw
--debug-view final
--resolution 1920x1080
--render-scale 1.0
--spp 4096
--max-bounce 8
--seed 1
--frames 120
--headless
--capture <directory>
--benchmark <preset>
--reference <image.exr>
```

自动化输出至少包括：

```text
image.exr
preview.png
config.json
timings.csv
counters.csv
validation.log
comparison.json
```

每份 metadata 记录：commit、contract version、GPU、driver、Vulkan SDK、resolution、scene hash、asset hash、seed、SPP、bounce、backend、integrator、sampler、reconstruction 和全部可调参数。

---

## 13. 分阶段发布目标

## Release 0 — Stable GLFW Baseline

- 当前 Whitted/PBR 行为迁移到 GLFW；
- Vulkan Core 拆分；
- canonical contracts；
- CLI/capture/validation；
- Baseline Gallery。

## Release 1 — Correct Dual-Backend Portfolio MVP

- glTF/static/rigid scene；
- CPU brute force + CPU SAH；
- Software GPU Flattened SAH；
- Vulkan Ray Query；
- independent CPU Reference PT；
- GPU Megakernel PT；
- GGX、NEE、MIS、RR、emissive triangle、HDRI；
- Cornell、Material Lab、Sponza、Backend Benchmark；
- debug views 和 GPU timing。

这是第一个推荐公开展示版本。

## Release 2 — GPU Architecture

- GPU LBVH；
- Wavefront PT；
- RT Pipeline/SBT；
- Megakernel/Wavefront/Software/HW 对照。

## Release 3 — Real-Time Reconstruction

- 1 SPP；
- ray-hit GBuffer、motion、history；
- variance、A-Trous、SVGF；
- Temporal Stability Corridor。

## Release 4 — Many Lights

- Reservoir/RIS tests；
- ReSTIR DI initial/temporal/spatial；
- biased/unbiased validation；
- Many Lights Arena。

## Release 5 — Final Research Showcase

- 十个展示空间；
- 完整 Debug UI；
- correctness/quality/performance 报告；
- 自动 capture；
- 技术说明和展示视频。

---

## 14. 统一验收规则

### 固定测试档位

```text
Correctness  256 x 256，固定 seed 集合，最高 4096 SPP
Interactive  1280 x 720，1 SPP/frame
Portfolio    1920 x 1080，最终展示与性能报告
```

性能测试默认 warm-up 120 帧、测量 1000 帧、至少重复 3 次；GPU 阶段使用 timestamp query，不能用包含 present 的 CPU wall time 替代。

初始数值门槛如下，若需调整必须保留历史结果并通过 ADR：

- CPU BVH 对 1,000,000 条固定/随机 rays：非歧义命中的 hit/miss 与 IDs 一致，`t` 相对误差不高于 `1e-5`；
- GPU Software/HW 对同一 ray corpus：非歧义命中的 hit/miss 与 IDs 一致，`t` 相对误差不高于 `1e-4`；
- 共享边、共面、等距离 primitive 等歧义 ray 使用预先定义的 equivalent-hit set，不强迫硬件 traversal 返回同一个 primitive ID；
- PDF 数值积分误差不高于 0.5%，经验频率落在约定统计置信范围；
- Validation error、queue/stack overflow、silent ray drop、NaN/Inf 均为 0；
- 绝对毫秒指标在各阶段 baseline 测完后冻结，不能在没有测量前凭空指定。

### 正确性

- 所有 sampling 算法必须有 PDF/statistical test。
- 所有 traversal backend 必须对固定 ray corpus 做 hit parity。
- stochastic integrator 以 convergence 和置信区间判断，不要求逐 pixel/逐 bit 相同。
- high-SPP GPU 输出只能是比较对象，独立 CPU Reference 才是主要 oracle。

### Vulkan 稳定性

- Validation error 为测试失败。
- GPU queue/stack overflow、NaN/Inf、negative PDF、invalid hit 必须计数。
- Resize、minimize、shader reload、scene switch、backend switch 必须有 smoke test。

### 图像质量

- 固定相机、seed、asset hash；
- 保存 Raw 和 Reference；
- 报告 RMSE、PSNR，可选 SSIM；
- temporal 算法必须分别测试 static、camera motion、rigid motion、disocclusion。

### 性能

- warm-up 后记录 median 和 p95；
- build/refit/trace/shade/denoise/present 分项；
- 记录 resolution、SPP、rays、path length、candidate、visibility rays、memory；
- 不用降低质量或改变预算伪造加速比。

### 证据边界

每个 milestone 必须区分：

```text
Static inspection passed
Build passed
CPU unit/statistical tests passed
GPU runtime passed
Vulkan validation passed
Visual acceptance passed
Performance target passed
```

未执行的层级不得写成已通过。

---

## 15. 最终 Definition of Done

只有同时满足以下条件，才称为“完整第一阶段完成”：

- [ ] GLFW Vulkan 1.3 platform 稳定；
- [ ] canonical glTF/static/rigid scene；
- [ ] 独立 CPU Reference PT；
- [ ] CPU SAH BVH；
- [ ] Software GPU Flattened SAH；
- [ ] GPU LBVH；
- [ ] Vulkan Ray Query；
- [ ] Vulkan RT Pipeline/SBT 对照；
- [ ] GGX/NEE/MIS/RR/environment/emissive triangle；
- [ ] Rough dielectric transmission 与统计/能量测试；
- [ ] Megakernel PT；
- [ ] Wavefront PT；
- [ ] 1 SPP ray-hit GBuffer/motion/history；
- [ ] SVGF；
- [ ] Reservoir/RIS 单元测试；
- [ ] ReSTIR DI initial/temporal/spatial；
- [ ] 十个算法展示空间；
- [ ] Backend/Integrator/Sampler/Reconstruction 正交切换；
- [ ] 完整 debug views；
- [ ] CPU/GPU profiler 与自动 benchmark；
- [ ] correctness、quality、performance 报告；
- [ ] 资产许可和可复现构建；
- [ ] 展示视频所需 capture 与说明。

---

## 16. 第一批并行对话的建议启动顺序

不要第一天启动 ReSTIR、SVGF 或 Wavefront 编码。第一批只启动：

1. L0：冻结当前基线、build、contracts；
2. L1：GLFW/Vulkan Core；
3. L2：canonical scene/glTF；
4. L3：CPU intersections/SAH/reference harness；
5. L10：CLI/capture/UI skeleton。

当 L0 发布 `abi-v1`，且 L1/L2/L3 的最小 vertical slice 已合并后，再启动 L4/L5/L6。这样能避免多条高级算法线路分别发明自己的 scene、ray、hit、material 和 frame layout。

项目的第一条完整证明链应当是：

```text
Cornell canonical scene
    -> CPU brute force hit
    -> CPU SAH hit parity
    -> Software GPU hit parity
    -> Vulkan Ray Query hit parity
    -> CPU Reference PT
    -> GPU Megakernel PT + MIS convergence
    -> fixed-seed image/metrics/timing report
```

这条链闭环之后，Wavefront、SVGF 和 ReSTIR 才有可信的地基。
