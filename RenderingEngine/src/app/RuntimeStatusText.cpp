#include "app/RuntimeStatusText.hpp"

#include <iomanip>
#include <filesystem>
#include <locale>
#include <optional>
#include <sstream>
#include <string>

namespace RenderingEngine
{
    namespace
    {
        [[nodiscard]] std::string_view RuntimeToggleName(const RuntimeToggle value) noexcept
        {
            switch (value)
            {
            case RuntimeToggle::RendererDefault: return "渲染器默认";
            case RuntimeToggle::Enabled: return "已启用";
            case RuntimeToggle::Disabled: return "已禁用";
            default: return "未知开关状态";
            }
        }

        [[nodiscard]] std::string_view ShadowMethodName(const ShadowMethod value) noexcept
        {
            switch (value)
            {
            case ShadowMethod::Pcf: return "PCF";
            case ShadowMethod::Pcss: return "PCSS";
            case ShadowMethod::Physical: return "物理阴影";
            default: return "未知阴影方法";
            }
        }

        [[nodiscard]] std::string BoolName(const bool value)
        {
            return value ? "是" : "否";
        }

        [[nodiscard]] std::string PathText(const std::filesystem::path& value)
        {
            // path::u8string() is std::u8string under C++20 and std::string
            // on older standard-library modes. Both contain the same UTF-8
            // byte sequence, so this keeps the public result std::string.
            const auto encoded = value.u8string();
            return std::string(
                reinterpret_cast<const char*>(encoded.data()), encoded.size());
        }

        [[nodiscard]] std::string OptionalPathText(
            const std::optional<std::filesystem::path>& value)
        {
            if (!value.has_value())
            {
                return "<未设置>";
            }
            return PathText(*value);
        }

        [[nodiscard]] std::string OptionalStringText(
            const std::optional<std::string>& value)
        {
            if (!value.has_value())
            {
                return "<未设置>";
            }
            return *value;
        }
    }

    std::string_view ScenePresetName(const ScenePreset value) noexcept
    {
        switch (value)
        {
        case ScenePreset::BaselineGallery: return "基线展厅";
        case ScenePreset::IntersectionBvhLab: return "交集与 BVH 实验室";
        case ScenePreset::WhittedOpticsRoom: return "Whitted 光学室";
        case ScenePreset::CornellBox: return "Cornell Box";
        case ScenePreset::GgxMisMaterialLab: return "GGX、MIS 与粗糙介质材质实验室";
        case ScenePreset::EnvironmentSamplingDome: return "环境采样穹顶";
        case ScenePreset::SponzaTraversalHall: return "Sponza 遍历大厅";
        case ScenePreset::BackendParityBenchmark: return "后端一致性基准";
        case ScenePreset::TemporalStabilityCorridor: return "时序稳定性走廊";
        case ScenePreset::ManyLightsRestirArena: return "多光源 ReSTIR 竞技场";
        default: return "未知场景";
        }
    }

    std::string_view TraversalBackendName(const TraversalBackend value) noexcept
    {
        switch (value)
        {
        case TraversalBackend::CanonicalLinearGpu:
            return "Canonical Linear GPU";
        case TraversalBackend::CpuBruteForce: return "CPU 暴力遍历";
        case TraversalBackend::CpuSahBvh: return "CPU SAH BVH";
        case TraversalBackend::GpuFlattenedSahBvh: return "GPU 展平 SAH BVH";
        case TraversalBackend::GpuLbvh: return "GPU LBVH";
        case TraversalBackend::VulkanRayQuery: return "Vulkan Ray Query";
        case TraversalBackend::VulkanRayTracingPipeline: return "Vulkan 光线追踪管线";
        default: return "未知遍历后端";
        }
    }

    std::string_view TransportModelName(const TransportModel value) noexcept
    {
        switch (value)
        {
        case TransportModel::Pbr: return "PBR 路径传输";
        case TransportModel::Whitted: return "Whitted 镜面传输";
        default: return "未知传输模型";
        }
    }

    std::string_view ExecutionArchitectureName(
        const ExecutionArchitecture value) noexcept
    {
        switch (value)
        {
        case ExecutionArchitecture::Staged: return "GPU 分阶段执行";
        case ExecutionArchitecture::CpuReference: return "CPU 参考执行";
        case ExecutionArchitecture::Megakernel: return "GPU Megakernel";
        case ExecutionArchitecture::Wavefront: return "GPU Wavefront";
        default: return "未知执行架构";
        }
    }

