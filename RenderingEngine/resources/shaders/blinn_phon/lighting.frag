#version 330 core
out vec4 FragColor;

in vec3 Normal;  
in vec3 FragPos;  

uniform vec3 lightPos; 
uniform vec3 viewPos; 
uniform vec3 lightColor;

struct Material 
{
    vec3 albedo;           // 漫反射颜色
    vec3 specular;         // 镜面反射颜色
    float shininess;       // 高光系数
    float ambient;         // 环境光强度
};

uniform Material material;

void main()
{
    // ambient
    vec3 ambient = material.ambient * lightColor;
  	
    // diffuse 
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;
    
    // specular
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    vec3 specular = material.specular * spec * lightColor;  
        
    vec3 result = (ambient + diffuse + specular) * material.albedo;
    FragColor = vec4(result, 1.0);
}