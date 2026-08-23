#include "SceneObject.hpp"
#include <string>
#include <memory>
#include <glm/gtc/matrix_transform.hpp>


namespace RenderingEngine
{
    SceneObject::SceneObject(const std::string& name, std::unique_ptr<GeometryBase> geometry, std::shared_ptr<MaterialBase> material)
        :m_name(name), m_geometry(std::move(geometry)), m_material(material)
    {

    }
    // 变换操作
    void SceneObject::SetPosition(const glm::vec3& position)
    {

    }
    void SceneObject::SetRotation(const glm::quat& rotation)
    {

    }
    void SceneObject::SetScale(const glm::vec3& scale)
    {

    }
    // 获取信息
    const std::string& SceneObject::GetName() const { return m_name; }
    std::unique_ptr<GeometryBase> SceneObject::GetGeometry() { return std::move(m_geometry); }
    std::shared_ptr<MaterialBase> SceneObject::GetMaterial() { return m_material; }
    const glm::mat4& SceneObject::GetTransform() const { return m_transform; }
}