    std::string_view DirectLightingEstimatorName(
        const DirectLightingEstimator value) noexcept
    {
        switch (value)
        {
        case DirectLightingEstimator::BsdfOnly: return "仅 BSDF";
        case DirectLightingEstimator::NextEventEstimation: return "下一事件估计（NEE）";
        case DirectLightingEstimator::MultipleImportanceSampling:
            return "多重重要性采样（MIS）";
        case DirectLightingEstimator::RestirDirectIllumination:
            return "ReSTIR 直接光照";
        default: return "未知直射光估计器";
        }
    }

    std::string_view LightSelectionStrategyName(
        const LightSelectionStrategy value) noexcept
    {
        switch (value)
        {
        case LightSelectionStrategy::Uniform: return "均匀选灯";
        case LightSelectionStrategy::PowerWeighted: return "功率加权选灯";
        default: return "未知选灯策略";
        }
    }

    std::string_view EnvironmentDirectionSamplerName(
        const EnvironmentDirectionSampler value) noexcept
    {
        switch (value)
        {
        case EnvironmentDirectionSampler::UniformSphere: return "均匀球面方向";
        case EnvironmentDirectionSampler::ImportanceMap: return "环境贴图重要性方向";
        default: return "未知环境方向采样器";
        }
    }

    std::string_view ReconstructionModeName(const ReconstructionMode value) noexcept
    {
        switch (value)
        {
        case ReconstructionMode::CurrentFrame: return "当前帧未滤波 (Current Frame)";
        case ReconstructionMode::ProgressiveMean: return "静态累计均值 (Progressive Mean)";
        case ReconstructionMode::TemporalAccumulation: return "时序累积";
        case ReconstructionMode::SpatialFixedAtrous: return "空间 A-Trous（无时序历史）";
        case ReconstructionMode::Svgf: return "SVGF";
        default: return "未知重建模式";
        }
    }

    std::string_view DebugViewName(const DebugView value) noexcept
    {
        switch (value)
        {
        case DebugView::Final: return "最终图像";
        case DebugView::BaseColor: return "基础色";
        case DebugView::Normal: return "法线";
        case DebugView::Roughness: return "粗糙度";
        case DebugView::Metallic: return "金属度";
        case DebugView::Emissive: return "自发光";
        case DebugView::Motion: return "运动向量";
        case DebugView::HistoryLength: return "时序历史长度";
        case DebugView::Moments: return "时序亮度矩";
        case DebugView::Variance: return "时序方差";
        case DebugView::TemporalAcceptance: return "时序接受掩码";
        case DebugView::TemporalRejectReasons: return "时序拒绝原因";
        case DebugView::ReservoirM: return "Reservoir M";
        case DebugView::ReservoirWeight: return "Reservoir 权重";
        case DebugView::ReservoirLightId: return "Reservoir 光源 ID";
        case DebugView::ReservoirSource: return "候选来源";
        case DebugView::ReservoirReuse: return "复用来源";
        case DebugView::ReservoirRejection: return "复用拒绝原因";
        case DebugView::WinnerVisibility: return "最终样本可见性";
        default: return "未知调试视图";
        }
    }

    namespace
    {
        [[nodiscard]] std::string_view ManyLightsTierName(
            const ManyLightsTier value) noexcept
        {
            switch (value)
            {
            case ManyLightsTier::Lights100: return "100";
            case ManyLightsTier::Lights1000: return "1000";
            case ManyLightsTier::Lights10000: return "10000";
            default: return "invalid";
            }
        }

        [[nodiscard]] std::string_view RestirReuseStageName(
            const RestirReuseStage value) noexcept
        {
            switch (value)
            {
            case RestirReuseStage::Initial: return "initial";
            case RestirReuseStage::Spatial: return "spatial";
            case RestirReuseStage::Temporal: return "temporal";
            case RestirReuseStage::TemporalSpatial: return "temporal-spatial";
            default: return "invalid";
            }
        }

        [[nodiscard]] std::string_view RestirBiasModeName(
            const RestirBiasMode value) noexcept
        {
            switch (value)
            {
            case RestirBiasMode::ExplicitlyBiased: return "explicitly-biased";
            case RestirBiasMode::ReferenceCorrection: return "reference-correction";
            default: return "invalid";
            }
        }

