// SceneDataTransporter.hpp
#pragma once
#include "core/shader.h"
#include "Scene.hpp"
#include "scene/sphere.hpp"
#include "WittedMaterial.hpp"
#include <type_traits>

namespace RenderingEngine
{
    // 几何体类型枚举
    enum class GeometryType 
    {
        SPHERE = 0,
        PLANE = 1,
        CUBE = 2,
        TRIANGLE = 3
    };

    // 材质类型枚举
    enum class MaterialType 
    {
        WITTED = 0,
        BLINN_PHONG = 1,
        DIELECTRIC = 2
    };

    // 统一的GPU几何体结构（支持多种类型）
    struct GPUGeometry 
    {
        GeometryType type;
        glm::vec3 data1;  // 球体: center | 平面: normal | 立方体: minCorner
        glm::vec3 data2;  // 球体: (radius, 0, 0) | 平面: point | 立方体: maxCorner
        glm::vec3 data3;  // 保留字段
        float extra[3];   // 额外参数
    };

    // 统一的GPU材质结构（支持多种类型）
    struct GPUMaterial 
    {
        MaterialType type;
        glm::vec3 albedo;
        glm::vec3 emission;
        glm::vec3 specular;
        float reflectivity;
        float roughness;
        float ior; // 折射率
        float padding;
    };

    // 统一的GPU场景对象
    struct GPUSceneObject 
    {
        GPUGeometry geometry;
        GPUMaterial material;
        glm::mat4 transform; // 变换矩阵
        int objectId;
    };

    class SceneDataTransporter
    {
    public:
        // 统一的场景上传方法
        static void UploadSceneToShader(Shader& shader, const std::vector<std::shared_ptr<SceneObject>>& scene)
        {
            std::vector<GPUSceneObject> gpuObjects;

            for (int i = 0; i < scene.size(); ++i) {
                const auto& obj = scene[i];
                GPUSceneObject gpuObj;
                gpuObj.objectId = i;
                gpuObj.transform = obj->GetTransform();

                // 转换几何体
                ConvertGeometryToGPU(obj->GetGeometry(), gpuObj.geometry);

                // 转换材质
                ConvertMaterialToGPU(obj->GetMaterial(), gpuObj.material);

                gpuObjects.push_back(gpuObj);
            }

            // 上传到Shader
            UploadGPUObjectsToShader(shader, gpuObjects);
        }

    private:
        // 几何体转换器
        static void ConvertGeometryToGPU(const std::unique_ptr<GeometryBase>& geometry, GPUGeometry& gpuGeometry)
        {
            // 球体转换
            if (auto sphere = dynamic_cast<Sphere*>(geometry.get())) {
                gpuGeometry.type = GeometryType::SPHERE;
                gpuGeometry.data1 = sphere->center;
                gpuGeometry.data2 = glm::vec3(sphere->radius, 0, 0);
            }
            // 可以在这里添加其他几何体类型的转换
            // else if (auto plane = dynamic_cast<Plane*>(geometry.get())) {
            //     gpuGeometry.type = GeometryType::PLANE;
            //     gpuGeometry.data1 = plane->normal;
            //     gpuGeometry.data2 = plane->point;
            // }
            else {
                // 未知几何体类型
                gpuGeometry.type = GeometryType::SPHERE;
                gpuGeometry.data1 = glm::vec3(0);
                gpuGeometry.data2 = glm::vec3(0);
            }
        }

        // 材质转换器
        static void ConvertMaterialToGPU(const std::shared_ptr<MaterialBase>& material, GPUMaterial& gpuMaterial)
        {
            // Witted材质转换
            if (auto witted = std::dynamic_pointer_cast<WittedMaterial>(material)) {
                gpuMaterial.type = MaterialType::WITTED;
                gpuMaterial.albedo = witted->GetAlbedo();
                gpuMaterial.emission = witted->GetEmission();
                gpuMaterial.reflectivity = witted->GetReflectivity();
                gpuMaterial.roughness = 0.0f;
                gpuMaterial.ior = 1.0f;
            }
            // 可以在这里添加其他材质类型的转换
            // else if (auto blinnPhong = std::dynamic_pointer_cast<BlinnPhongMaterial>(material)) {
            //     gpuMaterial.type = MaterialType::BLINN_PHONG;
            //     gpuMaterial.albedo = blinnPhong->GetDiffuse();
            //     gpuMaterial.specular = blinnPhong->GetSpecular();
            //     gpuMaterial.roughness = blinnPhong->GetShininess();
            // }
            else {
                // 默认材质
                gpuMaterial.type = MaterialType::WITTED;
                gpuMaterial.albedo = glm::vec3(0.8f);
                gpuMaterial.emission = glm::vec3(0.0f);
                gpuMaterial.reflectivity = 0.5f;
            }
        }

        // 上传到Shader的统一方法
        static void UploadGPUObjectsToShader(Shader& shader, const std::vector<GPUSceneObject>& gpuObjects)
        {
            shader.setInt("u_ObjectCount", static_cast<int>(gpuObjects.size()));

            for (size_t i = 0; i < gpuObjects.size(); ++i) {
                const auto& obj = gpuObjects[i];
                std::string baseName = "u_Objects[" + std::to_string(i) + "]";

                // 上传几何体信息
                shader.setInt(baseName + ".geometry.type", static_cast<int>(obj.geometry.type));
                shader.setVec3(baseName + ".geometry.data1", obj.geometry.data1);
                shader.setVec3(baseName + ".geometry.data2", obj.geometry.data2);
                shader.setVec3(baseName + ".geometry.data3", obj.geometry.data3);
                shader.setFloat(baseName + ".geometry.extra[0]", obj.geometry.extra[0]);
                shader.setFloat(baseName + ".geometry.extra[1]", obj.geometry.extra[1]);
                shader.setFloat(baseName + ".geometry.extra[2]", obj.geometry.extra[2]);

                // 上传材质信息
                shader.setInt(baseName + ".material.type", static_cast<int>(obj.material.type));
                shader.setVec3(baseName + ".material.albedo", obj.material.albedo);
                shader.setVec3(baseName + ".material.emission", obj.material.emission);
                shader.setVec3(baseName + ".material.specular", obj.material.specular);
                shader.setFloat(baseName + ".material.reflectivity", obj.material.reflectivity);
                shader.setFloat(baseName + ".material.roughness", obj.material.roughness);
                shader.setFloat(baseName + ".material.ior", obj.material.ior);

                // 上传变换矩阵
                shader.setMat4(baseName + ".transform", obj.transform);
                shader.setInt(baseName + ".objectId", obj.objectId);
            }
        }
    };
}