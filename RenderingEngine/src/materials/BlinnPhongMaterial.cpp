#include "materials/MaterialBase.hpp"
#include "materials/BlinnPhongMaterial.hpp"
#include <shader.h>
#include <camera.h>

extern Camera camera;
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
        shader.setVec3("lightPos", lightPos);
        shader.setVec3("viewPos", camera.Position);
        shader.setVec3("objectColor", albedo_);
        shader.setVec3("lightColor", 1.0f, 1.0f, 1.0f);

    }
} // namespace RenderingEngine