        [[nodiscard]] std::string_view RuntimeProfileName(
            const RuntimeConfig& config) noexcept
        {
            if (config.executionArchitecture == ExecutionArchitecture::CpuReference)
            {
                return "L3 CPU Reference";
            }
            return "Final Mixed Runtime";
        }

        [[nodiscard]] std::string_view ScenePurpose(const ScenePreset value) noexcept
        {
            switch (value)
            {
            case ScenePreset::BaselineGallery:
                return "验证基础反射、折射、GGX、阴影与逐帧累积是否正常。";
            case ScenePreset::IntersectionBvhLab:
                return "隔离射线求交和 BVH 遍历，比较不同后端的命中结果。";
            case ScenePreset::WhittedOpticsRoom:
                return "检查镜面反射、折射、Fresnel、全反射和介质吸收。";
            case ScenePreset::CornellBox:
                return "观察漫反射全局光照以及 BSDF、NEE、MIS 的收敛差异。";
            case ScenePreset::GgxMisMaterialLab:
                return "检查 GGX 微表面、高光粗糙度、VNDF、MIS 与能量守恒。";
            case ScenePreset::EnvironmentSamplingDome:
                return "比较环境贴图均匀采样和亮度重要性采样的方差。";
            case ScenePreset::SponzaTraversalHall:
                return "面向大型网格和复杂遮挡的遍历压力测试；当前资源仍受门控。";
            case ScenePreset::BackendParityBenchmark:
                return "用同一相机和场景比较 Canonical Linear、Flattened SAH 与 Ray Query 的结果一致性。";
            case ScenePreset::TemporalStabilityCorridor:
                return "暴露运动重投影、历史拒绝、拖影以及时空降噪稳定性。";
            case ScenePreset::ManyLightsRestirArena:
                return "比较多光源均匀/功率采样与 ReSTIR 时空 reservoir 复用。";
            default:
                return "未知场景，没有可用的测试目的说明。";
            }
        }

        [[nodiscard]] std::string_view SceneObservation(const ScenePreset value) noexcept
        {
            switch (value)
            {
            case ScenePreset::BaselineGallery:
                return "镜面边缘、玻璃透射、接触阴影，以及 SPP 增长后的噪声变化。";
            case ScenePreset::IntersectionBvhLab:
                return "物体轮廓、遮挡关系和切换后端后是否出现漏交或错交。";
            case ScenePreset::WhittedOpticsRoom:
                return "反射/折射方向、临界角附近的全反射和厚介质颜色衰减。";
            case ScenePreset::CornellBox:
                return "顶灯附近、小面积光源阴影、墙面间接染色和低 SPP 噪声。";
            case ScenePreset::GgxMisMaterialLab:
                return "粗糙度梯度、高光形状、掠射角 Fresnel 和白炉能量。";
            case ScenePreset::EnvironmentSamplingDome:
                return "环境亮区附近的噪声、阴影方向和采样热点是否匹配。";
            case ScenePreset::SponzaTraversalHall:
                return "大量三角形下的漏光、漏交、遍历时间和显存占用。";
            case ScenePreset::BackendParityBenchmark:
                return "相同种子下轮廓和亮度是否一致，并比较 GPU trace 时间。";
            case ScenePreset::TemporalStabilityCorridor:
                return "移动相机时的拖影、闪烁、边缘历史泄漏，停止后是否重新收敛。";
            case ScenePreset::ManyLightsRestirArena:
                return "低 SPP 下的直接光噪声、阴影稳定性和 reservoir 复用伪影。";
            default:
                return "观察最终图像和调试输出是否与当前配置一致。";
            }
        }

        [[nodiscard]] std::string_view TraversalSummary(
            const TraversalBackend value) noexcept
        {
            switch (value)
            {
            case TraversalBackend::CanonicalLinearGpu:
                return "逐射线线性测试全部 canonical 三角形；使用规范 canonical-linear-gpu CLI token，不会回退到 SAH BVH。";
            case TraversalBackend::CpuBruteForce:
                return "逐射线测试全部图元，简单但复杂度高，主要用于最小参考。";
            case TraversalBackend::CpuSahBvh:
                return "CPU 使用表面积启发式 BVH 减少无效图元测试。";
            case TraversalBackend::GpuFlattenedSahBvh:
                return "把 SAH BVH 展平成连续数组，由 Compute Shader 显式遍历。";
            case TraversalBackend::GpuLbvh:
                return "按 Morton 码并行建树，建树快；当前未接入生产展示路径。";
            case TraversalBackend::VulkanRayQuery:
                return "在 Compute Shader 内调用 Vulkan 硬件光追遍历并自行处理着色。";
            case TraversalBackend::VulkanRayTracingPipeline:
                return "通过 RayGen/Miss/Hit 与 SBT 调度；当前未接入生产展示路径。";
            default:
                return "未知遍历后端。";
            }
        }

