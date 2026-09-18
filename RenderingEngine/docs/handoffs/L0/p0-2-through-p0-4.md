# P0-2～P0-4：生产实验闭环（进行中）

工作树：`D:\MyProjects\RenderingEngine_Current_20260908`。日期：2026-09-12。

范围以 2026-09-09 源码审查回复中的编号为准，而非把原审查文档 P0-A～E 顺次套成数字。
P0-1 已单独交付；本记录不以 P0-1 的构建或短帧出图替代以下验收。

## 完成要求与证据清单

| 工作包 | 必须落地的行为 | 所需证据 | 当前状态 |
|---|---|---|---|
| P0-2 信号 | 独立 Current Frame Noisy、Progressive Mean、Temporal、A-Trous、SVGF；明确 temporal+A-Trous 与 spatial-only | 同一帧输入、五路 AOV 求和、逐帧统计与真实 GPU 图像 | 待实现/核验 |
| P0-2 计数 | 分开本帧 paths/pixel、film SPP、reference SPP、temporal history、reservoir M/age/candidates、visibility/total rays | F10/HUD 与实际 GPU 或 recorder 来源；缺测量明确 unavailable | 待实现/核验 |
| P0-2 历史 | 普通相机运动重投影；Home/cut/身份或不兼容配置变化强重置；film 与 temporal/reservoir 分离 | 移动、静止、cut 事件序列与 history rejection 实测 | 待实现/核验 |
| P0-2 Motion | 场景 8 真实 current/previous 物体变换进入 guide/motion；不以 identity 假充动画 | 对应表面点投影、动态 GPU 图与遮挡拒绝 | 待实现/核验 |
| P0-3 能力 | ReSTIR 明确 opaque metallic-roughness 范围，其他材质不冒充已支持 | 材质边界与主直接光路径所有权测试 | 待实现/核验 |
| P0-3 诊断 | 真实 M、Weight、Light ID、Source/Reuse、Reject、Winner Visibility 键位到最终显示 | 生产资源、图像和 GPU 数据回读一致 | 待实现/核验 |
| P0-3 消融 | Initial-only、Temporal-only、Spatial-only、Temporal+Spatial，Biased/Reference Correction | pass/barrier 与运行证据；M=1 无复用退化；独立 PT/MIS 参考 | 待实现/核验 |
| P0-3 命名预算 | 未证无偏的模式改称 Reference Correction；8/1 是比较预算而非实际工作量 | CLI/UI/metadata 与实际计数分离 | 待实现/核验 |
| P0-4 场景 1 | 固定射线与等价 HitRecord 对比 | 相同 corpus 的 CPU/已运行 GPU 后端误差报告 | 待实现/核验 |
| P0-4 场景 4 | 材质阵列、White Furnace、粗糙透射及已有变体可达 | 可操作入口、实际场景/灯光/相机变化、数值实验 | 待实现/核验 |
| P0-4 场景 5 | Uniform Sphere/Importance、太阳对齐/极点/接缝 | 变体可达、Sample/Eval/PDF 与环境积分/接缝/极点测试 | 待实现/核验 |
| P0-4 场景 8 | Camera-only、Object-only、Disocclusion、Camera Cut 可重复时间轴 | 实际动画、运动矢量、history length/rejection | 待实现/核验 |
| P0-4 场景 9 | RIS/Temporal/Spatial/Bias/SVGF 消融 | 独立预设、明确目标量、计数和未滤波参考 | 待实现/核验 |
| P0-4 交互 | 全部已有变体可达；F11 仅 Presentation 推荐，增加 Diagnosis/Benchmark；F10 显示实际 tuple/变体/预算/历史 | 输入/FIFO/原子恢复测试与真实窗口显示 | 待实现/核验 |
| 全局回归 | 1280×720 场景 0/4/5/8/9 无黑屏、错误残影、Profiler 拒绝和 Vulkan validation 错误 | Debug 解决方案、相关测试及静态/运动/切换验证矩阵 | 待实现/核验 |

## 不扩展的范围

