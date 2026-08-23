# Vulkan 现代光追渲染器：最终目标、算法路线与本地 Agent 复现规格

> 文档用途：作为本地 Coding / Research Agent 的长期实现说明书。  
> 项目定位：Vulkan 实时渲染器 + 现代实时光线追踪研究型作品集。  
> 核心原则：**先保证经典 Monte Carlo Path Tracing 正确，再逐步引入 GPU Wavefront、SVGF、ReSTIR DI。不要一开始把多个高级算法混在一起。**

---

## 0. 最终目标

最终完成一个具有两套 Ray Tracing Backend 的 Vulkan Renderer：

```text
Vulkan Renderer
│
├── Raster Pipeline
│   ├── Depth / GBuffer
│   ├── PBR / GGX
│   ├── Motion Vector
│   └── Primary Visibility
│
├── Software Ray Tracing Backend
│   ├── AABB
│   ├── SAH BVH
│   ├── GPU LBVH
│   └── Compute Shader Traversal
│
├── Vulkan Hardware RT Backend
│   ├── BLAS / TLAS
│   ├── VK_KHR_acceleration_structure
│   ├── VK_KHR_ray_query
│   └── VK_KHR_ray_tracing_pipeline
│
├── Reference Path Tracer
│   ├── Monte Carlo
│   ├── GGX Importance Sampling
│   ├── Next Event Estimation
│   ├── MIS
│   ├── Russian Roulette
│   └── High-SPP Ground Truth
│
├── GPU Path Tracer
│   └── Wavefront Architecture
│
├── Real-Time Reconstruction
│   ├── 1 SPP / few-SPP
│   ├── Reprojection
│   ├── Temporal Accumulation
│   ├── Variance Estimation
│   ├── A-Trous
│   └── SVGF
│
└── Modern Sampling
    ├── Reservoir Sampling
    ├── RIS
    ├── ReSTIR DI
    ├── Temporal Reuse
    └── Spatial Reuse
```

最终作品集应能展示：

1. 高 SPP Path Tracing Reference。
2. 1 SPP 原始噪声结果。
3. 1 SPP + SVGF。
4. ReSTIR DI + SVGF。
5. Software BVH 与 Vulkan Hardware RT 的性能对比。
6. 动态光源、动态相机、动态物体情况下的 Temporal Stability。
7. Debug View：BVH、Normal、Depth、Motion Vector、Variance、History Length、Reservoir、Selected Light、Ray Count。
8. GPU Profiler / timestamp query 的阶段耗时。
9. Cornell Box、Sponza、Many Lights、Glossy Area Light 等标准测试场景。

---

# 1. 实现优先级

## P0：光追基础与可验证基线

必须完成：

- Ray / Triangle Intersection
- Ray / AABB Intersection
- Whitted-style Ray Tracing
- Shadow Ray
- Mirror Reflection
- Refraction / Fresnel
- CPU BVH
- SAH BVH
- 可切换的软件光追 Backend

Whitted 不作为最终算法，而作为：

- BVH Traversal 正确性基线
- Reflection / Refraction 正确性测试
- Material / Ray Payload 测试
- 与后续 Monte Carlo Path Tracing 对照

### Whitted 原论文

Turner Whitted, *An Improved Illumination Model for Shaded Display*, 1980.

- ACM DOI  
  https://doi.org/10.1145/358876.358882
- 可公开访问的课程 PDF  
  https://courses.cs.washington.edu/courses/cse557/06wi/lectures/whitted-ray-tracing-cacm-1980.pdf

---

# 2. Rendering Equation：整个渲染器的数学核心

所有后续算法都必须从 Rendering Equation 出发，而不是把 Path Tracing 当成“随机反弹”。

表面形式：

\[
L_o(x,\omega_o)
=
L_e(x,\omega_o)
+
\int_{\Omega^+}
f_r(x,\omega_i,\omega_o)
L_i(x,\omega_i)
|n\cdot\omega_i|
\,d\omega_i
\]

其中：

- \(L_o\)：出射 Radiance
- \(L_e\)：自发光
- \(f_r\)：BSDF / BRDF
- \(L_i\)：入射 Radiance
- \(|n\cdot\omega_i|\)：投影余弦项

Path Tracing 的本质：

> 使用 Monte Carlo 方法求解递归 Rendering Equation。

### 必读

PBRT 4e — The Light Transport Equation  
https://www.pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/The_Light_Transport_Equation

PBRT 4e — Light Transport I  
https://www.pbr-book.org/4ed/Light_Transport_I_Surface_Reflection

Kajiya Path Tracing 的历史说明可从 PBRT Further Reading 继续查询：  
https://www.pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/Further_Reading

---

# 3. Monte Carlo Integration

必须真正理解并实现统一的：

```cpp
Sample
PDF
Evaluate
Estimator
```

对于：

\[
I=\int_\Omega f(x)\,dx
\]

若：

\[
X_i\sim p(x)
\]

则 Monte Carlo estimator：

\[
\hat I_N
=
\frac{1}{N}
\sum_{i=1}^{N}
\frac{f(X_i)}{p(X_i)}
\]

要求：

\[
p(x)>0
\]

对于所有 \(f(x)\neq0\) 的区域成立。

## Agent 必须检查

- Sample 与 PDF 必须严格匹配。
- 不允许“采样函数改了但 PDF 没改”。
- 所有 BSDF sample 函数返回：
  - sampled direction
  - BSDF value
  - PDF
  - lobe / flags
- 使用离线统计测试验证 PDF。
- 测试 Monte Carlo 误差随样本数增加而下降。

### 资料

PBRT 4e — Monte Carlo Integration  
https://pbr-book.org/4ed/Monte_Carlo_Integration

PBRT 4e — Monte Carlo Basics  
https://pbr-book.org/4ed/Monte_Carlo_Integration/Monte_Carlo_Basics