        [[nodiscard]] std::string_view TransportModelSummary(
            const TransportModel value) noexcept
        {
            switch (value)
            {
            case TransportModel::Pbr:
                return "蒙特卡洛采样材质和光传输，逐步逼近渲染方程。";
            case TransportModel::Whitted:
                return "计算局部直接光，再随机延续理想镜面/透射链；当前不是同时穷举反射和折射的确定性递归树。";
            default:
                return "未知传输模型。";
            }
        }

        [[nodiscard]] std::string_view ExecutionArchitectureSummary(
            const ExecutionArchitecture value) noexcept
        {
            switch (value)
            {
            case ExecutionArchitecture::Staged:
                return "按反弹分阶段调度整幅图像；当前是 Whitted 的唯一生产执行架构。";
            case ExecutionArchitecture::CpuReference:
                return "受限 Cornell Lambert 的 headless 参考，不是所有 GPU 材质/场景的真值。";
            case ExecutionArchitecture::Megakernel:
                return "单个大型 Shader 完成整条路径，结构直接但容易线程分歧。";
            case ExecutionArchitecture::Wavefront:
                return "将 RayGen/Trace/Shade/Shadow 等阶段拆成队列；性能收益需要同场景同预算测量。";
            default:
                return "未知执行架构。";
            }
        }

        [[nodiscard]] std::string_view DirectLightingSummary(
            const DirectLightingEstimator value) noexcept
        {
            switch (value)
            {
            case DirectLightingEstimator::BsdfOnly:
                return "只按 BSDF 采样方向，小光源难命中，因此方差通常较高。";
            case DirectLightingEstimator::NextEventEstimation:
                return "在着色点主动采样光源并发射阴影射线，加速直接光收敛。";
            case DirectLightingEstimator::MultipleImportanceSampling:
                return "组合 BSDF 与光源采样并按 PDF 加权，降低两者的极端噪声。";
            case DirectLightingEstimator::RestirDirectIllumination:
                return "用 reservoir 压缩候选光，并进行时间/空间复用，适合大量光源。";
            default:
                return "未知直射光估计器。";
            }
        }

        [[nodiscard]] std::string_view LightSelectionSummary(
            const LightSelectionStrategy value) noexcept
        {
            switch (value)
            {
            case LightSelectionStrategy::Uniform:
                return "每盏灯等概率被选中，简单但可能浪费样本。";
            case LightSelectionStrategy::PowerWeighted:
                return "按光源功率分配概率，更常选择潜在贡献较大的灯。";
            default:
                return "未知选灯策略。";
            }
        }

        [[nodiscard]] std::string_view EnvironmentSamplerSummary(
            const EnvironmentDirectionSampler value) noexcept
        {
            switch (value)
            {
            case EnvironmentDirectionSampler::UniformSphere:
                return "选中环境灯后，在球面上等概率采样方向。";
            case EnvironmentDirectionSampler::ImportanceMap:
                return "选中环境灯后，按环境贴图亮度和 texel 立体角采样方向。";
            default:
                return "未知环境方向采样器。";
            }
        }

        [[nodiscard]] std::string_view ReconstructionSummary(
            const ReconstructionMode value) noexcept
        {
            switch (value)
            {
            case ReconstructionMode::ProgressiveMean:
                return "固定目标图像的独立 Progressive Film 均值；普通相机移动清 Film，不清重投影历史。";
            case ReconstructionMode::CurrentFrame:
                return "不使用历史或滤波，直接显示蒙特卡洛样本。";
            case ReconstructionMode::TemporalAccumulation:
                return "重投影上一帧并累积可信历史，降低静态噪声。";
            case ReconstructionMode::SpatialFixedAtrous:
                return "仅对当前帧进行边缘保持的多尺度 A-Trous 空间滤波，不读取时序历史。";
            case ReconstructionMode::Svgf:
                return "结合时序、亮度矩、方差估计和 A-Trous 的时空降噪。";
            default:
                return "未知重建模式。";
            }
        }

