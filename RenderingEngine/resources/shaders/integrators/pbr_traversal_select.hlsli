#ifndef RENDERING_ENGINE_PBR_TRAVERSAL_SELECT_HLSLI
#define RENDERING_ENGINE_PBR_TRAVERSAL_SELECT_HLSLI

#if defined(PBR_L6_TRAVERSAL_SOFTWARE) && defined(PBR_L6_TRAVERSAL_RAY_QUERY)
#error Select exactly one production Megakernel traversal adapter.
#elif defined(PBR_L6_TRAVERSAL_SOFTWARE)
#include "pbr_software_traversal.hlsli"
#elif defined(PBR_L6_TRAVERSAL_RAY_QUERY)
#include "pbr_ray_query_traversal.hlsli"
#else
#include "pbr_fixture_traversal.hlsli"
#endif

#endif