PBRT 4e — Improving Efficiency  
https://pbr-book.org/4ed/Monte_Carlo_Integration/Improving_Efficiency

---

# 4. Importance Sampling

目标：

> 让 PDF 尽可能接近被积函数的高贡献区域，从而降低 variance。

Monte Carlo 项：

\[
\frac{f(X)}{p(X)}
\]

如果 \(p(X)\) 与高贡献区域严重不匹配，就会产生 firefly / 高方差样本。

必须依次实现：

1. Uniform Hemisphere Sampling
2. Cosine Weighted Hemisphere Sampling
3. GGX / Trowbridge-Reitz Importance Sampling
4. Light Sampling
5. Environment Map Importance Sampling

## Diffuse cosine sampling

Lambert：

\[
f_r=\frac{\rho}{\pi}
\]

Cosine hemisphere PDF：

\[
p(\omega_i)=\frac{\cos\theta_i}{\pi}
\]

于是：

\[
\frac{f_r\cos\theta_i}{p(\omega_i)}
=
\rho
\]

这是理解 Importance Sampling 的最好基础例子。

资料：

https://pbr-book.org/4ed/Monte_Carlo_Integration/Improving_Efficiency

---

# 5. GGX / Trowbridge-Reitz Microfacet BRDF

最终 Path Tracer 不要停留在 Lambert + Perfect Mirror。

应实现至少：

- Lambert diffuse
- GGX conductor
- GGX dielectric
- Fresnel
- Smith masking-shadowing
- Rough reflection
- Rough transmission
- Visible Normal Sampling 优先

Microfacet BRDF：

\[
f_r(\omega_i,\omega_o)
=
\frac{
F(\omega_i,h)
D(h)
G(\omega_i,\omega_o)
}{
4|n\cdot\omega_i||n\cdot\omega_o|
}
\]

各项：

- \(F\)：Fresnel
- \(D\)：Normal Distribution Function
- \(G\)：Geometric masking-shadowing

Isotropic GGX 常见形式：

\[
D_{GGX}(h)
=
\frac{\alpha^2}
{\pi
\left[
(n\cdot h)^2(\alpha^2-1)+1
\right]^2}
\]

注意：

> Sampling GGX 时不能只实现 BRDF Evaluate；Sample 与 PDF 同样是核心组成。

### 必读

PBRT 4e — Roughness Using Microfacet Theory  
https://www.pbr-book.org/4ed/Reflection_Models/Roughness_Using_Microfacet_Theory

Agent 查询关键词：

```text
Trowbridge Reitz
GGX VNDF
Visible Normal Sampling
Smith masking shadowing
microfacet BRDF PDF
microfacet reflection Jacobian
```

---

# 6. Path Tracing

## 6.1 基础递归形式

基本状态：

```text
Ray
 ↓
Intersect
 ↓
Emission
 ↓
Sample BSDF
 ↓
Update Throughput
 ↓
Spawn Next Ray
```

Path throughput：

\[
\beta_{k+1}
=
\beta_k
\frac{
f_r(\omega_i,\omega_o)
|n\cdot\omega_i|
}{
p(\omega_i)
}
\]

Pixel contribution：

\[
L
=
\sum_k
\beta_k L_{e,k}
\]

---

## 6.2 必做功能

Path Tracer 至少支持：

- emissive triangles
- point / directional / spot light
- area light
- environment light
- diffuse
- GGX metal
- GGX dielectric
- reflection
- refraction
- multiple bounce
- Russian Roulette
- NEE
- MIS

### PBRT Path Tracing

PBRT 4e — Path Tracing 章节入口  
https://www.pbr-book.org/4ed/Light_Transport_I_Surface_Reflection

PBRT 4e — Better Path Tracer  
https://www.pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer

---

# 7. Next Event Estimation — NEE

只靠 BSDF Random Walk 时，小光源非常难被随机路径命中。

因此每个非 delta 表面点主动采样光源：

```text
Surface X
    |
    +---- sample light ----> Light
    |
    +---- sample BSDF -----> Next Bounce
```

直接光 estimator：

\[
\hat L_{direct}
=
\frac{
L_i(y\rightarrow x)
f_r(x,\omega_i,\omega_o)
|n_x\cdot\omega_i|
V(x,y)
}{
p_L(y)
}
\]

其中：

- \(p_L\)：光源采样 PDF
- \(V(x,y)\)：Visibility
- Visibility 由 Shadow Ray 判断

Agent 必须特别处理：

- Area PDF 与 Solid Angle PDF 的转换
- Delta Light
- Emissive Triangle
- Environment PDF
- BSDF hit emitter 时与 NEE 的重复计数问题

推荐直接结合 MIS 实现，不要长期维护“只有 NEE、没有 MIS”的最终版本。

---

# 8. Multiple Importance Sampling — MIS

这是项目中的**经典必做算法**。

核心问题：

直接光积分通常同时受到：

- BSDF
- Light Distribution
- Geometry
- Visibility

影响。

没有单个 sampling strategy 永远最佳。

因此同时使用：

- Light Sampling
- BSDF Sampling

并通过 MIS 合并。

## Power Heuristic

对于两种 sampling strategy：

\[
w_a
=
\frac{(n_a p_a)^2}
{(n_a p_a)^2+(n_b p_b)^2}
\]

一 sample/strategy 时：

\[
w_a
=
\frac{p_a^2}
{p_a^2+p_b^2}
\]

\[
w_b=1-w_a
\]

### 必读

PBRT 4e — Multiple Importance Sampling  
https://pbr-book.org/4ed/Monte_Carlo_Integration/Improving_Efficiency

Eric Veach PhD Thesis  
https://graphics.stanford.edu/papers/veach_thesis/

Full thesis PDF  
https://graphics.stanford.edu/papers/veach_thesis/thesis.pdf

