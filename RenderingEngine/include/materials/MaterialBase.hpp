#pragma once
#include <glm/glm.hpp>
#include <string>

namespace RenderingEngine
{
    // 在命名空间内前向声明 Shader
    class Shader;

    enum class MaterialType { BlinnPhong, PBR, Emissive };

    class MaterialBase
    {
    public:
        virtual ~MaterialBase() = default;

        virtual MaterialType GetType() const = 0;
        virtual void         BindToShader(Shader& shader) = 0;
        virtual std::string  GetShaderName() const = 0;

    protected:
        glm::vec3   albedo_{ 1.0f };
        std::string name_;
    };

} // namespace RenderingEngine