//场景构建核心，可以添加和管理场景中的对象，但是仍然是工具函数类，具体的一个可渲染场景还需要继承这个类并实现具体的功能
#pragma once
#include "SceneObject.hpp"
#include <vector>
#include <memory>
#include <unordered_map>

namespace RenderingEngine 
{
    class Scene 
    {
    public:
        virtual void AddObject(std::shared_ptr<SceneObject> object, std::vector<std::shared_ptr<SceneObject>> objects) = 0;
        virtual void RemoveObject(const std::string& name) = 0;
        virtual SceneObject* GetObject(const std::string& name) const = 0;

		// 场景构建(这个函数要构建场景的整体结构，比如添加光源，设置环境参数等，具体的几何体构建交给BuildAllObjects函数)
        virtual void BuildStructure() = 0;

		// 场景中的所有对象构建(这个函数要把所有对象的几何体构建好，构建几何体的时候同时会绑定材质)
		virtual void BuildAllObjects() = 0;

		// 获取存储场景中所有对象的容器
		virtual const std::vector<std::shared_ptr<SceneObject>>& GetScene() const = 0;
        // 光源查询

        // 场景信息

    private:
		std::vector<std::shared_ptr<SceneObject>> m_objects; //存储场景中的所有对象
    };

} // namespace RenderingEngine