重点：

```text
Chapter 9: Multiple Importance Sampling
balance heuristic
power heuristic
one-sample model
multi-sample model
```

---

# 9. Russian Roulette

禁止简单使用固定最大 bounce 作为唯一终止方法。

达到一定 bounce 数后：

\[
q=\mathrm{clamp}(1-\beta_{max},q_{min},q_{max})
\]

以概率 \(q\) 终止。

如果路径继续：

\[
\beta
\leftarrow
\frac{\beta}{1-q}
\]

这样保持 estimator 期望值不变。

Agent 注意：

- RR 通常不要第一、第二 bounce 就启用。
- survival probability 必须进入 throughput compensation。
- 避免极端 throughput 导致 NaN / Inf。

Veach Thesis 同样包含 Russian Roulette 理论：  
https://graphics.stanford.edu/papers/veach_thesis/

PBRT Better Path Tracer：  
https://www.pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer

---

# 10. BVH：Software Ray Tracing 的核心

必须实现两个版本：

```text
CPU SAH BVH
GPU LBVH
```

最终可以与 Vulkan Hardware Acceleration Structure 对比。

---

## 10.1 Ray / AABB

Slab method：

对于每个轴：

\[
t_0=\frac{x_{min}-o_x}{d_x}
\]

\[
t_1=\frac{x_{max}-o_x}{d_x}
\]

最终：

\[
t_{near}
=
\max(t_{min,x},t_{min,y},t_{min,z})
\]

\[
t_{far}
=
\min(t_{max,x},t_{max,y},t_{max,z})
\]

若：

\[
t_{near}\le t_{far}
\]

则存在交点。

GPU 实现时注意：

- inverse direction
- zero direction component
- NaN
- negative zero
- branch reduction

---

# 11. SAH BVH

Surface Area Heuristic：

对于候选 split：

\[
C
=
C_T
+
\frac{A_L}{A_P}N_LC_I
+
\frac{A_R}{A_P}N_RC_I
\]

其中：

- \(C_T\)：Traversal cost
- \(C_I\)：primitive intersection cost
- \(A_L,A_R,A_P\)：左右节点和父节点 surface area
- \(N_L,N_R\)：primitive 数

Agent 不要求第一版实现完美的 production BVH。

建议：

1. CPU recursive BVH
2. median split
3. binned SAH
4. flatten 成 GPU-friendly linear nodes

需要输出：

- node count
- tree depth
- average leaf primitive count
- construction time
- traversal rays/s

PBRT 可作为 BVH 实现和理论参考入口：

https://pbr-book.org/4ed/Primitives_and_Intersection_Acceleration

---

# 12. GPU LBVH

LBVH 路线：

```text
Primitive Centroid
      ↓
Normalize Scene Bounds
      ↓
Morton Code
      ↓
Radix Sort
      ↓
Binary Radix Tree
      ↓
BVH Internal Nodes
      ↓
Bottom-up AABB
```

Morton Code：

\[
M(x,y,z)
=
interleave(bits(x),bits(y),bits(z))
\]

Karras LBVH 的核心：

> 按 Morton code 排序后，通过相邻 key 的最长公共前缀决定 radix tree topology。

GPU 上常利用：

\[
\delta(i,j)
=
LCP(code_i,code_j)
\]

实际可以通过：

```text
XOR
+
count leading zeros
```

计算共同前缀长度。

### 必读论文

Tero Karras, *Maximizing Parallelism in the Construction of BVHs, Octrees, and k-d Trees*, HPG 2012.

PDF：  
https://developer.nvidia.com/blog/parallelforall/wp-content/uploads/2012/11/karras2012hpg_paper.pdf

Agent 查询关键词：

```text
Karras LBVH 2012
Morton code BVH
binary radix tree GPU
longest common prefix BVH
parallel BVH construction
```

---

# 13. Vulkan Hardware Ray Tracing Backend

Software BVH 完成之后，再建立 Hardware RT Backend。

核心扩展：

```text
VK_KHR_acceleration_structure
VK_KHR_ray_tracing_pipeline
VK_KHR_ray_query
```

## 13.1 Acceleration Structure

架构：

```text
Mesh
 ↓
BLAS
 ↓
Instance
 ↓
TLAS
```

官方资料：

Vulkan Guide — Ray Tracing  
https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html

Vulkan Spec — Acceleration Structures  
https://docs.vulkan.org/spec/latest/chapters/accelstructures.html

Vulkan Spec — Ray Tracing  
https://docs.vulkan.org/spec/latest/chapters/raytracing.html

VK_KHR_ray_tracing_pipeline  
https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_ray_tracing_pipeline.html

VK_KHR_ray_query  
https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_ray_query.html

Khronos Ray Tracing Final Specification Overview  
https://www.khronos.org/blog/vulkan-ray-tracing-final-specification-release

---

## 13.2 推荐 Backend 抽象

```cpp
class IRayTracingBackend
{
public:
    virtual void BuildScene(...) = 0;
    virtual Hit TraceClosest(const Ray&) = 0;
    virtual bool TraceAny(const Ray&) = 0;
};
```

实现：

```text
SoftwareBVHBackend
HardwareRayQueryBackend
HardwareRTPipelineBackend
```

注意：

> 不要求三套 backend 一开始完全共享 shader code；优先保证统一场景和统一材质输入，便于性能 / 正确性对照。

---

# 14. Wavefront Path Tracing

完成正确的 Reference Path Tracer 后，把 GPU Path Tracing 改造为 Wavefront。

不要把 GPU Path Tracer 当成：

```text
一个 shader invocation 内递归完成整条 path
```

现代 GPU 更适合拆成多个阶段：

```text
RayGen
 ↓
Ray Queue
 ↓
Intersect
 ↓
Hit Queue
 ↓
Shade
 ├── Shadow Queue
 └── Next Ray Queue
 ↓
Trace Shadow
 ↓
Compact
 ↓
Next Bounce
```

