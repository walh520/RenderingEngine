# Third-party references

No third-party binary, texture, or model is vendored by this PBR change. Most
of the implementation is an original integration of published equations and
documented material semantics. The presentation shader's `PbrNeutralToneMap`
function is an HLSL adaptation of Khronos' short GLSL reference function and is
identified in that shader as modified Apache-2.0 code.

The following works were consulted and must retain their own upstream
copyright and license terms if upstream source code is copied in the future:

| Work | Use in this project | Upstream |
| --- | --- | --- |
| Khronos glTF 2.0 | Metallic-roughness and volume parameter semantics | https://github.com/KhronosGroup/glTF |
| Google Filament | PBR equation/reference validation | https://github.com/google/filament |
| PBRT v4 | Path-transport and microfacet reference validation | https://github.com/mmp/pbrt-v4 |
| Khronos ToneMapping | `Present.hlsl` adapts `PBR_Neutral/pbrNeutral.glsl` to HLSL; Apache-2.0 | https://github.com/KhronosGroup/ToneMapping |
| Heitz, JCGT 2018 | Published GGX visible-normal sampling algorithm | https://jcgt.org/published/0007/04/01/ |
| Epic SIGGRAPH 2013 notes | Real-time GGX design reference | https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf |
| Disney SIGGRAPH 2012 notes | Artist-facing principled material research | https://disneyanimation.com/publications/physically-based-shading-at-disney/ |

The upstream Khronos ToneMapping license is available at:
https://github.com/KhronosGroup/ToneMapping/blob/main/LICENSE.md

Except for the explicitly identified PBR Neutral adaptation, this notice is
attribution and provenance documentation, not a claim that the renderer
redistributes upstream project source. Consult each linked project or
publication before incorporating additional source or assets.
