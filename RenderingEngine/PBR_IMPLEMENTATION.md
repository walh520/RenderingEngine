# PBR implementation record

This document describes the application's startup-default and primary-delivery
PBR path. `--integrator whitted` remains an explicit compatibility and A/B
comparison mode.

## Selected reference stack

This renderer uses one coherent metallic-roughness transport model rather than
copying unrelated effects from several engines:

1. glTF 2.0 defines the authored material contract: base color, metallic, and
   perceptual roughness.
2. Filament and Epic's real-time PBR notes provide the Cook-Torrance GGX,
   height-correlated Smith, and Schlick formulation used for opaque surfaces.
3. Heitz 2018 provides exact visible-normal sampling for GGX, used by indirect
   reflection paths.
4. PBRT provides the reference structure for Monte Carlo path transport,
   throughput updates, next-event estimation, and Russian roulette.
5. Khronos PBR Neutral is the display transform.

Disney's principled shading paper was reviewed for artist-facing parameter
design. The active baseline deliberately stays with glTF metallic-roughness and
Lambert diffuse so the scene record and rendered semantics remain portable.

## Source-to-code map

| Requirement | Implementation | Authoritative file |
| --- | --- | --- |
| Material parameters | Linear baseColor, metallic, roughness, emissive, transmission, IOR, attenuation | include/scene/GpuScene.hpp |
| Validation values | Dielectric, conductor, glass, roughness, emissive fixtures | src/scene/GpuScene.cpp |
| GGX distribution | DistributionGGX | resources/shaders/pbr/PbrPathTrace.hlsl |
| Correlated masking-shadowing | VisibilitySmithGGXCorrelated | resources/shaders/pbr/PbrPathTrace.hlsl |
| Fresnel | FresnelSchlick and FresnelDielectric | resources/shaders/pbr/PbrPathTrace.hlsl |
| Opaque BRDF | EvaluateOpaqueBrdf | resources/shaders/pbr/PbrPathTrace.hlsl |
| GGX VNDF sampling | SampleGGXVisibleNormal | resources/shaders/pbr/PbrPathTrace.hlsl |
| Direct finite lights | SampleSphereLight and EvaluateDirectLighting | resources/shaders/pbr/PbrPathTrace.hlsl |
| Indirect transport | TracePath | resources/shaders/pbr/PbrPathTrace.hlsl |
| Beer-Lambert absorption | ApplyVolumeAttenuation | resources/shaders/pbr/PbrPathTrace.hlsl |
| Progressive accumulation | CSMain plus accumulationFrame_ | shader and src/renderers/VulkanWhittedRenderer.cpp |
| HDR/display transform | RGBA32F plus PbrNeutralToneMap and LinearToSrgb | renderer and resources/shaders/whitted/Present.hlsl |

Both compute shaders are built. The runtime loads PbrPathTrace.comp.spv by
default and loads WhittedTrace.comp.spv only when `--integrator whitted` is
requested.

## Opaque BRDF

For view direction v, light direction l, shading normal n, and half vector h:

    f(v,l) = diffuse + F(v,h) D(n,h) V(n,v,n,l)

Perceptual roughness r is remapped to alpha = max(r squared, 0.002).

The GGX distribution is:

    D = alpha squared /
        (pi * ((n dot h) squared * (alpha squared - 1) + 1) squared)

The height-correlated Smith visibility term is evaluated directly, so the
specular term is F * D * V without another 1/(4 NoV NoL) factor.

Dielectric F0 is derived from IOR:

    F0 = ((ior - 1) / (ior + 1)) squared

Metal F0 is baseColor. Intermediate metallic values interpolate between those
endpoints. Diffuse energy is multiplied by (1 - F), (1 - metallic), and
(1 - transmission).

## Sampling and transport

- Diffuse directions use cosine-weighted hemisphere sampling.
- Specular directions use the visible GGX normal distribution from Heitz.
- Their PDFs are combined with the same data-dependent mixture probability
  used to select a lobe.
