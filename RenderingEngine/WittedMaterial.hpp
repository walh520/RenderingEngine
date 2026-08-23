//这是通用物体材质类，支持多种光照和渲染效果
#pragma once
#include <glm/glm.hpp>
#include "materials/MaterialBase.hpp"

namespace RenderingEngine
{
	class WittedMaterial : public MaterialBase
	{
	public:
		explicit WittedMaterial(const std::string& name = "default", const glm::vec3& albedo = glm::vec3(1.0f), const glm::vec3& emission = glm::vec3(0.0f), float reflectivity = 0.5f);
		MaterialType GetType() const override;
		std::string  GetShaderName() const override;
		void         BindToShader(Shader& shader) override;

		//Get系列函数
		std::string  GetName() const;
		glm::vec3    GetAlbedo() const;
		glm::vec3    GetEmission() const;
		float        GetReflectivity() const;

	private:
		std::string m_name;
		glm::vec3   m_albedo; // 默认“物体颜色”
		glm::vec3   m_emission; // 自发光颜色
		float       m_reflectivity; // 反射率
		//后续添加更多性质
	};
}