        [[nodiscard]] std::string_view ShadowSummary(const ShadowMethod value) noexcept
        {
            switch (value)
            {
            case ShadowMethod::Pcf:
                return "对多次二值遮挡结果求平均，得到固定宽度软化阴影。";
            case ShadowMethod::Pcss:
                return "先搜索遮挡物，再按距离估计半影宽度。";
            case ShadowMethod::Physical:
                return "向采样光源发射真实可见性射线，由光源面积自然形成软阴影。";
            default:
                return "未知阴影方法。";
            }
        }
    }

    std::string FormatRuntimeConfigStatus(const RuntimeConfig& config)
    {
        const std::string captureDirectory = OptionalPathText(config.run.captureDirectory);
        const std::string benchmarkPreset = OptionalStringText(config.run.benchmarkPreset);
        const std::string referenceImage = OptionalPathText(config.run.referenceImage);
        const std::string artifactRoot = PathText(config.run.artifactRoot);

        const SceneRecommendedProfile* const recommendation =
            FindSceneRecommendedProfile(config.scene);
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::fixed << std::setprecision(6)
            << "版本=" << config.version
            << "; 场景=" << ScenePresetName(config.scene)
            << "; 实验变体=" << (config.sceneVariant.empty() ? "<presentation>" : config.sceneVariant)
            << "; 遍历=" << TraversalBackendName(config.backend)
            << "; 传输模型=" << TransportModelName(config.transportModel)
            << "; 执行架构=" << ExecutionArchitectureName(
                config.executionArchitecture)
            << "; 直射光估计=" << DirectLightingEstimatorName(config.directLightingEstimator)
            << "; 选灯策略=" << LightSelectionStrategyName(config.lightSelection)
            << "; 环境方向=" << EnvironmentDirectionSamplerName(
                config.environmentSampler)
            << "; 重建=" << ReconstructionModeName(config.reconstruction)
            << "; 调试视图=" << DebugViewName(config.debugView)
            << "; 阴影=" << ShadowMethodName(config.shadowMethod)
            << "; 分辨率=" << config.render.width << 'x' << config.render.height
            << "; 渲染缩放=" << config.render.renderScale
            << "; 每帧采样=" << config.render.samplesPerFrame
            << "; 目标 SPP=" << config.render.targetSamplesPerPixel
            << "; 最大反弹=" << config.render.maximumBounce
            << "; 种子=" << config.render.baseSeed
            << "; 曝光=" << config.render.exposure
            << "; 垂直 FOV=" << config.render.verticalFovDegrees << "°"
            << "; 垂直同步=" << RuntimeToggleName(config.render.vsync)
            << "; 帧数限制=" << config.run.frameLimit
            << "; 缩放测试=" << BoolName(config.run.resizeTest)
            << "; 无窗口=" << BoolName(config.run.headless)
            << "; 验证=" << RuntimeToggleName(config.run.validation)
            << "; 捕获目录=" << captureDirectory
            << "; 基准预设=" << benchmarkPreset
            << "; 参考图=" << referenceImage
            << "; 产物根目录=" << artifactRoot
            << "; 运行 ID=" << config.run.runIdentifier
            << "; Many Lights=" << ManyLightsTierName(config.restir.manyLightsTier)
            << "; ReSTIR 阶段=" << RestirReuseStageName(config.restir.reuseStage)
            << "; ReSTIR 模式=" << RestirBiasModeName(config.restir.biasMode)
            << "; 候选/像素=" << config.restir.initialCandidatesPerPixel
            << "; 空间邻居=" << config.restir.spatialNeighbors
            << "; 最大 M=" << config.restir.maximumReservoirM
            << "; 历史年龄=" << config.restir.maximumHistoryAge
            << "; 对照候选预算="
            << config.restir.comparisonCandidateBudgetPerPixel
            << "; 对照可见性预算="
            << config.restir.comparisonVisibilityBudgetPerPixel
            << "; 灯光初始动画采样=" << BoolName(config.restir.animateLights)
            << "; 遮挡物初始动画采样="
            << BoolName(config.restir.animateRigidOccluders)
            << "; 教学推荐 ID="
            << (recommendation != nullptr
                ? recommendation->stableId
                : std::string_view("<未注册>"))
            << "; 教学推荐匹配="
            << (recommendation != nullptr
                    && MatchesSceneRecommendedProfile(config, *recommendation)
                ? "是"
                : "否");
        return output.str();
    }