不新增编号 10～14、美术资产、大型 Sponza 导入、BDPT/SPPM/VCM/MLT 或全局 CPU 真值。
不把 ray-tap PCF/PCSS 宣传成 shadow-map 算法。不增加旧枚举/CLI 的兼容映射。
保留已有工作树修改；源代码删除必须有明确替代责任，不以隐藏失败路径充当实现。

## 本轮进展

- 已恢复原审查回复的精确 P0-2～P0-4 范围并建立上述逐项验收清单。
- 主代理负责信号/历史主链、运行配置/交互与最终集成。
- 独立子任务：ReSTIR 生产 Shader 诊断与范围、场景纯构建变体、历史数据流只读核查。
- 当前未把任何后续包标为完成；每项须补齐与其范围相称的证据。

## 2026-09-12 续轮：SPP 交互与测试证据修正

- 正常文件审批已恢复放行；此前用量拒绝不再作为当前阻塞。
- 修复目标 Film SPP 与 F11 的职责冲突：CLI 启动拒绝非 Final / Progressive Mean 的非零 `--spp`；CapabilityTable 不再将保存的 Film 目标作为运行中算法切换锁。
- 运行中切到其他重建或调试视图时保留目标数值，但停用 Film 终止条件；恢复 Final / Progressive Mean 后重新生效。未增加旧 CLI 兼容映射。
- F11 原子恢复及独立 Debug 切换的 Showcase 回归通过。Debug x64 完整解决方案构建 exit 0，内置 Contracts 检查通过；Showcase 单独运行 exit 0。
- 修复 L9 Vulkan 测试中 productionNeighbors 局部变量重定义、五组生产 pipeline/layout 未释放，以及销毁阶段 validation 不计入测试结果的问题。
- L9 测试在 RTX 4070 Laptop GPU 上 exit 0，validation enabled，50167 assertions / 0 failures；此次输出无销毁阶段 Vulkan validation 错误。
- 重要证据边界：现有 VulkanSmoke 已创建 ABI-v3 fixture / pipeline，但 RecordAndSubmit 仍只 dispatch 旧 stages_，ReadBack 仍只检查旧 ABI。上述绿色结果不是生产 V3 的 M=1、零目标、无复用或 Debug 数值证明；生产 V3 dispatch、同步和数值断言仍待完成。
- P0-2 的真实 Film 图像均值、完整计数与动态物体 Motion，P0-3 的完整主机入口和材质所有权，P0-4 的变体交互入口及整体验收仍未完成。不得以本节局部通过替代总验收。

## 2026-09-12 续轮：生产 V3 GPU 数值证据

- 本轮补齐 VulkanSmoke 的真实 productionStages_ dispatch、计算读写 barrier、Debug 图像传输与 host readback，不再只有管线创建。
- RTX 4070 Laptop GPU、validation enabled 下生产 ABI-v3 检查通过：单候选 t=2/q=0.25 的 M=1、weightSum=8、W=4；合法零目标 M=1/W=0；无效表面 M=0；关闭 Temporal 的完整 reservoir 一致性；零 Spatial 来源的 sample/weight/M 不变；可见/遮挡输入及 Debug buffer/image 对应。
- 可见性由固定 GpuHitQueueRecordV1 输入提供，这是 WinnerResolve 的数值测试，不是真实 traversal、动态复用或独立 PT/MIS 图像参考验收。Debug 图像目前只验证 WinnerVisibility 模式，其他模式仍需独立覆盖。
- RuntimeConfig、ReSTIR 运行模式、帧图、Capture metadata 和 ManyLightsWave4 模式统一命名 ReferenceCorrection / reference-correction；删除 CLI 的 unbiased-reference 与 unbiased 别名，并增加拒绝测试。枚举数值不变，没有 ABI 布局变更。
- 剩余命名审查：ShowcaseProgram 的 ReSTIR comparison bias classification 仍使用 Unbiased；不能把常规 Uniform/Power 的无偏分类一起盲目替换。须单独补 ReferenceCorrection 分类并更新对应契约。
- 完整 Debug x64 solution build exit 0（内置 Contracts 通过）；Showcase exit 0；ReSTIR exit 0，生产 V3 oracle PASS，执行及销毁阶段无 validation 错误。

