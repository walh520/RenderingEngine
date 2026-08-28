#ifndef RENDERING_ENGINE_WAVEFRONT_TRAVERSAL_ADAPTER_HLSLI
#define RENDERING_ENGINE_WAVEFRONT_TRAVERSAL_ADAPTER_HLSLI

#if defined(WF_TRAVERSAL_SOFTWARE)
#include "SoftwareTraversalAdapter.hlsli"
#elif defined(WF_TRAVERSAL_RAY_QUERY)
#include "RayQueryTraversalAdapter.hlsli"
#else
// Default, self-contained compilation and numerical smoke-test fixture.
#include "FixtureTraversalAdapter.hlsli"
#endif

#endif
