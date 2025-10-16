#pragma once
#include "materials/MaterialBase.hpp"
#include <glm/glm.hpp>

namespace RenderingEngine
{

    class BlinnPhongMaterial : public MaterialBase
    {
    public:
        explicit BlinnPhongMaterial(const std::string& name = "DefaultBP");

        MaterialType GetType() const override;
        std::string  GetShaderName() const override;
        void         BindToShader(Shader& shader) override;

        // 材质参数 setter 
        void SetAlbedo(const glm::vec3& albedo) { albedo_ = albedo; }
        void SetSpecular(const glm::vec3& spec) { specular_ = spec; }
        void SetShininess(float shininess) { shininess_ = shininess; }
        void SetAmbient(float ambient) { ambient_ = ambient; }

    private:
        std::string name_;
        glm::vec3   albedo_{ 1.0f, 0.5f, 0.31f };   // 默认“物体颜色”
        glm::vec3   specular_{ 0.5f };
        float       shininess_{ 32.0f };
        float       ambient_{ 0.1f };   // 环境光强度
    };

} // namespace RenderingEngine