## 2026-09-12 续轮：Spatial-only 主程序接入

- ShowcaseProgram 将 ReSTIR 的第二种 comparison classification 改为 ReferenceCorrection；Uniform/Power 的 Unbiased 分类保留。Wave4Acceptance 的 ReSTIR bias 枚举、判定和序列化同步改名，对应展示测试通过。
- RuntimeConfig 新增独立 Spatial 阶段，CLI `--restir-stage spatial`、状态/捕获序列化、CapabilityTable 和生产 runtime 均接入。新增明确的 UsesRestirTemporalReuse / UsesRestirSpatialReuse 判定，删除“非 Initial 即 Temporal”和“Spatial 必须依赖 Temporal”的旧逻辑。
- 契约测试覆盖四种阶段开关矩阵；Spatial-only 帧计划 ready/valid、无历史读取槽、保留 5 个空间邻居。完整 Debug x64 构建及 Contracts 通过；Showcase exit 0。
- 第一次旧二进制运行因上述旧依赖拒绝，未算通过。删除依赖并重建后，1280×720、Many Lights、Ray Query、Wavefront、Current Frame、Spatial-only / 5 neighbors / 1 candidate 的 explicitly-biased 与 reference-correction 均完成 2 帧，退出 0。
- reference-correction 完整输出检查 VUID、Validation Error、Runtime failure、Profiler 拒绝匹配数为 0。此证据只证明短帧主链可运行，未检查图像质量、空间复用数值或动态历史。
- F1 重置说明修正为普通相机移动清 Film、Home/cut 强清历史，不再描述旧的移动即全清策略。
- 待完成：场景变体 CLI/键位入口、Diagnosis/Benchmark、各 Debug 模式可视化与数值、Film/路径/history 各自计数、动态物体 Motion 以及完整验收矩阵。整体目标仍未完成。

## 2026-09-12 续轮：场景变体 CLI 入口

- RuntimeConfig 保存 sceneVariant；新增 `--scene-variant ID`，Application 和 renderer 的 BuildExperimentScene 使用同一个 ID。F10/status 与 Capture metadata 展示/保存 ID。
- 删除 renderer 中始终重选 variants.front() 的 ApplyRuntimeSceneVariant 旧逻辑，由场景构建器唯一负责解析和应用。未知 ID 在构建时明确失败，不回退。
- 数字键切到另一 scene 清除旧 scene-local ID；重复选择同一 scene 保留变体。配置快照比较增加 sceneVariant。CPU reference 尚未消费此实验场景构建链，显式拒绝非空变体而非静默忽略。
- Debug x64 solution build exit 0，Contracts 通过，Showcase exit 0，SceneWave1 exit 0。未知变体实际运行 exit 10，原因 Unknown experiment scene variant for the selected preset。
- 1280×720 / Ray Query / Megakernel / MIS / Current Frame / 2 frames：ggx-mis 的 white-furnace、environment-dome 的 polar-sun 均 exit 0；启动报告的 variant 和 fingerprint 对应选择值，完整日志错误匹配数为 0。
- 入口示例：`--scene ggx-mis --scene-variant white-furnace`，`--scene environment-dome --scene-variant polar-sun`。
- 本轮只完成 CLI 和配置/构建通路。运行中变体键位、列表发现、Diagnosis/Benchmark、动态时间轴驱动与图像数值验收仍待完成，不能把启动成功当作画质或理论验证。

## 2026-09-12 续轮：F12 运行中变体循环

