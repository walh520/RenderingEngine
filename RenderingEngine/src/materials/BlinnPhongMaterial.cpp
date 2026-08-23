#include "materials/MaterialBase.hpp"
#include "materials/BlinnPhongMaterial.hpp"
#include "core/shader.h"
#include "core/camera.h"

extern RenderingEngine::Camera camera;
extern glm::vec3 lightPos;

namespace RenderingEngine
{
    BlinnPhongMaterial::BlinnPhongMaterial(const std::string& name) : name_(name)
    {
        // 构造函数实现
    }

    MaterialType BlinnPhongMaterial::GetType() const
    {
        return MaterialType::BlinnPhong;
    }

    std::string BlinnPhongMaterial::GetShaderName() const
    {
        return "blinn_phong";
    }

    void BlinnPhongMaterial::BindToShader(Shader& shader)
    {
        shader.use();
        // 设置光源和相机
        shader.setVec3("lightPos", lightPos);
        shader.setVec3("viewPos", camera.Position);
        shader.setVec3("lightColor", 1.0f, 1.0f, 1.0f);

        // 设置材质结构体
        shader.setVec3("material.albedo", albedo_);
        shader.setVec3("material.specular", specular_);
        shader.setFloat("material.shininess", shininess_);
        shader.setFloat("material.ambient", ambient_);
        //调试输出
        std::cout << "Material bound - Albedo: (" << albedo_.r << ", " << albedo_.g << ", " << albedo_.b << ")" << std::endl;
        std::cout << "Light position: (" << lightPos.x << ", " << lightPos.y << ", " << lightPos.z << ")" << std::endl;
        std::cout << "View position: (" << camera.Position.x << ", " << camera.Position.y << ", " << camera.Position.z << ")" << std::endl;
    }
} // namespace RenderingEngine