核心 GPU 数据结构：

```cpp
struct RayWorkItem;
struct HitWorkItem;
struct ShadowRayWorkItem;
struct MaterialEvalWorkItem;
```

需要研究：

- queue
- atomic append
- prefix sum
- stream compaction
- indirect dispatch
- persistent threads（扩展）
- SoA vs AoS
- divergence
- occupancy
- memory coalescing

### 必读

PBRT 4e — Wavefront Rendering on GPUs  
https://www.pbr-book.org/4ed/Wavefront_Rendering_on_GPUs

Mapping Path Tracing to the GPU  
https://www.pbr-book.org/4ed/Wavefront_Rendering_on_GPUs/Mapping_Path_Tracing_to_the_GPU

Path Tracer Implementation  
https://www.pbr-book.org/4ed/Wavefront_Rendering_on_GPUs/Path_Tracer_Implementation

---

# 15. 实时目标：1 SPP Path Tracing

现代部分不要一开始追求几十 SPP。

目标输入：

```text
1 SPP / pixel / frame
```

然后依靠：

```text
Temporal Reuse
+
Spatial Reconstruction
+
Better Sampling
```

得到最终结果。

Reference Mode：

```text
512 / 1024 / 4096 SPP
```

Realtime Mode：

```text
1 SPP
```

用于客观比较。

---

# 16. Temporal Reprojection

SVGF / ReSTIR 之前必须先做好 Temporal Reprojection。

每个 pixel 至少维护：

- World Position / Depth
- Normal
- Motion Vector
- Previous UV
- Previous Radiance
- History Length
- Moments
- Material / Object ID（可选但推荐）

Reprojection：

\[
uv_{prev}
=
uv_{curr}
+
v_{motion}
\]

历史有效性不能只检查屏幕范围。

至少判断：

\[
|z_{prev}-z_{reprojected}|<\epsilon_z
\]

\[
n_{prev}\cdot n_{curr}>\epsilon_n
\]

并在动态对象场景下使用 motion vector。

历史融合：

\[
C_t
=
(1-\alpha)C_{history}
+
\alpha C_{current}
\]

其中 \(\alpha\) 可以根据 History Length 调整。

---

# 17. SVGF

**Spatiotemporal Variance-Guided Filtering** 是项目的核心现代算法之一。

SVGF 的整体链：

```text
Noisy 1SPP
   ↓
Temporal Reprojection
   ↓
Temporal Accumulation
   ↓
Moments
   ↓
Variance Estimation
   ↓
A-Trous Wavelet Filtering
   ↓
Denoised Radiance
```

---

## 17.1 Moments 与 Variance

维护一阶、二阶 luminance moment：

\[
m_1=E[L]
\]

\[
m_2=E[L^2]
\]

variance：

\[
\sigma^2
=
m_2-m_1^2
\]

数值上：

\[
\sigma^2=\max(0,m_2-m_1^2)
\]

Variance 决定滤波强度：

- 高 variance → 更强 spatial filter
- 低 variance → 尽量保留 detail

---

## 17.2 A-Trous Filter

每轮 kernel spacing 增大：

```text
Iteration 0: step = 1
Iteration 1: step = 2
Iteration 2: step = 4
Iteration 3: step = 8
...
```

权重应该联合：

\[
w
=
w_{kernel}
w_{normal}
w_{depth}
w_{luminance}
\]

Normal 权重示例：

\[
w_n
=
\max(0,n_p\cdot n_q)^{\phi_n}
\]

Depth 权重：

\[
w_z
=
\exp
\left(
-\frac{|z_p-z_q|}
{\phi_z}
\right)
\]

Luminance 权重：

\[
w_l
=
\exp
\left(
-\frac{|L_p-L_q|}
{\phi_l\sqrt{\sigma^2+\epsilon}}
\right)
\]

注意：

> 实际 SVGF 参数和 variance handling 必须回到原论文核对，本文公式只作为 Agent 的架构指导，不替代论文细节。

### SVGF 原论文

NVIDIA Research Page  
https://research.nvidia.com/labs/rtr/publication/schied2017spatiotemporal/

论文介绍明确针对 one-path-per-pixel GI 的时空重建。

Agent 必须优先阅读论文 PDF / supplemental / reference code，再实现：

```text
reprojection
temporal accumulation
moments
variance
A-Trous
disocclusion handling
```

---

# 18. Reservoir Sampling

ReSTIR 之前先独立实现并测试经典 Reservoir Sampling。

对于流式输入：

```text
x1, x2, ..., xn
```

希望只保存一个样本并保证 uniform selection：

当处理第 \(i\) 个样本时：

\[
P(select\ x_i)=\frac1i
\]

Weighted Reservoir Sampling：

候选 \(x_i\) 权重：

\[
w_i
\]

累计：

\[
W_i=W_{i-1}+w_i
\]

以概率：

\[
P(replace)
=
\frac{w_i}{W_i}
\]

替换当前 sample。

建议 Reservoir：

```cpp
struct Reservoir
{
    LightSample y;
    float weightSum;
    uint M;
};
```

必须写独立 CPU unit test：

- 等权重下每个 candidate 出现概率接近 \(1/N\)
- 不等权重下频率与 weight 成比例

PBRT Sampling Algorithms 中也可继续查询 Reservoir Sampling：

https://www.pbr-book.org/4ed/Sampling_Algorithms

---

# 19. RIS — Resampled Importance Sampling

ReSTIR 不是“Temporal TAA for light samples”。

其数学基础来自：

```text
Importance Sampling
      ↓
Resampled Importance Sampling
      ↓
Weighted Reservoir Sampling
      ↓
Spatiotemporal Reservoir Reuse
      ↓
ReSTIR
```