- 新增 GLFW F12 / Shift+F12，分别触发下一个/上一个场景变体。PressOnly，全局离散键不受 ImGui 键盘捕获屏蔽，Repeat 不提交。
- RuntimeConfigHarness 接受场景提供者返回的真实变体目录，不在输入层重复维护场景 ID 表；无提供者、空目录、失效当前 ID 或提供者异常均拒绝，不修改配置。
- F12 经 FIFO 队列生成完整候选，CapabilityTable 通过后提交 sceneVariant，合并 A/T/Q/P/AS 重置。数字键后同批次 F12 使用新场景目录；F10 等队列快照保留其所在位置的配置。
- renderer 使用已构建场景目录；同批次切到其他场景时从该场景构建器读取目录。载荷重建不移动相机，Home 恢复当前变体预设机位。F1 加入 F12 说明。
- Debug x64 完整 solution build exit 0，Showcase exit 0，PlatformGlfw exit 0。新增测试覆盖 GLFW 映射、前后循环、UI 捕获与 Repeat、FIFO 快照、数字键后循环、保留非变体字段、reset mask、缺 provider 拒绝。
- 证据边界：本轮是输入/配置自动测试与完整构建，尚未在可见窗口中实际按 F12 逐一截图验收；不据此声称所有变体画面、资源失败回滚或动态时间轴已完成。

## 2026-09-12 续轮：Film 与本帧路径计数分离

- ShowcaseRuntimeStatus / RuntimeStatusLineViewModel 删除含糊的 samplesPerPixel 字段，改为 progressiveFilmSpp，并增加 currentFramePathsPerPixel；没有保留旧字段别名。
- 主程序仅在 Final / Progressive Mean 发布 Film 计数，其他模式不伪造为零或复用 temporal 长度。当前已实现 GPU 路径固定每像素每帧一个主路径，独立显示 paths/pixel/frame=1；它不代表总光线或可见性光线数。
- 空 Film（0）是合法重置状态，不再把整条 Fresh 状态判 Invalid。缺测量仍保持 --，不以配置目标 SPP 填充运行计数。
- 窗口标题、ImGui 公共状态行、F1 和运行结束摘要使用明确标签。Showcase 增加零 Film、缺 Film 与独立路径值测试并通过。
- 完整 Debug x64 build exit 0，Showcase exit 0。1280×720 Cornell / Ray Query / Megakernel / MIS / 2 frames / validation on：Current Frame 报告 Film SPP=n/a, paths/pixel/frame=1；Progressive Mean 报告 Film SPP=2, paths/pixel/frame=1；两者 exit 0，完整输出错误匹配数均为 0。
- 本证据证明计数标签和运行路径分离，不证明 Film 图像等于独立逐帧算术平均。Reference SPP、temporal history、reservoir M/age/candidates、实际 visibility/total rays 的完整分离和 F10 展示仍待完成。

## 2026-09-12 续轮：真实 Film 数值验收发现 Wavefront 失败

- 新增 tools/Validate-FrameSignals.ps1 和 Showcase Tests 的 --verify-progressive-film：固定种子，逐个运行到第 1/2/3 帧导出 Current Frame，再单独导出三帧 Progressive Mean，以 double 求和独立比较 RGB 均值。拒绝非有限/负 RGB、全黑、完全不变的帧以及超过 2e-5 的 max scaled error。
- 完整 Debug 构建通过。默认 640×360 / Cornell / Ray Query / MIS / seed 47 / maximum bounce 4 的真实 GPU 捕获结果：Staged max_scaled_error=1.22667e-7，RMSE=8.95244e-9；Megakernel max_scaled_error=1.18873e-7，RMSE=9.00632e-9，均通过。
- **Wavefront 未通过**：max_scaled_error=1.9426，RMSE=1.15114；脚本 exit 1。未放宽阈值，不得将 Film 主链整体标记完成。
- 原始 EXR/PNG/metadata 保留在 `.artifacts/p02-film-d6b0077542f74c48970f2363c92f99c5/`，每个执行架构有 current-frame-1/2/3 和 progressive-mean-3 的独立 run。
- 已核查 Wave2 的 Wavefront 分支 RecordFrame 后直接 return，并非显然重复 RecordPostIntegrator；UpdatePostIntegratorFrame 也只有一条更新路径。下一步需区分固定种子 Wavefront 的跨运行确定性、生产信号输出与 Film 输入/同步问题，不凭计数猜原因。

