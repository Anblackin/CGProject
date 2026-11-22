# 公共接口约定（模块 C 侧实现）

本文件记录模块 C 对 `PROJECT_PLAN.md` 第 2 节「约定的公共接口」的落地实现。
所有跨模块约定一经改动必须通知全组并同步修改相关测试。

## 1. 坐标、单位与矩阵

| 项 | 约定 |
|---|---|
| 坐标系 | 右手系，Y 轴向上，轨道位于 XZ 平面，太阳位于世界原点 |
| 单位 | 场景显示单位（scene units），**不是**天文单位；物理参数与展示比例分别标注 |
| 向量 | 列向量；变换写作 `v' = M * v` |
| 矩阵存储 | 4×4 列主序，`m[column * 4 + row]` |
| 组合顺序 | `World = T * R`，`Clip = P * V * World`，从右向左作用 |
| 投影 | OpenGL 风格对称透视，裁剪空间 z ∈ [-w, w] |
| 相机 | `Mat4::lookAt(eye, target, up)`，相机看向局部 -Z |

模块 C 已按该约定实现 `Mat4::operator*`、`transformPoint`、`transformDirection`、
`translation/scaling/rotationX/rotationY/rotationZ/lookAt/perspective`，demo 中已验证成像位置正确。

## 2. Vertex / Mesh

```cpp
struct Vertex { Vec3 position; Vec3 normal; Vec2 uv; };
struct Mesh   { std::vector<Vertex> vertices; std::vector<uint32_t> indices; std::string name; };
```

`Vertex::position` 与 `normal` 均为**物体空间**，由 A 提供的模型矩阵变换到世界空间；
`normal` 用矩阵的**方向变换**（不含平移）处理。索引为 32 位无符号整数，三角形按每 3 个索引一组。

## 3. 球体网格

- 经纬度球：`segmentsU` 段经度 × `segmentsV` 段纬度，顶点数 `(segmentsU+1)(segmentsV+1)`。
- **A+C 共用精度档位**：`makeSphereParams(radius, SphereDetail)` 返回 `Low=16×8`、`Medium=32×16`、
  `High=64×32` 的 `SphereParams`。A 的参数表按显示半径与相机距离选档，C 负责按档生成网格；
  双方不各自硬编码段数。
- UV：等距圆柱投影，`u = i / segmentsU`，`v = j / segmentsV`（v=0 为北极，v=1 为南极）。
- **接缝**：经度首尾各一列顶点，位置与法线完全相同，但 `u = 0` 与 `u = 1` 不同；纹理采样端必须用
  `WrapMode::Repeat` 才能在接缝处连续。这是「球面纹理接缝」处理的核心。
- **极点**：删除零面积三角形，避免重心坐标 / 插值出现除零。绕序经测试验证所有三角形外法线朝外。

## 4. 环带与轨道细带

- `buildRing` 生成 XZ 平面圆环（`y = 0`），`innerRadius/outerRadius/radialSegments/angularSegments` 可配置。
- 双面：上表面法线 +Y，下表面法线 -Y，两面各自独立顶点与相反绕序。B 模块可以对普通实体开启背面剔除，
  对 `Material::doubleSided == true` 的网格关闭剔除；C 已经把下表面显式给出，画家顺序与深度测试即可正确显示。
- UV：`u` 沿圆周方向，`v` 沿半径方向（内 0 外 1）。环带纹理使用 `makeRingBandTexture()` 的径向条纹，
  包含两处类似卡西尼环缝的暗带。
- `buildOrbitRibbon` 是 `buildRing` 的特例：宽度很小、径向 1 段、双面，用 `unlit` 材质显示轨道示意线。

## 5. 纹理与线性颜色空间

- 磁盘纹理为 8-bit sRGB PPM（P6, maxval 255）；`loadPPM` 在读取时立即解码为**线性 float RGBA**。
- 第 0 行对应 `v = 0`（图像顶部）；与等距圆柱 UV 约定一致。
- 采样：`FilterMode::Nearest / Bilinear`，`WrapMode::Repeat / Clamp / Mirror`；采样输出线性 RGBA。
- 三处必须保持线性：纹理采样 → `shade()` → `SampleBuffer` 合成；最终显示转换（linear→sRGB）由 D/E 在
  Resolve / present 阶段统一完成，C 不在中途做 gamma 校正。

## 6. Material / 光照

```cpp
struct Material {
    TexturePtr albedo;             // 空则使用 baseColor
    Rgba baseColor;
    bool sunEmissive; Rgba sunEmissiveColor;   // 自发光路径，跳过光照
    float ambient;                 // 常量环境光
    float diffuse;                 // Lambert 漫反射系数
    bool unlit;                    // 轨道线等，直接返回基色
    bool doubleSided;              // 供 B 决定背面剔除策略
};
struct DirectionalLight { Vec3 direction; Rgba intensity; };  // direction 指向光源
```

`shade(mat, input, light)` 是**纯函数**，返回线性 RGBA：

- `sunEmissive`：`emissiveColor × albedo`，不受法线与光源方向影响；
- `unlit`：直接返回 albedo；
- 其他：`albedo × (ambient + diffuse × max(0, N·L) × lightIntensity)`。

> 说明：环带的上下表面是显式几何，不是同一面的双面着色技巧。`doubleSided` 只作为渲染状态提示。

## 7. 与其他模块的协作接口建议

- A 输出 `SceneSnapshot` 时携带天体网格标识、模型矩阵、材质索引；C 的网格与材质按 `name` 关联。
- B 在顶点着色后的插值阶段保证透视正确（见 demo 中 `invW` 加权做法）；`ShadingInput` 接收世界空间法线与 UV。
- D 对每个子样本调用 `shade()` 时传入相同插值结果；MSAA 只在覆盖到的三角形上求值一次。
- E 的像素显微镜显示 `ShadingInput.uv` 与 `Material` 名称即可解释颜色来源，无需复算着色。