对于 candidate：

\[
x_i\sim q(x)
\]

定义目标函数 \(\hat p(x)\)。

candidate weight：

\[
w_i
=
\frac{\hat p(x_i)}{q(x_i)}
\]

Reservoir 根据 \(w_i\) 进行 weighted selection。

常见 reusable weight 表达中会出现：

\[
W
=
\frac{
\sum_i w_i
}{
M\hat p(y)
}
\]

但：

> ReSTIR 中 temporal / spatial merge、visibility、bias correction、Jacobian / target PDF 等细节不能只靠该简化式实现，必须严格根据论文推导。

---

# 20. ReSTIR DI

这是项目最终最重要的现代算法之一。

目标：

> 在非常少的 shadow rays / pixel 情况下，从大量动态光源中找到高贡献 light sample。

Pipeline：

```text
Candidate Generation
       ↓
Initial Reservoir
       ↓
Temporal Reuse
       ↓
Spatial Reuse
       ↓
Final Selected Light
       ↓
Visibility Ray
       ↓
Direct Lighting
```

---

## 20.1 Initial Candidates

每个 pixel 生成少量候选：

```text
Uniform Light
Light Power Distribution
Emissive Triangle
Environment
Previous techniques...
```

对于 candidate：

\[
w_i
=
\frac{
\hat p(x_i)
}{
q(x_i)
}
\]

其中 target 可以与：

\[
L_i
f_r
\cos\theta
\]

相关。

---

## 20.2 Temporal Reuse

从上一帧 reproject reservoir：

```text
Current Pixel
     ↑
Motion Vector
     ↑
Previous Reservoir
```

必须做 similarity test：

- normal
- depth
- object / material
- screen bounds
- disocclusion

不能把完全不同表面的 reservoir 直接复用。

---

## 20.3 Spatial Reuse

采样邻居 reservoir：

```text
        R1
         |
R2 ---- Current ---- R3
         |
        R4
```

进行 reservoir merge。

注意：

- 邻居必须做 geometric similarity test
- 防止跨物体边缘
- 防止薄几何漏光
- 邻居数量是 quality / cost 参数
- 原始 ReSTIR 同时讨论 biased / unbiased estimator

---

## 20.4 Visibility

候选评估阶段 visibility 很贵。

需要严格区分：

```text
cheap target evaluation
final visibility evaluation
```

不要每个 candidate 都无脑 trace full visibility，否则失去 ReSTIR 意义。

---

## 20.5 原始论文

Bitterli et al., *Spatiotemporal Reservoir Resampling for Real-Time Ray Tracing with Dynamic Direct Lighting*, SIGGRAPH 2020.

NVIDIA Research Page  
https://research.nvidia.com/labs/rtr/publication/bitterli2020spatiotemporal/

PDF  
https://research.nvidia.com/sites/default/files/pubs/2020-07_Spatiotemporal-reservoir-resampling/ReSTIR.pdf

DOI  
https://doi.org/10.1145/3386569.3392481

Agent 查询关键词：

```text
ReSTIR DI
RIS
weighted reservoir sampling
temporal reservoir reuse
spatial reservoir reuse
target function
source PDF
reservoir merge
Jacobian
biased ReSTIR
unbiased ReSTIR
```

---

# 21. ReSTIR 工程优化扩展

第一版 ReSTIR DI 正确后，再读 production-oriented work。

NVIDIA — *Rearchitecting Spatiotemporal Resampling for Production*  
https://research.nvidia.com/labs/rtr/publication/wyman2021rearchitecting/

不要在第一版 ReSTIR DI 完成前直接套生产优化。

推荐顺序：

```text
Original ReSTIR DI
      ↓
Correctness
      ↓
Temporal stability
      ↓
Spatial stability
      ↓
Profiler
      ↓
Production optimizations
```

---

# 22. ReSTIR GI：扩展目标，不属于第一阶段必做

ReSTIR DI 完成以后，如果仍有足够开发时间，再研究 ReSTIR GI。

NVIDIA Publication Index 中可查询：

```text
ReSTIR GI: Path Resampling for Real-Time Path Tracing
```

NVIDIA Research  
https://research.nvidia.com/person/matt-pharr

搜索关键词：

```text
ReSTIR GI Path Resampling 2021 Ouyang
```

不要在 ReSTIR DI 还不稳定时实现 ReSTIR GI。

---

# 23. 2026 前沿扩展：只研究，不作为当前交付要求

若后续希望将项目继续发展为研究型 renderer，可以阅读：

NVIDIA — *ReSTIR PT Enhanced: Algorithmic Advances for Faster and More Robust ReSTIR Path Tracing*，2026  
https://research.nvidia.com/labs/rtr/publication/lin2026restirptenhanced/

当前 Agent：

> 只记录该路线，不立即实现。

---

# 24. 经典 GI 算法：作为扩展实验，而非主路线

这些算法很重要，但当前 Vulkan 实时作品集优先级低于：

```text
Path Tracing + MIS
Wavefront
SVGF
ReSTIR DI
```

---

## 24.1 Photon Mapping

两阶段：

```text
Light
 ↓
Photon Tracing
 ↓
Photon Map
 ↓
Camera Rendering
 ↓
Density Estimation
```

局部 photon density estimation：

\[
L
\approx
\frac{1}{\pi r^2}
\sum_i
f_r
\Phi_i
\]

适合展示：

- caustics
- difficult specular-diffuse paths

资料：

Henrik Wann Jensen — Global Illumination using Photon Maps  
https://graphics.ucsd.edu/~henrik/papers/photon_map/

PDF 镜像：  
https://www.cs.princeton.edu/courses/archive/fall16/cos526/papers/jensen01.pdf

优先级：

```text
Optional
```

---

## 24.2 Bidirectional Path Tracing — BDPT

同时构建：

```text
Camera Subpath
Light Subpath
```

