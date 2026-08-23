#include "scene/GpuScene.hpp"

namespace RenderingEngine
{
    SceneData CreateDemoScene()
    {
        SceneData scene;

        // baseColor + metallic, emission + perceptual roughness,
        // transmission + IOR, attenuation color + attenuation distance.
        // These values span dielectric, conductor, rough and smooth surfaces so
        // the demo scene also acts as a compact PBR material validation chart.
        scene.materials = {
            { { 0.80f, 0.055f, 0.035f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.28f }, { 0.0f, 1.50f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.0f } },
            { { 0.96f, 0.985f, 1.00f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.02f }, { 1.0f, 1.52f, 0.0f, 0.0f }, { 0.72f, 0.90f, 1.0f, 4.0f } },
            { { 1.00f, 0.710f, 0.290f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.16f }, { 0.0f, 1.50f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.0f } },
            { { 0.91f, 0.920f, 0.920f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.48f }, { 0.0f, 1.50f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.0f } },
            { { 0.55f, 0.570f, 0.620f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.65f }, { 0.0f, 1.50f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.0f } },
            { { 0.13f, 0.170f, 0.240f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.85f }, { 0.0f, 1.50f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.0f } },
            { { 0.08f, 0.520f, 0.180f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.78f }, { 0.0f, 1.50f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.0f } },
            { { 1.00f, 0.720f, 0.420f, 0.0f }, { 180.0f, 105.0f, 45.0f, 0.30f }, { 0.0f, 1.50f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.0f } },
            { { 0.35f, 0.550f, 1.000f, 0.0f }, { 45.0f, 80.0f, 180.0f, 0.30f }, { 0.0f, 1.50f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.0f } },
            { { 0.82f, 0.810f, 0.760f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.12f }, { 0.0f, 1.46f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.0f } }
        };

        scene.spheres = {
            { { -1.55f, -0.14f, -4.20f, 0.86f }, { 0, 0, 0, 0 } },
            { {  0.10f, -0.28f, -3.65f, 0.72f }, { 1, 0, 0, 0 } },
            { {  1.62f, -0.12f, -4.35f, 0.88f }, { 2, 0, 0, 0 } },
            { { -0.72f,  0.42f, -6.15f, 1.12f }, { 3, 0, 0, 0 } },
            { {  1.55f, -0.30f, -6.30f, 0.70f }, { 6, 0, 0, 0 } },
            { { -2.62f, -0.46f, -6.05f, 0.54f }, { 9, 0, 0, 0 } },
            { { -3.10f,  4.20f, -1.90f, 0.34f }, { 7, 0, 0, 0 } },
            { {  3.15f,  2.45f, -3.20f, 0.28f }, { 8, 0, 0, 0 } }
        };

        scene.planes = {
            { { 0.0f, 1.0f, 0.0f, 1.0f }, { 4, 1, 0, 0 } },
            { { 0.0f, 0.0f, 1.0f, 8.5f }, { 5, 0, 0, 0 } }
        };

        scene.lights = {
            { { -3.10f, 4.20f, -1.90f, 0.34f }, { 180.0f, 105.0f, 45.0f, 0.0f } },
            { {  3.15f, 2.45f, -3.20f, 0.28f }, { 45.0f, 80.0f, 180.0f, 0.0f } }
        };

        return scene;
    }
}
