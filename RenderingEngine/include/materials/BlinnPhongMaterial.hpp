//这是传统的Blinn-Phong材质模型实现，包含漫反射、镜面反射和环境光成分
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "materials/MaterialBase.hpp"
#include "core/camera.h"

#include <iostream>

namespace RenderingEngine
{
    class BlinnPhongMaterial : public MaterialBase
    {
    public:
        explicit BlinnPhongMaterial(const std::string& name = "DefaultBP");

        MaterialType GetType() const override;
        std::string  GetShaderName() const override;
        void         BindToShader(Shader& shader) override;

        // 材质参数设置 
        void SetAlbedo(const glm::vec3& albedo) { albedo_ = albedo; } 
        void SetSpecular(const glm::vec3& spec) { specular_ = spec; }
        void SetShininess(float shininess) { shininess_ = shininess; } 
        void SetAmbient(float ambient) { ambient_ = ambient; }   

    private:
        std::string name_;
        glm::vec3   albedo_{ 1.0f };    // 物体的基础颜色
		glm::vec3   specular_{ 0.5f };  // 镜面反射颜色和强度
		float       shininess_{ 32.0f };// 高光系数
		float       ambient_{ 0.1f };   // 环境光强度
    };

} // namespace RenderingEngine