并尝试连接不同顶点组合。

最后使用 MIS 合并多种 path construction strategies。

资料：

Eric Veach Thesis  
https://graphics.stanford.edu/papers/veach_thesis/

重点：

```text
Chapter 10: Bidirectional Path Tracing
Chapter 9 : Multiple Importance Sampling
```

优先级：

```text
Optional / Research
```

---

## 24.3 Metropolis Light Transport — MLT

在 Path Space 中做 Markov Chain mutation。

理想目标：

> 高贡献 path 被更频繁访问。

资料：

Veach Thesis  
https://graphics.stanford.edu/papers/veach_thesis/

MLT Paper Page  
https://graphics.stanford.edu/papers/metro/gamma-fixed/

适合：

- small openings
- difficult indirect light
- complex caustics

优先级：

```text
Research only
```

---

## 24.4 VCM — Vertex Connection and Merging

概念上可理解为结合：

```text
BDPT-style Vertex Connection
+
Photon-style Vertex Merging
```

当前不进入主路线。

Agent 如后续研究，查询：

```text
Vertex Connection and Merging
VCM rendering Georgiev
Bidirectional Path Tracing Photon Mapping MIS
```

---

# 25. 建议项目阶段

## Milestone 1 — Correct Ray Tracer

完成：

- Triangle intersection
- AABB
- Whitted
- Reflection
- Refraction
- Shadow
- CPU BVH

验收：

- 镜面正确
- 折射正确
- 阴影正确
- BVH 与 brute force hit result 一致

---

## Milestone 2 — Physically Based Path Tracer

完成：

- Rendering Equation
- Monte Carlo
- Lambert
- GGX
- Importance Sampling
- Area Light
- Environment
- NEE
- MIS
- Russian Roulette

验收：

- Cornell Box color bleeding
- Soft shadow
- Glossy reflection
- Glass
- 多次反弹
- 高 SPP 收敛
- NEE / MIS 明显降低 variance

---

## Milestone 3 — Acceleration

完成：

- SAH BVH
- Flattened BVH
- GPU LBVH
- Software GPU traversal

验收：

- 与 brute force 完全一致
- 大场景 rays/s 明显提高
- 输出 BVH build / traversal profiler

---

## Milestone 4 — Vulkan Hardware RT

完成：

- BLAS
- TLAS
- Ray Query
- RT Pipeline
- SBT

验收：

```text
Software BVH result ≈ Hardware RT result
```

允许存在 floating point / epsilon 小差异。

---

## Milestone 5 — Wavefront PT

完成：

- Ray Queue
- Hit Queue
- Shadow Queue
- Next Bounce Queue
- Compaction
- GPU dispatch

验收：

- 与 reference PT 收敛到相同图像
- profiler 显示各阶段耗时
- 统计 active path 数量随 bounce 的变化

---

## Milestone 6 — Temporal Infrastructure

完成：

- GBuffer
- Motion Vector
- Reprojection
- History Validation
- Temporal Accumulation
- Moments
- Variance

验收：

- 静态镜头稳定
- 移动相机不产生大面积 ghosting
- 动态对象 history 能被正确 reject

---

## Milestone 7 — SVGF

完成：

- temporal accumulation
- variance
- A-Trous
- edge stopping

验收：

对比：

```text
1 SPP raw
1 SPP temporal only
1 SPP temporal + A-Trous
1 SPP SVGF
Reference
```

---

## Milestone 8 — ReSTIR DI

完成：

1. independent reservoir test
2. initial candidates
3. initial reservoir
4. temporal reuse
5. spatial reuse
6. final visibility
7. debug views
8. bias / stability validation

验收场景：

```text
100 lights
1,000 lights
10,000 emissive lights
dynamic lights
moving camera
moving objects
```

对比：

```text
Uniform light sampling
Power-weighted light sampling
ReSTIR DI
Reference
```

---

# 26. 推荐测试场景

## Cornell Box

验证：

- diffuse GI
- color bleeding
- area light
- convergence

## Glossy Cornell Box

验证：

- GGX
- BSDF sampling
- Light sampling
- MIS

## Mirror + Glass

验证：

- delta transport
- Fresnel
- recursion
- Russian Roulette

## Sponza

验证：

- BVH
- real scene geometry
- indirect light
- denoising

## Many Lights Scene

验证：

- ReSTIR DI

建议：

```text
随机 emissive triangles
动态位置
动态颜色
大量小面积光源
```

---

# 27. Debug Views

必须实现 Debug UI。

至少包含：

```text
Albedo
Normal
Roughness
Metallic
Depth
World Position
Motion Vector
Object ID

Raw Radiance
Direct
Indirect
Throughput
Path Length
Ray Count

Temporal History Length
Moments M1
Moments M2
Variance

Reservoir M
Reservoir Weight Sum
Selected Light ID
Selected Light Position
Temporal Reuse Accepted
Spatial Reuse Accepted

BVH Node Depth
BVH Leaf Occupancy
Traversal Node Count
Triangle Test Count
```

这是作品集展示的重要部分，不是“开发时临时工具”。

---

# 28. 数值稳定性规则

Agent 在所有 shader / C++ 中统一遵守：

```text
避免 normalize(0)
避免 divide by zero
避免 NaN
避免 Inf
避免负 PDF
避免负 radiance
避免非法 sqrt
```

建议：

```cpp
safeNormalize()
safeRcp()
isFinite()
sanitizeRadiance()
```

Ray epsilon 必须统一。

不要在不同模块随意写：

```cpp
0.001
0.0001
1e-5
```

统一定义：

```cpp
RayTMin
ShadowRayTMin
NormalOffset
PDFEpsilon
RoughnessMin
```

---

# 29. Monte Carlo 正确性验证

所有高级采样算法必须做统计测试。

## 29.1 PDF Test

