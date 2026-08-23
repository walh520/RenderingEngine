#pragma once
#include <vector>
#include <glm/glm.hpp>
#include "scene/sphere.hpp"
#include "WittedMaterial.hpp"
#include "core/camera.h"
#include "SpecificScene_01.hpp"

extern const unsigned int SCR_WIDTH;
extern const unsigned int SCR_HEIGHT;

namespace RenderingEngine
{
	class WittedTracer
	{
	public:
		glm::vec3 background = glm::vec3(0.0f);
		glm::vec3 lightPos = glm::vec3(-1.0f, 0.0f, -3.0f);
		glm::vec3 lightColor = glm::vec3(1.0f, 1.0f, 1.0f);
		int maxDepth = 3;

		WittedTracer() = default;

		glm::vec3 trace(const Ray& ray, int depth, std::vector<std::shared_ptr<SceneObject>> scene)
		{
			if (depth <= 0) return background;

			float closest = std::numeric_limits<float>::max(); // float最大值
			std::shared_ptr<SceneObject> hitObject = nullptr;
			glm::vec3 hitPoint;

			for (auto& object : scene)
			{
				float t;
				if (object->Intersect(ray, t) && t < closest)
				{
					closest = t;
					hitObject = object;
				}
			}

			if (!hitObject)return background;

			hitPoint = ray.at(closest);
			glm::vec3 normal = hitObject->Normal(hitPoint);
			std::shared_ptr<MaterialBase> mat = hitObject->GetMaterial();

			glm::vec3 color = mat->GetAlbedo() * mat->GetEmission();
			color += mat->GetAlbedo() * 0.1f;

			if (!inShadow(hitPoint, normal, scene))
			{
				glm::vec3 lightDir = glm::normalize(lightPos - hitPoint);
				float diff = std::max(glm::dot(lightDir, normal), 0.0f);
				color += mat->GetAlbedo() * diff * lightColor;
			}

			if (mat->GetReflectivity() > 0.001f)
			{
				glm::vec3 reflectDir = glm::reflect(ray.direction, normal);
				Ray reflectRay(hitPoint + normal * 0.001f, reflectDir);
				glm::vec3 reflectColor = trace(reflectRay, depth - 1, scene);
				color = glm::mix(color, reflectColor, mat->GetReflectivity());
			}

			return color;
		}

		// 执行光线追踪渲染,返回像素颜色数组
		std::vector<glm::vec3> performRayTracing(Camera& camera, std::vector<std::shared_ptr<SceneObject>> scene)
		{
			std::vector<glm::vec3> pixels(SCR_WIDTH * SCR_HEIGHT);

			glm::mat4 view = camera.GetViewMatrix();
			glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom),
				(float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 100.0f);
			glm::mat4 invVP = glm::inverse(projection * view);

			for (int y = 0; y < SCR_HEIGHT; y++)
			{
				for (int x = 0; x < SCR_WIDTH; x++)
				{
					// 将像素坐标转换为NDC [-1, 1]
					float ndcX = (2.0f * x) / SCR_WIDTH - 1.0f;
					float ndcY = 1.0f - (2.0f * y) / SCR_HEIGHT;

					// 近平面和远平面的点
					glm::vec4 rayClipNear(ndcX, ndcY, -1.0f, 1.0f);
					glm::vec4 rayClipFar(ndcX, ndcY, 1.0f, 1.0f);

					// 直接转换到世界空间
					glm::vec4 rayWorldNear = invVP * rayClipNear;
					glm::vec4 rayWorldFar = invVP * rayClipFar;

					rayWorldNear /= rayWorldNear.w;
					rayWorldFar /= rayWorldFar.w;

					// 计算光线方向
					glm::vec3 rayDir = glm::normalize(glm::vec3(rayWorldFar) - glm::vec3(rayWorldNear));

					Ray ray(camera.Position, rayDir);
					pixels[y * SCR_WIDTH + x] = trace(ray, maxDepth, scene);
				}
			}
			return pixels;
		}
	private:
		bool inShadow(const glm::vec3& point, const glm::vec3& normal, std::vector<std::shared_ptr<SceneObject>> scene)
		{
			glm::vec3 lightDir = glm::normalize(lightPos - point);
			Ray shadowRay(point + normal * 0.001f, lightDir);

			for (auto& object : scene)
			{
				float t;
				if (object->Intersect(shadowRay, t))
				{
					float distToLight = glm::length(lightDir);
					if (t < distToLight) return true;
				}
			}
			return false;
		}
	};
}// namespace RenderingEngine
