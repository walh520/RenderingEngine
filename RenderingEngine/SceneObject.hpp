//这个类代表了一个统一的场景对象，包含几何体和材质，并支持基本的变换操作。(就是一个通用的场景物体类)
#pragma once
#include "GeometryBase.hpp"
#include "materials/MaterialBase.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>

namespace RenderingEngine 
{
    class SceneObject 
    {
    public:
        SceneObject(const std::string& name, std::unique_ptr<GeometryBase> geometry, std::shared_ptr<MaterialBase> material);//把名称，几何体，材质绑定到场景中的同一个一个物体上

		//转发几何体的求交函数
        bool Intersect(const Ray& ray, float& t) const
        {
            return m_geometry->Intersect(ray, t);
        }
        //转发几何体的归一化法线函数
        glm::vec3 Normal(const glm::vec3& p)const
        {
            return m_geometry->Normal(p);
        }
        // 变换操作
        void SetPosition(const glm::vec3& position);
        void SetRotation(const glm::quat& rotation);
        void SetScale(const glm::vec3& scale);

        // 获取信息
        const std::string& GetName() const;
        std::unique_ptr<GeometryBase> GetGeometry();
        std::shared_ptr<MaterialBase> GetMaterial();
        const glm::mat4& GetTransform() const;
        
    private:
        std::string m_name;
        std::unique_ptr<GeometryBase> m_geometry;
        std::shared_ptr<MaterialBase> m_material;
        glm::mat4 m_transform = glm::mat4(1.0f);
    };
}
//std::unique_ptr<基类> 创建出来的是“基类指针类型”，但它可以指向派生类对象