采样大量方向：

```text
SampleBSDF()
```

构造 histogram。

与：

```text
BSDF_PDF()
```

理论分布比较。

---

## 29.2 Energy Test

对 BRDF 验证：

\[
\int_{\Omega^+}
f_r(\omega_i,\omega_o)
\cos\theta_i
\,d\omega_i
\le1
\]

避免材质凭空制造能量。

---

## 29.3 Convergence Test

保存：

```text
1 spp
2 spp
4 spp
8 spp
16 spp
...
4096 spp
```

并与 reference 计算：

- MSE
- RMSE
- PSNR
- 可选 SSIM

---

# 30. 性能指标

每个 milestone 都记录：

```text
Resolution
SPP
Frame Time
Rays / frame
Mrays / second
Primary Rays
Shadow Rays
Secondary Rays
Average Path Length
BVH Node Tests
Triangle Tests
GPU Memory
```

ReSTIR：

```text
Candidates / pixel
Temporal reuse count
Spatial neighbors
Visibility rays / pixel
Reservoir memory
```

SVGF：

```text
Temporal pass ms
Variance pass ms
A-Trous pass ms
Total denoiser ms
```

---

# 31. 推荐代码模块

建议结构：

```text
Renderer/
│
├── Core/
│   ├── Math
│   ├── Random
│   ├── Sampling
│   └── Profiler
│
├── Scene/
│   ├── Mesh
│   ├── Material
│   ├── Light
│   └── Camera
│
├── RayTracing/
│   ├── Ray
│   ├── Intersection
│   ├── BVH
│   ├── LBVH
│   ├── SoftwareRT
│   └── VulkanRT
│
├── BSDF/
│   ├── Lambert
│   ├── Fresnel
│   ├── GGX
│   └── Sampling
│
├── Integrators/
│   ├── Whitted
│   ├── PathTracerReference
│   └── WavefrontPathTracer
│
├── Denoiser/
│   ├── Reprojection
│   ├── Temporal
│   ├── Variance
│   ├── ATrous
│   └── SVGF
│
└── ReSTIR/
    ├── Reservoir
    ├── InitialSampling
    ├── TemporalReuse
    ├── SpatialReuse
    └── ReSTIRDI
```

---

# 32. 本地 Agent 的强制实现规则

Agent 每次实现算法时必须执行：

## Step 1 — Research

先打开本文对应的一手资料。

优先级：

```text
Original Paper
>
PBRT
>
Khronos / Vulkan Spec
>
NVIDIA Research
>
高质量课程资料
>
博客
```

不要优先从随机博客复制代码。

---

## Step 2 — Mathematics

在写代码前先输出：

```text
1. Algorithm assumptions
2. Input
3. Output
4. Estimator
5. Sampling distribution
6. PDF
7. Bias / unbiased condition
8. Numerical edge cases
```

如果 Sample / PDF / estimator 关系不清楚：

> 不进入代码阶段。

---

## Step 3 — Minimal implementation

先完成最小正确版本。

例如 ReSTIR：

```text
Reservoir
↓
Initial Sampling
↓
Temporal
↓
Spatial
```

不要一次性加入几十个 production optimization。

---

## Step 4 — Reference comparison

每个算法必须和 Reference 对比。

例如：

```text
ReSTIR DI
vs
High-SPP Direct Lighting

SVGF
vs
High-SPP PT

LBVH
vs
Brute Force Intersection
```

---

## Step 5 — Debug output

新算法必须同时提供 debug buffer。

禁止只有 final color。

---

## Step 6 — Benchmark

新算法完成后记录：

```text
before
after
quality
performance
memory
```

---

# 33. Agent 禁止事项

禁止：

1. 未理解 PDF 就复制网上 Shader。
2. BSDF Sample 与 PDF 不对应。
3. NEE + emitter hit 重复计数。
4. MIS 使用错误 measure。
5. Area PDF / Solid Angle PDF 混用。
6. Reservoir weight 没有推导就凭经验写。
7. Temporal reuse 不做 disocclusion test。
8. SVGF 只做普通 bilateral blur 却命名为 SVGF。
9. ReSTIR 只做“邻居随机选灯”却命名为 ReSTIR。
10. GPU Wavefront 仍在单 kernel 内模拟完整 recursive path。
11. 为了“效果好”随意 clamp radiance，导致 estimator bias，却不记录。
12. 每次出现 firefly 就直接加 arbitrary clamp。
13. 把 Reference Renderer 和 Real-Time Renderer 混成一套难以验证的代码。

---

# 34. 核心资料总表

## Rendering / Monte Carlo

PBRT 4e  
https://pbr-book.org/4ed/contents

Monte Carlo Integration  
https://pbr-book.org/4ed/Monte_Carlo_Integration

Improving Efficiency / Importance Sampling / MIS  
https://pbr-book.org/4ed/Monte_Carlo_Integration/Improving_Efficiency

Light Transport Equation  
https://www.pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/The_Light_Transport_Equation

Better Path Tracer  
https://www.pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer

---

## Microfacet / GGX

PBRT Microfacet Theory  
https://www.pbr-book.org/4ed/Reflection_Models/Roughness_Using_Microfacet_Theory

---

## MIS / BDPT / MLT

Eric Veach Thesis  
https://graphics.stanford.edu/papers/veach_thesis/

Full PDF  
https://graphics.stanford.edu/papers/veach_thesis/thesis.pdf

MLT  
https://graphics.stanford.edu/papers/metro/gamma-fixed/

---

## BVH / LBVH

Tero Karras LBVH 2012 PDF  
https://developer.nvidia.com/blog/parallelforall/wp-content/uploads/2012/11/karras2012hpg_paper.pdf

PBRT BVH / Acceleration Structures  
https://pbr-book.org/4ed/Primitives_and_Intersection_Acceleration

---

