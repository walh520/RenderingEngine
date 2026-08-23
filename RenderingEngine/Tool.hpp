//这是工具函数包，包含一些常用的渲染相关的数据结构和函数。头文件内含定义，无需再次声明
#pragma once
#include<glm/glm.hpp>
namespace RenderingEngine
{
	struct Ray
	{
		glm::vec3 origin;    //射线的起点
		glm::vec3 direction; //射线的方向
		glm::vec3 invDirection; //射线方向的倒数，用于加速计算

		Ray(const glm::vec3& orig, const glm::vec3& dir) : origin(orig), direction(glm::normalize(dir))
		{
			invDirection = glm::vec3(1.0f) / direction;
		}

		glm::vec3 at(float t) const// 防止意外修改
		{
			return origin + t * direction;
		}
	};
}