//这个场景包含一个简单的球体，用于测试基本的渲染功能。
#pragma once
#include "Scene.hpp"
#include "SceneObject.hpp"
#include "scene/sphere.hpp"

namespace RenderingEngine
{
	class SpecificScene_01 : public Scene
	{
	public:
		SpecificScene_01()
		{
			BuildAllObjects();
			BuildStructure();
		}
		~SpecificScene_01() = default;

		void AddObject(std::shared_ptr<SceneObject> object, std::vector<std::shared_ptr<SceneObject>> objects)
		{
			m_objects.push_back(object);
		}
		void RemoveObject(const std::string& name)
		{

		}
		SceneObject* GetObject(const std::string& name) const
		{
			return nullptr;
		}

		void BuildStructure() override
		{
			AddObject(sphere, m_objects);
		}

		void BuildAllObjects() override
		{
			sphere = std::make_shared<SceneObject>("TestSphere", std::make_unique<Sphere>(glm::vec3(0.0f, 0.0f, -3.0f), 0.5f),
				std::make_shared<RenderingEngine::WittedMaterial>("RedMaterial", glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0.5f));
		}

		const std::vector<std::shared_ptr<SceneObject>>& GetScene() const
		{
			return m_objects;
		}
	private:
		std::shared_ptr<SceneObject> sphere;//场景中的球体对象
		std::vector<std::shared_ptr<SceneObject>> m_objects;//存储场景中的所有对象
	};
}