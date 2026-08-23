#include"WittedMaterial.hpp"
#include"materials/MaterialBase.hpp"

namespace RenderingEngine
{
	WittedMaterial::WittedMaterial(const std::string& name, const glm::vec3& albedo, const glm::vec3& emission, float reflectivity) :
		m_name(name), m_albedo(albedo), m_emission(emission), m_reflectivity(reflectivity)
	{
		// 构造函数实现
	}
	MaterialType WittedMaterial::GetType() const
	{
		return MaterialType::Witted;
	}
	std::string WittedMaterial::GetShaderName() const
	{
		return "RayTracingShader";
	}
	void WittedMaterial::BindToShader(Shader& shader)
	{
		// 绑定材质属性到着色器
	}

	std::string  WittedMaterial::GetName() const { return m_name; }
	glm::vec3    WittedMaterial::GetAlbedo() const { return m_albedo; }
	glm::vec3    WittedMaterial::GetEmission() const { return m_emission; }
	float        WittedMaterial::GetReflectivity() const { return m_reflectivity; }
}