## 2026-09-12 续轮：固定捕获输入后 Film 三架构通过

- 同 seed / frame=3 重复 Wavefront Current Frame 的旧、新 EXR 不同，RGB 比较 RMSE=1.74223；两张预览连构图都不同。旧 metadata 只有 camera preset，不能证明实际相机一致，因此旧失败不能直接归因于 Film 算法。
- 修复有限帧 CLI capture 的输入条件：有 captureDirectory 且有 frame/SPP 终止目标时，不捕获/warp 光标，并 drain 但不将物理输入转换成相机/算法动作；普通交互模式保持原输入链。未改 Film 公式或放宽阈值。
- 修复后 Wavefront 单独重跑通过，又在三架构完整重跑中复现通过：Staged max_scaled_error=1.22667e-7 / RMSE=8.95244e-9；Megakernel 1.18873e-7 / 9.00632e-9；Wavefront 1.17409e-7 / 7.47942e-9。
- 完整三架构捕获在 `.artifacts/p02-film-015b3bc29d354f6e89803bab792cc133/`；单独 Wavefront 重跑在 `.artifacts/p02-film-38de5632f90445aab7c08cddc0f538a1/`。三帧均有变化且非黑，阈值仍为 2e-5。
- Debug solution build 和 Showcase 测试通过。此数值证据范围是固定 Cornell / MIS / 640×360 / 三帧 / maximum bounce 4；不覆盖 ReSTIR 注入、动态历史、全部场景或完整 P0 验收。

## 2026-09-12 续轮：ReSTIR 注入后的 Film 数值覆盖

- Validate-FrameSignals.ps1 增加 Scene、DirectLighting 和 RestirStage 参数；ReSTIR 验证固定 1 candidate、空间阶段 5 neighbors、ExplicitlyBiased、灯光/遮挡物动画关闭，不改变均值误差阈值。
- Many Lights / 640×360 / 三帧 / Initial-only：Staged max_scaled_error=1.62719e-7，Megakernel=1.61835e-7，Wavefront=1.23109e-7，全部通过。产物 `.artifacts/p02-film-9d9e79738ec845869475adc10c976e86/`。
- Wavefront 三个其他阶段通过：Temporal=1.23109e-7（87554320b57244b1b0b0629761b2609d）；Spatial=1.21715e-7（f8c020e7ffb242e59c200c8ef7cf9064）；Temporal-Spatial=1.23836e-7（b105184b37ae44a1b7e3a42f7b224f56）。各目录均在 `.artifacts/p02-film-<ID>/`。
- 目标 1280×720 的 Wavefront Temporal-Spatial 三帧复验通过：max_scaled_error=1.15859e-7，RMSE=2.65502e-9，产物 `.artifacts/p02-film-287c85c8f32541b6a2e5d93b030cbcfc/`。
- 负例：以第一个 Current Frame 充当 Film，max_scaled_error=2、RMSE=0.0825931，验证器按预期 exit 2。正常实验均未触发脚本的 validation/runtime/Profiler 错误门控。
- 证据只证明 ReSTIR 最终信号进入 Film 的算术一致性，包括以上静态复用配置；不证明目标函数/材质范围、复用无偏性、实际可见性次数、相机运动拒绝或动态残影。P0-3/P0-4 不因此整体完成。

## 2026-09-12 续轮：七个 ReSTIR Debug 视图最终输出