- Each opaque bounce updates throughput by BRDF * NoL / PDF.
- Every finite sphere emitter is sampled uniformly over its visible solid
  angle for next-event estimation.
- Emitter hits reached through non-delta BSDF sampling are suppressed because
  direct light is already estimated explicitly. Camera and delta-path emitter
  hits remain visible.
- Environment radiance is evaluated on path escape.
- Russian roulette begins after the third visited surface and compensates
  surviving paths by the continuation probability.

One independent sample per pixel is added per frame using an online arithmetic
mean. The sample sequence is reset after any camera/FOV/resize/shadow-mode
change and stops at 4096 spp to avoid a permanently running dispatch.

## Transmission

Transmission is a smooth dielectric extension:

- Exact unpolarized dielectric Fresnel selects reflection or refraction.
- The refracted branch uses Snell's law through HLSL refract.
- Total internal reflection is handled by Fresnel returning one.
- When a ray exits a transmissive surface, traveled interior distance applies
  the glTF volume-style attenuation color over a reference distance.

Rough dielectric BTDF, nested media, dispersion, and spectral absorption are
outside the current analytic renderer scope and are not silently approximated
as opaque GGX.

## Light and display units

GpuLight.radiance and emissive material RGB store emitted radiance in linear
Rec. 709. The physical estimator integrates the finite sphere's visible solid
angle; it does not apply an extra inverse-square point-light factor. Distance
falloff follows from the subtended solid angle.

The accumulated image is linear HDR RGBA32F. Presentation applies user
exposure, Khronos PBR Neutral, and then:

- lets an sRGB swapchain encode linear RGB in hardware; or
- applies the exact piecewise linear-to-sRGB function for a UNORM fallback.

## Why the legacy shadow modes remain

PCF and PCSS predate the physical estimator and are useful A/B diagnostics.
They trace multiple deterministic rays to an emitter disk, then multiply a
center-light BRDF estimate by visibility. They are intentionally labeled
legacy comparison modes. Physical mode performs stochastic solid-angle light
sampling and is the startup default.

## Verification contract

The automated suite in tools/Validate-Pbr.ps1 proves:

1. DXC compiles the active compute and presentation shaders with warnings as
   errors.
2. The C++ target compiles and links with warnings as errors.
3. Startup without an integrator argument reports the PBR path tracer,
   guarding the default-path contract.
4. A real Vulkan device creates all resources and renders PBR physical, PCF,
   PCSS, every material debug view, seed/target-SPP, VSync, and swapchain resize
   cases; an explicit Whitted invocation remains as a compatibility smoke test.
5. Swapchain/output recreation survives two programmed resizes.

Visual acceptance remains a separate gate. The expected observations are:

- dielectric spheres retain diffuse color plus white dielectric reflection;
- gold and silver lose dielectric diffuse and use colored conductor F0;
- lower roughness produces tighter reflection/highlight structure;
- glass reflects, refracts, and accumulates blue-tinted absorption by distance;
- sphere emitters are visible and create finite-size penumbrae in physical
  mode;
- noise converges while static and accumulation restarts after camera input.

## Primary references

- glTF 2.0 specification, Materials:
  https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/Specification.adoc
- Google Filament, Physically Based Rendering:
  https://google.github.io/filament/Filament.md.html
- Brian Karis, Real Shading in Unreal Engine 4:
  https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf
- Eric Heitz, Sampling the GGX Distribution of Visible Normals:
  https://jcgt.org/published/0007/04/01/
- Physically Based Rendering, 4th edition, microfacet theory:
  https://www.pbr-book.org/4ed/Reflection_Models/Roughness_Using_Microfacet_Theory
- Disney, Physically Based Shading at Disney:
  https://disneyanimation.com/publications/physically-based-shading-at-disney/
- Khronos PBR Neutral tone mapper:
  https://github.com/KhronosGroup/ToneMapping/blob/main/PBR_Neutral/README.md