    std::string FormatRuntimeReview(const RuntimeConfig& config)
    {
        const SceneRecommendedProfile* const recommendation =
            FindSceneRecommendedProfile(config.scene);
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output
            << "\n========== 当前场景与算法复习（F10） ==========\n"
            << "算法域：" << RuntimeProfileName(config) << '\n'
            << "场景：" << ScenePresetName(config.scene) << '\n'
            << "实验变体：" << (config.sceneVariant.empty() ? "<presentation>" : config.sceneVariant) << '\n'
            << "场景目的：" << ScenePurpose(config.scene) << '\n'
            << "观察重点：" << SceneObservation(config.scene) << "\n\n"
            << "遍历：" << TraversalBackendName(config.backend) << '\n'
            << "  " << TraversalSummary(config.backend) << '\n'
            << "传输模型：" << TransportModelName(config.transportModel) << '\n'
            << "  " << TransportModelSummary(config.transportModel) << '\n'
            << "执行架构：" << ExecutionArchitectureName(
                config.executionArchitecture) << '\n'
            << "  " << ExecutionArchitectureSummary(
                config.executionArchitecture) << '\n'
            << "直射光：" << DirectLightingEstimatorName(
                config.directLightingEstimator) << '\n'
            << "  " << DirectLightingSummary(config.directLightingEstimator) << '\n'
            << "离散选灯：" << LightSelectionStrategyName(
                config.lightSelection) << '\n'
            << "  " << LightSelectionSummary(config.lightSelection) << '\n'
            << "环境方向：" << EnvironmentDirectionSamplerName(
                config.environmentSampler) << '\n'
            << "  " << EnvironmentSamplerSummary(config.environmentSampler) << '\n'
            << "重建：" << ReconstructionModeName(config.reconstruction) << '\n'
            << "  " << ReconstructionSummary(config.reconstruction) << '\n'
            << "阴影：" << ShadowMethodName(config.shadowMethod) << '\n'
            << "  " << ShadowSummary(config.shadowMethod) << '\n'
            << "Debug View：" << DebugViewName(config.debugView) << '\n'
            << "最大反弹：" << config.render.maximumBounce << '\n';

        if (config.directLightingEstimator
            == DirectLightingEstimator::RestirDirectIllumination)
        {
            output
                << "ReSTIR：" << ManyLightsTierName(config.restir.manyLightsTier)
                << " lights / " << RestirReuseStageName(config.restir.reuseStage)
                << " / " << RestirBiasModeName(config.restir.biasMode)
                << "；候选=" << config.restir.initialCandidatesPerPixel
                << "；空间邻居=" << config.restir.spatialNeighbors << '\n'
                << "组合说明：当前 ReSTIR 仅允许 PBR 传输；历史身份变化时会显式重置。\n";
            output << "调试显示编码（之后经过显示曝光/色调映射）：M/age 按各自上限归一化；权重为 x/(1+x)；"
                "Light ID、Source、Reuse、Reject 是由真实值映射的分类颜色，不是强度或数值读数；"
                "Winner Visibility 的 RGB 分别为可见性、是否已求值、是否可见。原始字段保存在 GPU Debug record。\n";
        }
        else if (config.executionArchitecture == ExecutionArchitecture::Wavefront)
        {
            output << "调度说明：Wavefront 使用分离队列；重建由共享 L8 后处理阶段执行。\n";
        }
        else if (config.executionArchitecture == ExecutionArchitecture::Megakernel)
        {
            output << "调度说明：Megakernel 使用单路径状态调度；重建由共享 L8 后处理阶段执行。\n";
        }
        else if (config.executionArchitecture == ExecutionArchitecture::CpuReference)
        {
            output << "当前边界：CPU Reference 仅通过 Cornell 无窗口产物运行。\n";
        }
        else
        {
            output << "调度说明：分阶段执行按反弹处理整幅图像；Whitted 只随机延续理想镜面/透射链。\n";
        }

        if (recommendation != nullptr)
        {
            const bool matches = MatchesSceneRecommendedProfile(
                config,
                *recommendation);
            output
                << "\n教学推荐 ID：" << recommendation->stableId << '\n'
                << "当前是否匹配：" << (matches ? "匹配" : "偏离") << '\n'
                << "推荐组合："
                << TraversalBackendName(recommendation->backend) << " / "
                << TransportModelName(recommendation->transportModel) << " / "
                << ExecutionArchitectureName(
                    recommendation->executionArchitecture) << " / "
                << DirectLightingEstimatorName(
                    recommendation->directLightingEstimator) << " / "
                << LightSelectionStrategyName(
                    recommendation->lightSelection) << " / "
                << EnvironmentDirectionSamplerName(
                    recommendation->environmentSampler) << " / "
                << ReconstructionModeName(recommendation->reconstruction) << " / "
                << DebugViewName(recommendation->debugView) << " / "
                << ShadowMethodName(recommendation->shadowMethod)
                << "；最大反弹=" << recommendation->maximumBounce << '\n';
            if (recommendation->restir.has_value())
            {
                const RestirSettings& restir = *recommendation->restir;
                output
                    << "推荐 ReSTIR：" << ManyLightsTierName(restir.manyLightsTier)
                    << " lights / " << RestirReuseStageName(restir.reuseStage)
                    << " / " << RestirBiasModeName(restir.biasMode)
                    << "；候选=" << restir.initialCandidatesPerPixel
                    << "；邻居=" << restir.spatialNeighbors
                    << "；M=" << restir.maximumReservoirM
                    << "；历史=" << restir.maximumHistoryAge
                    << "；对照=" << restir.comparisonCandidateBudgetPerPixel
                    << '/' << restir.comparisonVisibilityBudgetPerPixel
                    << "；灯光/遮挡动画="
                    << (restir.animateLights ? "开" : "关") << '/'
                    << (restir.animateRigidOccluders ? "开" : "关") << '\n';
            }
            output << "Presentation (F11)：原子恢复上述教学推荐；恢复后仍可用 B/I/L/N/V 独立比较。\n";
        }
        else
        {
            output << "\nPresentation：当前场景未注册，F11 将保持配置不变。\n";
        }

        output
            << "切换提示：B=遍历后端；I=传输模型；Ctrl+I=执行架构；"
               "L=直射光估计；Ctrl+L=离散选灯；Alt+L=环境方向；"
               "Ctrl+Alt+L=阴影；N=重建；V=Debug View。每次只改变一个轴。\n"
            << "================================================\n";
        return output.str();
    }