- 能力表与 V/Shift+V 循环开放 Reservoir M、Weight、Light ID、Source、Reuse、Rejection、WinnerVisibility，前提是直接光生产者为 ReSTIR DI；其他生产者明确拒绝。
- 删除 renderer 的第二份六值 Debug 白名单，统一复用能力表；调试名称复用公共名称，避免新增视图显示 unknown。
- MakeReSTIRResourceTable 的 DebugImage 从未呈现的独立 AOV 改为最终 outputImageView，Debug pass 在重建之后写最终图。Reservoir 调试时 Compose 不执行 Film 更新，避免纯显示切换污染均值。
- structured Debug record 保持原始数值；图像 M/age 按上限归一化，权重 x/(1+x)，ID/Source/Reuse/Reject 使用实际数值的分类颜色编码，可见性图 RGB=visibility/evaluated/visible。F10 说明这些是显示编码且经过曝光/tone mapping，不声称图像颜色等于数值读数。
- Debug x64 solution build、Contracts、Showcase 和生产 V3 GPU oracle 通过。真实 1280×720 / Many Lights / Wavefront / Temporal-Spatial / 三帧捕获覆盖七个视图，全部 exit 0、validation/runtime/Profiler 错误匹配为 0；产物 `.artifacts/p03-debug-5930b155c2a34b27b8c158ffe38e20f7/`。
- 已目视检查 M、WinnerVisibility、Light ID 三张预览，确认为不同诊断输出而非普通光照图。全部字段的 GPU 原始数值与显示编码逐项断言、鼠标位置数值检查器、动态重投影拒绝和材质范围仍未验收。

## 2026-09-12 续轮：Spatial A-Trous、材质所有权与场景 8 动态 Motion

- 删除旧 `temporal-atrous` 含混入口，RuntimeConfig/CLI/UI/捕获统一为 `atrous-spatial` / `SpatialFixedAtrous`。生产计划为 Prepare → VarianceBootstrap → N×A-Trous → Compose，不再调度 Motion/Temporal/PublishHistory，也不要求 temporal provider。Debug x64 全量构建、Contracts、Showcase 与 Reconstruction 测试通过；640×360 真实 Vulkan Many Lights 捕获 exit 0、非黑且无 validation/runtime/profiler 错误。
- ReSTIR 主直接光所有权限制为 opaque metallic-roughness。透射、复杂 Fresnel 或不兼容 lobe 的 primary surface 对 ReSTIR 失效，但保留常规 NEE/MIS，且不会触发下一跳环境/发光体的重复抑制。ABI-v2/v3 布局未变。Debug 全量构建、Contracts/Showcase/Megakernel/Wavefront/HardwareRT 与生产 V3 GPU oracle 通过；rough-transmission 聚焦 ROI 的 MIS/ReSTIR 线性 EXR 数值完全一致（RMSE=0）。
- 修复场景 8 的伪 identity motion：表面导出现在按 stable instance ID 找真实实例，使用 `worldToObject` 写 object-space surface point 和实际 transform index；L8 transform SSBO 上传每个实例的 current/previous `objectToWorld`，Motion 常量发布真实 transform count。无法解析实例时 fail-closed，不再伪造 identity。
- 新增六个 L8 最终调试视图：Motion、History Length、Moments、Variance、Temporal Acceptance、Temporal Reject Reasons；已接入 CLI、V/Shift+V 循环、状态、Showcase catalog、CapabilityTable 与捕获 metadata。诊断视图会构造完整 SVGF guide/history 计划，即使当前显示轴不是 Final。
- 场景 8 由静态启动样本改为运行时确定性 60 Hz 时间轴。移动面板每帧同步 canonical instance、Linear/Flattened SAH 世界空间三角形、Ray Query TLAS UPDATE 和 L8 transform pair；不增长 scene/resource generation，不全清 temporal/reservoir history。P 暂停，O 在暂停时单步；F12/场景重建重置时间轴。Camera-only 走正常重投影，Camera-cut 只在 N=60 强清 Film/Temporal/Reservoir。
- 640×360 / Ray Query / Wavefront / SVGF 实测：object-only Motion 三帧 exit 0，预览为中灰静止背景及移动面板的有向 motion 色条；camera-cut + Temporal Reject Reasons 61 帧 exit 0，日志仅在 frame=60 报告一次强失效。两次均启用 Vulkan validation，未出现 VUID、Runtime failure 或 Profiler 拒绝。产物分别在 `.artifacts/p02-scene8-dynamic-motion-20260912/` 与 `.artifacts/p02-scene8-camera-cut-20260912/`。
- 本轮最终 Debug x64 solution build exit 0；Contracts、Showcase、SceneWave1、Wavefront、Reconstruction 全部 exit 0。仍未以此声称 P0-2～P0-4 全量完成：Reference SPP/实际光线计数、L8 原始 history 数值回读、所有场景 1280×720 矩阵、Diagnosis/Benchmark 配置以及场景 1/4/5/9 的完整数值验收仍需继续。

