#include "restir/VulkanProductionRuntime.hpp"

namespace RenderingEngine::Restir
{
    Renderers::ReSTIRRuntimeStatus RecordVulkanReSTIRProductionFrame(
        const Renderers::ReSTIRFramePlan& framePlan,
        const Renderers::VulkanReSTIRFrameContext& frameContext,
        const VulkanReSTIRProductionEvidence& evidence,
        Renderers::VulkanReSTIRRecorder& recorder,
        Rt::Gpu::IGpuTraversalBackend& traversalBackend)
    {
        if (!Renderers::ValidateReSTIRFramePlan(framePlan))
        {
            return { Renderers::ReSTIRRuntimeStatusCode::InvalidRequest,
                "refusing to attach an invalid ABI-v3 ReSTIR frame plan" };
        }
        const VulkanReSTIRProductionReport attachment =
            ValidateVulkanReSTIRProductionAttachment(
                framePlan, frameContext, evidence, traversalBackend);
        if (!attachment.IsReady())
        {
            return attachment.status;
        }
        if (!recorder.IsInitialized())
        {
            return { Renderers::ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "Vulkan ReSTIR recorder has no descriptor/pipeline owner" };
        }
        if (recorder.PipelineLayout() != evidence.pipelineLayout)
        {
            return { Renderers::ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "validated ABI-v3 pipeline layout is not the recorder pipeline layout" };
        }
        if (Renderers::ReSTIRRuntimeStatus status =
                recorder.SetFrameContext(frameContext); !status)
        {
            return status;
        }
        Renderers::ReSTIRDIRuntime runtime;
        return runtime.RecordFrame(framePlan, recorder, traversalBackend);
    }
}