    std::string_view GlfwKeyHelpText() noexcept
    {
        return
            "GLFW 统一键位帮助（所有 UI、CLI 与键盘切换共享 RuntimeConfig）\n"
            "\n"
            "相机与应用\n"
            "  W/A/S/D：前后左右移动；Q/E：下移/上移\n"
            "  Left Shift：快速移动；Left Ctrl：精细慢速移动\n"
            "  鼠标：指针捕获时观察方向；滚轮：调整移动速度；Alt+滚轮：调整垂直 FOV\n"
            "  Tab：捕获/释放鼠标；Esc：只释放鼠标，不直接退出；Alt+F4：退出程序\n"
            "  Home：回到当前展示空间的固定相机；P：暂停/继续场景动画；O：暂停时推进一帧\n"
            "\n"
            "算法切换（按键正向，Shift+按键反向）\n"
            "  B / Shift+B：下一个/上一个 Traversal Backend\n"
            "  I / Shift+I：PBR / Whitted 传输模型\n"
            "  Ctrl+I / Ctrl+Shift+I：Staged / Megakernel / Wavefront 执行架构\n"
            "  L / Shift+L：下一个/上一个 Direct Lighting Estimator\n"
            "  Ctrl+L / Ctrl+Shift+L：Uniform / Power-weighted 离散选灯\n"
            "  Alt+L / Alt+Shift+L：Uniform Sphere / Importance Map 环境方向\n"
            "  Ctrl+Alt+L / Ctrl+Alt+Shift+L：下一个/上一个 Shadow Method\n"
            "  N / Shift+N：下一个/上一个 Reconstruction\n"
            "  V / Shift+V：下一个/上一个 Debug View\n"
            "  八个算法轴彼此独立；Debug View 另为显示轴；CapabilityTable 对未实现组合明确拒绝。\n"
            "\n"
            "调试、比较与参数\n"
            "  R：手动清空 accumulation、temporal history 和 reservoir history\n"
            "  K：锁定/解除固定 camera、base seed 和动画起点；frame/sample index 仍继续推进\n"
            "  [ / ]：减少/增加 maximum bounce；- / =：降低/提高 exposure\n"
            "  PageDown / PageUp：降低/提高内部 render scale\n"
            "  F1：完整键位帮助；F2：Algorithm/Mode 面板\n"
            "  F3：GPU/CPU Profiler 面板；F4：保存 PNG、线性 EXR 和 metadata JSON\n"
            "  F5：事务式重载 shader，失败时保留旧 pipeline；F6：fixed-seed split-screen A/B\n"
            "  F7：debug overlay legend；F8：当前场景 Benchmark 入口；F9：当前算法 reference comparison\n"
            "  F10：打印当前/推荐组合、匹配状态、场景目的和算法简介\n"
            "  F11：Presentation 入口，原子恢复当前场景教学推荐；不锁定，之后仍可逐轴切换\n"
            "  F12 / Shift+F12：Diagnosis 入口，下一个/上一个当前场景诊断变体；保留算法轴与相机，Home 恢复变体机位\n"
            "\n"
            "算法状态展示（窗口标题与 ImGui 顶部始终显示）\n"
            "  Scene | Backend | Transport | Execution | DirectEstimator | LightSelection | EnvironmentSampler | Reconstruction | Debug | Shadow\n"
            "  Resolution | Seed | Frame | Film SPP | Paths/pixel/frame | Bounce | GPU ms\n"
            "\n"
            "展示空间（数字键只更换 scene 和默认相机，不改变算法 tuple）\n"
            "  0：Baseline Gallery；1：Intersection & BVH Lab；2：Whitted Optics Room\n"
            "  3：Cornell Box；4：GGX & MIS Material Lab；5：Environment Sampling Dome\n"
            "  6：Sponza Traversal Hall；7：Backend Parity Benchmark\n"
            "  8：Temporal Stability Corridor；9：Many Lights / ReSTIR Arena\n"
            "  若当前 tuple 不支持目标场景，场景选择会被拒绝，原 scene、camera 和算法均保持不变。\n"
            "  数字键换场景后可按 F10 检查偏离状态，按 F11 恢复该场景教学推荐；Home 才恢复相机。\n"
            "\n"
            "输入仲裁与 Reset Mask\n"
            "  离散切换只响应 GLFW_PRESS，不响应 GLFW_REPEAT；回调只入 ActionQueue，统一在帧起点提交。\n"
            "  ImGui 捕获键盘时只屏蔽连续相机输入；Tab/Esc、场景、算法、参数与 F1-F11 等全局离散键仍生效。\n"
            "  ImGui 捕获鼠标时滚轮/观察轴不穿透；窗口失焦时所有输入均不生效。\n"
            "  Mask 缩写：A=accumulation，T=temporal，Q=reservoir，P=profiler，AS=acceleration structures。\n"
            "  scene 切换/重载与 backend 变化：A|T|Q|P|AS。\n"
            "  F11 按实际字段差异合并 mask；Many-Lights tier/初始动画变化包含 AS，纯 Final View 变化不 reset。\n"
            "  transport/execution/light、resolution/scale/FOV、camera bookmark/Home/瞬移：A|T|Q|P；执行架构跨资源路径时再含 AS。\n"
            "  reconstruction/SVGF 参数：T|P；Current Frame 与 Progressive Mean Film 分离。\n"
            "  普通相机移动：只清 Progressive Mean Film；temporal/reservoir 使用重投影和局部拒绝。\n"
            "  Home / camera cut：清 Film、temporal 和 reservoir history。\n"
            "  稳定 ID 的灯光/刚体连续运动：不全清；依靠局部 history/reservoir validation。\n"
            "  增删物体/灯、稳定 ID 或材质拓扑改变：A|T|Q|P，必要时 AS。\n"
            "  base color/roughness/emission/light intensity/environment 突变，以及 seed/bounce/SPP/"
            "candidate 改变或 shader reload 成功：A|T|Q|P。\n"
            "  exposure/tone map/debug/profiler/help/capture：不 reset；R 始终请求 A|T|Q。";
    }
}