## 2026-09-12 续轮：运行计数、场景 1 固定射线与场景 5 环境积分

- ShowcaseRuntimeStatus/F10 将 Reference SPP、Temporal History Length、Reservoir M/Age/Candidates、Visibility/Total Rays 分列；缺少生产回读时显示 `--`，不再拿目标 SPP、比较预算或配置上限伪装成运行观测。
- 传统 NEE/MIS 在 F3/L6 计数启用时发布实际 ShadowRays 为 Visibility Rays，并以 PathRays+ShadowRays 发布 Total Rays。ReSTIR 的独立可见性队列尚未回读，因此继续显示 `--`，不以 comparison budget 代替实际工作量。
- 场景 1 的 9 条命名固定射线现在由 CPU Brute Force 与 Binned SAH 对同一个 canonical scene 执行，逐条比较 hit/miss、稳定 primitive（允许共享边等价集合）、t、重心坐标和非法射线分类。当前仍缺将同一场景 1 corpus 原样送入 Flattened SAH/Ray Query 的 GPU 回放；既有 GPU smoke 的另一套固定射线不能冒充此证据。
- 场景 5 对 Original、PolarSun、SeamSun 三种环境逐 texel 使用精确纬经球面立体角，验证极点行有限正值、importance PMF 非负且归一、Sample/Eval/PDF 穷举重构球面亮度积分，并检查经度 `-pi/+pi` 映射到同一 seam texel。
- 完整 Debug x64 solution build exit 0；SceneWave1、Showcase、Contracts 均 exit 0。上述为契约和 CPU 数值证据，不替代 GPU 环境采样分布、场景 4 白炉能量或场景 9 全消融验收。

## 2026-09-12 续轮：生产 ReSTIR Statistics fence-safe 回读

- ABI-v3 现有 `GpuRestirStatisticsV3` 现在从真实生产帧回读：Statistics storage buffer 增加 transfer-src；每个 in-flight slot 使用独立 host-coherent transfer-dst staging；ReSTIR 完成后插入 compute-write → transfer-read barrier 和 copy；仅在该 slot 的 renderer fence 已完成后消费一次。
- 所有权链保持 `Wave3DebugRuntime -> Wave2RuntimeTelemetry -> VulkanWhittedRenderer -> ShowcaseRuntimeStatus`；渲染器不越过 Wave2Runtime 直接访问内部 Wave3 资源。未修改 ABI-v3 布局或 RuntimeConfig 版本。
- `reservoirCandidates` 发布 shader 原子统计的 generated candidates；`visibilityR` 发布实际 submitted visibility rays。开启 L6 路径计数时，Total Traced Rays 才以实际 PathRays + ReSTIR submitted visibility 相加；缺少任一生产回读就保持 `--`。
- 有限帧执行在 `vkDeviceWaitIdle` 后消费尚未轮回复用的两个 slot，并把 candidates/visibility/total 写入终端摘要，避免四帧短测因未复用 slot 而看不到观测。
- 真实 RTX 4070 Laptop GPU、1280×720、场景 9、Ray Query、Wavefront、Temporal-Spatial、1 candidate、5 neighbors、validation on、4 帧：exit 0；`candidates=921600`、`visibility rays=919813`、`total traced rays=--`。Total 为 `--` 是因为本次未打开 F3/L6 counter，并非拿预算补数；无 Vulkan validation/runtime/profiler 错误。
- Debug x64 solution build exit 0；Contracts、Showcase、ReSTIR 全部通过；ReSTIR GPU oracle 为 50,167 assertions / 0 failures。仍待：逐像素 reservoir M/age 的明确聚合口径和 GPU 归约、temporal history length 原始回读、Reference SPP provider、9A–9G 消融以及完整 1280×720 矩阵。