## GPU Wavefront

PBRT Wavefront Rendering  
https://www.pbr-book.org/4ed/Wavefront_Rendering_on_GPUs

Mapping PT to GPU  
https://www.pbr-book.org/4ed/Wavefront_Rendering_on_GPUs/Mapping_Path_Tracing_to_the_GPU

Wavefront PT Implementation  
https://www.pbr-book.org/4ed/Wavefront_Rendering_on_GPUs/Path_Tracer_Implementation

---

## Vulkan Ray Tracing

Vulkan Guide  
https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html

Acceleration Structure Spec  
https://docs.vulkan.org/spec/latest/chapters/accelstructures.html

Ray Tracing Spec  
https://docs.vulkan.org/spec/latest/chapters/raytracing.html

VK_KHR_ray_tracing_pipeline  
https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_ray_tracing_pipeline.html

VK_KHR_ray_query  
https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_ray_query.html

Khronos Overview  
https://www.khronos.org/blog/vulkan-ray-tracing-final-specification-release

---

## SVGF

NVIDIA Research  
https://research.nvidia.com/labs/rtr/publication/schied2017spatiotemporal/

关键词：

```text
SVGF
temporal accumulation
variance guided filtering
A-Trous wavelet
moments
disocclusion
```

---

## ReSTIR DI

NVIDIA Research  
https://research.nvidia.com/labs/rtr/publication/bitterli2020spatiotemporal/

Original PDF  
https://research.nvidia.com/sites/default/files/pubs/2020-07_Spatiotemporal-reservoir-resampling/ReSTIR.pdf

DOI  
https://doi.org/10.1145/3386569.3392481

Production Optimization  
https://research.nvidia.com/labs/rtr/publication/wyman2021rearchitecting/

2026 ReSTIR PT Enhanced  
https://research.nvidia.com/labs/rtr/publication/lin2026restirptenhanced/

---

## Photon Mapping

Henrik Wann Jensen  
https://graphics.ucsd.edu/~henrik/papers/photon_map/

PDF Mirror  
https://www.cs.princeton.edu/courses/archive/fall16/cos526/papers/jensen01.pdf

---

# 35. 当前明确“不优先”的内容

暂不优先：

```text
BDPT
MLT
VCM
Progressive Photon Mapping
Spectral Rendering
Participating Media
Differentiable Rendering
Neural Denoising
Path Guiding
ReSTIR GI
ReSTIR PT
```

理由：

这些算法都值得研究，但会稀释当前作品集的主线。

当前主线必须保持：

```text
Correct Monte Carlo PT
        ↓
MIS
        ↓
BVH / GPU Architecture
        ↓
Wavefront
        ↓
1 SPP
        ↓
SVGF
        ↓
ReSTIR DI
        ↓
Modern Vulkan Real-Time Ray Tracer
```

---

# 36. 最终作品集故事线

最终展示不要简单写：

```text
Implemented:
Whitted
Path Tracing
BVH
SVGF
ReSTIR
```

应该展示为一条问题驱动路线：

```text
传统 Whitted 光追
        ↓
无法表达完整 diffuse GI
        ↓
Monte Carlo Path Tracing
        ↓
能够求解 Rendering Equation，但低 SPP 方差巨大
        ↓
Importance Sampling + NEE + MIS
        ↓
降低方差
        ↓
Wavefront GPU Path Tracing
        ↓
提高 GPU throughput
        ↓
1 SPP 仍然无法直接显示
        ↓
SVGF 时空重建
        ↓
大量动态光源下 direct lighting sampling 仍昂贵
        ↓
ReSTIR DI
        ↓
时空复用重要光源 sample
        ↓
Modern Vulkan Real-Time Ray Tracing Renderer
```

这条路线本身就是项目的技术叙事。

---

# 37. 最终 Definition of Done

只有满足以下条件，才认为这个 Vulkan 光追项目完成第一阶段：

- [ ] Whitted baseline 正确
- [ ] CPU SAH BVH
- [ ] GPU LBVH
- [ ] Software RT Backend
- [ ] Vulkan BLAS / TLAS
- [ ] Ray Query / RT Pipeline 至少一条硬件路径稳定
- [ ] Rendering Equation Path Tracer
- [ ] GGX
- [ ] Importance Sampling
- [ ] NEE
- [ ] MIS
- [ ] Russian Roulette
- [ ] High-SPP Reference
- [ ] Wavefront PT
- [ ] 1 SPP Mode
- [ ] Motion Vector
- [ ] Temporal Reprojection
- [ ] Variance
- [ ] A-Trous
- [ ] SVGF
- [ ] Reservoir Sampling 单元测试
- [ ] ReSTIR DI Initial Sampling
- [ ] ReSTIR Temporal Reuse
- [ ] ReSTIR Spatial Reuse
- [ ] ReSTIR Debug Views
- [ ] Cornell Box benchmark
- [ ] Sponza benchmark
- [ ] Many Lights benchmark
- [ ] Software / Hardware RT 对比
- [ ] 1SPP / SVGF / ReSTIR / Reference 对比
- [ ] GPU timing 数据
- [ ] 技术展示视频所需 Debug UI

完成后再决定是否进入：

```text
ReSTIR GI
Path Guiding
Photon Mapping Caustics
BDPT / VCM
Participating Media
```

---

## 给本地 Agent 的一句话目标

> 构建一个“可验证、可对比、可实时运行”的 Vulkan 现代光追渲染器：以正确的 Monte Carlo Path Tracing + MIS 为数学基线，以 SAH/LBVH 和 Vulkan Hardware RT 为求交基础，以 Wavefront 为 GPU 执行架构，以 SVGF 解决低 SPP 重建，以 ReSTIR DI 解决大量动态光源的实时重要性采样问题；所有高级算法必须有高 SPP Reference、Debug View、数学推导来源和性能对照。
