# P0-1：RuntimeConfig v2 拆轴与旧入口退役

日期：2026-09-12。工作树：`D:\MyProjects\RenderingEngine_Current_20260908`。
分支：`codex/rt-integration-all`，HEAD `f88e898`，保留既有未提交修改；本次没有提交、重置或清理整个工作树。

## 完成范围

- 传输模型、执行架构、离散选灯、环境方向采样成为独立配置字段；贯通 CLI、输入、推荐配置、状态显示、历史身份与 GPU 参数。
- 删除旧 `Integrator` / `LightProposalDistribution` 配置枚举、旧 CLI 拼写及 `RunOptions` 投影，不增加兼容别名。
- 当前渲染器只创建 canonical scene + 当前 GPU runtime；删除旧运行开关、重复场景上传、旧 frame UBO/compute descriptors/pipeline/reload/dispatch 分支。保留必要的明确拒绝检查。
- Whitted 的当前实现仅允许 Staged，ReSTIR 不对 Whitted 开放；CPU Reference 仍为专用 headless Cornell 入口；未接入的 Provider 仍拒绝，不回退。
- 两个 `.vcxproj.user` 删除旧算法参数覆盖，继承权威 `.vcxproj` 的 v2 参数。MSBuild 实际求值确认启动目标和工作目录均在 D 盘当前工程。
- F11 继续原子恢复当前场景推荐配置；数字键仍只换场景，不连带改算法。F10/F1 和 UI 已使用拆分后的语义。

具体 BSDF、直接光、终止和 AOV 差异见 [当前实现记录](../../../PBR_IMPLEMENTATION.md)。当前 Whitted 使用随机选择的理想镜面链，并不是确定性的完整反射/折射递归树；三种 PBR 架构的 AOV 分解也不能直接宣称完全等价。

## 删除与恢复

以下四个退役源文件已从活动工程及构建清单删除，原文件受 Git 跟踪，可以从历史恢复：

- `include/scene/GpuScene.hpp`
- `src/scene/GpuScene.cpp`
- `resources/shaders/pbr/PbrPathTrace.hlsl`
- `resources/shaders/whitted/WhittedTrace.hlsl`

未删除当前 canonical 场景、共享 PBR BSDF、Whitted 生产分支或 Present Shader，也未修改当前生产 Shader 的算法数学。

官方 Debug 输出及旧镜像目录残留的四份旧 SPIR-V 已移到可恢复目录：
`D:\MyProjects\RenderingEngine_Current_20260908\.artifacts\p01-retired-shaders-20260912`。
移走后又实际运行了 Baseline、Whitted、VS 多灯默认配置，三者都成功，官方 Debug bin 中旧 SPIR-V 未重新出现。

## 验证结果

| 项目 | 本次证据 |
|---|---|
| Debug x64 工程和完整解决方案 | MSBuild 成功，退出码 0 |
| 11 个 Debug 测试程序 | 全通过：Contracts、CpuReference、PlatformGlfw、Restir、SceneWave1、Showcase、SoftwareGpu、Megakernel、Wavefront、Reconstruction、HardwareRT |
| 最终 CLI 帮助更新后的复测 | Contracts 和 Showcase 再次通过 |
| 组合矩阵 | 186,624 个 tuple 逐项核对预期状态；116,640 支持，69,984 明确不支持，不能混作 InvalidConfiguration |
| 四轴测试 | 完整配置比较补齐 transport/environment；执行中间值、保留字段与 A/T/Q/P reset mask 逐字段验证 |
| F11 | 推荐恢复、FIFO 快照、幂等性，以及非 Many-Lights 场景保留自定义 ReSTIR 设置通过 |
| CLI 删除契约 | 旧选项和旧值必须抛出 CLI 错误，不接受别名 |
| GPU 测试 | RTX 4070 Laptop GPU 上 ReSTIR validation smoke 通过；nonempty=4，final_visibility=4 |
| 运行矩阵 | `Validate-Pbr.ps1 -Configuration Debug -SkipBuild -Frames 2` 通过：13 项 CLI/拒绝用例 + 18 项真实运行用例 |
| 运行覆盖 | 三种 GPU 执行架构、三个当前遍历入口、Physical/PCF/PCSS、材质视图、种子/目标 SPP、VSync、环境方向两轴和 90 帧 resize |
| 有限 CPU 入口 | v2 CLI：Cornell / CPU SAH / CPU Reference / MIS，64×64、1 SPP，14,481 rays，退出码 0 |
| 移走旧 SPIR-V 后 | 三组 1280×720、2 帧截图与 metadata 均生成，退出码 0 |
| 静态检查 | 本次文件的 `git diff --check` 通过；当前生产入口无旧配置枚举/RunOptions/旧 compute Shader 引用 |

上述有界运行未检测到 Vulkan validation、Profiler contract rejection 或 Runtime failure。预览已检查为真实出图而非整幅黑屏，但只有 1–2 SPP，不构成画质、能量正确性或动态稳定性验收。

## 可复核产物

- [Baseline 原始运行矩阵截图及 metadata](../../../../.artifacts/validate-pbr-a11063aa60e740a3af35336973489897/metadata.json)
- [移走旧二进制后的 Baseline](../../../../.artifacts/p01-runtime-verification-20260912/baseline-staged-clean-bin/metadata.json)
- [Whitted 光学室](../../../../.artifacts/p01-runtime-verification-20260912/whitted-optics-clean-bin/metadata.json)
- [VS 默认多灯场景](../../../../.artifacts/p01-runtime-verification-20260912/vs-many-lights-clean-bin/metadata.json)
- [有限 CPU Reference](../../../../.artifacts/p01-runtime-verification-20260912/cpu-reference-v2/metadata.json)

本次 EXE：`D:\MyProjects\RenderingEngine_Current_20260908\bin\x64\Debug\RenderingEngine.exe`。
SHA-256：`809281738A5BB489EE85E14416EB4AF9D31B9174ED1680994CF5781E94AD1EFE`。

## 使用与后续边界

- `I / Shift+I`：传输模型；`Ctrl+I / Ctrl+Shift+I`：执行架构。
- `Ctrl+L`：离散选灯；`Alt+L`：环境方向采样；加 Shift 反向。
- `L`：直接光；`Ctrl+Alt+L`：阴影；`B / N / V`：遍历 / 重建 / Debug。
- `0–9` 只切场景，`F10` 看当前/推荐组合，`F11` 恢复推荐；`Home` 恢复相机。
- 从非 Staged 架构请求 Whitted 会明确拒绝，不代改执行架构。可先选 PBR/Staged 再切 Whitted，或在场景 2 按 F11。

本交付只完成 P0-1。CurrentFrame/ProgressiveMean、相机运动历史、环境/BSDF 数学积分、ReSTIR 阶段诊断、全面实验记录与画质性能验收属于后续工作，未借用本次“编译/能出图”结果宣称完成。
