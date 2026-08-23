#pragma once
#include "Tool.hpp"

extern struct RenderingEngine::Ray;
class GeometryBase
{
public:
    virtual ~GeometryBase() = default;

    // 光线求交 - 返回是否命中
    virtual bool Intersect(const RenderingEngine::Ray& ray, float& t) const = 0;

    // 获取包围盒（可选，用于加速结构）

    // 获取材质
    
    // 设置材质

    //获取归一化法线
    virtual glm::vec3 Normal(const glm::vec3& p)const = 0;
};