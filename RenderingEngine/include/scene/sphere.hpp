#pragma once
#include <glm/glm.hpp>
#include "Tool.hpp"
#include "GeometryBase.hpp"

class Sphere : public GeometryBase
{
public:
	Sphere(const glm::vec3& c, float r) :center(c), radius(r) {}
	//球体的隐式方程: (P - C)·(P - C) - r^2 = 0
	//光线与球体求交
	bool Intersect(const RenderingEngine::Ray& ray, float& t)const
	{
		glm::vec3 oc = ray.origin - center;
		float a = glm::dot(ray.direction, ray.direction);
		float b = 2 * glm::dot(oc, ray.direction);
		float c = glm::dot(oc, oc) - radius * radius;

		float discriminant = b * b - 4 * a * c;
		if (discriminant < 0) return false;
		float t0 = (-b + sqrt(discriminant)) / (2.0f * a);
		float t1 = (-b - sqrt(discriminant)) / (2.0f * a);
		t = t0 > 0 ? t0 : t1;
		return t > 0;
	}
	//获取归一化法线
	glm::vec3 Normal(const glm::vec3& p)const
	{
		return glm::normalize(p - center);
	}
private:
	glm::vec3 center; //球心
	float radius;
};
