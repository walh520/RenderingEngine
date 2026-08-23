#pragma once
#include <glm/glm.hpp>
#include "Tool.hpp"
#include "GeometryBase.hpp"

class Cube : public GeometryBase
{
	bool Intersect(const RenderingEngine::Ray& ray, float& t) const override
	{

	}

	glm::vec3 Normal(const glm::vec3& p)const override
	{

	}
};
