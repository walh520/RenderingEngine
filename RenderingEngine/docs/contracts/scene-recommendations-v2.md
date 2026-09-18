# Scene teaching recommendations v2

Status: active RuntimeConfig v2 debug-showcase policy.

These are stable teaching configurations for 1280x720, not universal quality
or performance optima.

| Scene | Stable ID | Backend | Transport | Execution | Direct | Light selection | Environment sampler | Reconstruction | Bounce |
|---|---|---|---|---|---|---|---|---|---:|
| 0 Baseline | `scene-recommended.baseline.v2` | Canonical Linear | PBR | Staged | NEE | Uniform | Uniform Sphere | Raw | 8 |
| 1 Intersection & BVH | `scene-recommended.intersection-bvh.v2` | Flattened SAH | PBR | Staged | NEE | Uniform | Uniform Sphere | Raw | 1 |
| 2 Whitted Optics | `scene-recommended.whitted-optics.v2` | Ray Query | Whitted | Staged | NEE | Uniform | Uniform Sphere | Raw | 12 |
| 3 Cornell | `scene-recommended.cornell.v2` | Flattened SAH | PBR | Staged | MIS | Uniform | Uniform Sphere | Raw | 8 |
| 4 GGX & MIS | `scene-recommended.ggx-mis.v2` | Ray Query | PBR | Megakernel | MIS | Power | Uniform Sphere | Raw | 8 |
| 5 Environment Dome | `scene-recommended.environment-dome.v2` | Ray Query | PBR | Staged | MIS | Power | Importance Map | Raw | 8 |
| 6 Sponza | `scene-recommended.sponza.v2` | Ray Query | PBR | Wavefront | MIS | Power | Uniform Sphere | Raw | 8 |
| 7 Backend Parity | `scene-recommended.backend-parity.v2` | Canonical Linear | PBR | Staged | NEE | Uniform | Uniform Sphere | Raw | 4 |
| 8 Temporal Stability | `scene-recommended.temporal-stability.v2` | Ray Query | PBR | Wavefront | MIS | Power | Uniform Sphere | SVGF | 6 |
| 9 Many Lights | `scene-recommended.many-lights.v2` | Ray Query | PBR | Wavefront | ReSTIR DI | Power | Uniform Sphere | SVGF | 4 |

All profiles select Final view and Physical shadows. Scene 9 also restores 100
lights, temporal-spatial reuse, explicitly-biased mode, one candidate, five
neighbors, M=32, history age 20, comparison budget 8/1, and disables light and
occluder animation. Sponza remains fail-closed until its pinned asset/provider
exists.

`0`-`9` change only the scene. `F10` prints current/recommended split-axis
tuples. `F11` constructs and validates the full recommendation, then performs
one atomic exchange. Repeating F11 on a match is a no-op. `Home` remains the
only camera-reset key.
