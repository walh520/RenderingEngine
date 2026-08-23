#pragma once
#include <glm/glm.hpp>
#include <string>

namespace RenderingEngine
{
    // 在命名空间内前向声明 Shader
    class Shader;

    enum class MaterialType { BlinnPhong, Witted };

    class MaterialBase
    {
    public:
        virtual ~MaterialBase() = default;

        virtual MaterialType GetType() const = 0;
        virtual void         BindToShader(Shader& shader) = 0;
        virtual std::string  GetShaderName() const = 0;

        virtual std::string  GetName() const = 0;
        virtual glm::vec3    GetAlbedo() const = 0;
        virtual glm::vec3    GetEmission() const = 0;
        virtual float        GetReflectivity() const = 0;
    };

} // namespace RenderingEngine