# 模块 C 交付清单（2026-11-21）

按 `PROJECT_PLAN.md` 逐条核对。**结论：模块 C 范围内的文件已全部完成并有验证材料。**

## 一、分工表要求 — 全部完成

| 分工表条目 | 实现文件 | 状态 |
|---|---|---|
| 球体网格 | `src/geometry.cpp` → `buildSphere` | ✅ |
| 土星环 | `src/geometry.cpp` → `buildRing` | ✅ |
| 轨道细带 | `src/geometry.cpp` → `buildOrbitRibbon` | ✅ |
| UV 与法线 | `include/solarc/types.h` → `Vertex` | ✅ |
| 球面纹理接缝 | 球体 UV 列复制 + `WrapMode::Repeat`，自测与 `module_c_seam.png` | ✅ |
| 纹理采样 | `src/texture.cpp` → `sampleTexture` | ✅ |
| 太阳自发光 | `src/shading.cpp` → `makeSunMaterial` | ✅ |
| 行星漫反射与环境光 | `src/shading.cpp` → `makePlanetMaterial` / `shade` | ✅ |
| **主要交付：可配置的球体/环带模型、材质与着色模块** | `include/solarc/` + `src/` 静态库 | ✅ |

## 二、论文分工要求 — 草稿完成

| 条目 | 文件 | 状态 |
|---|---|---|
| 网格、纹理、光照、环带和资源来源 | `docs/MODULE_C_PAPER_DRAFT.md` | ✅ 草稿 |

## 三、协作组要求 — 已完成

| 条目 | 实现 | 状态 |
|---|---|---|
| A+C 统一球体精度 | `makeSphereParams(radius, SphereDetail::Low/Medium/High)` | ✅ |
| A+C 统一环带/轨道数据格式 | `RingParams`、`buildOrbitRibbon`，写入 `INTERFACE_CONTRACT.md` | ✅ |
| 行星参数表 | 属 A 模块职责；C 提供精度档位接口供 A 调用 | ⚠️ 不在 C 范围 |

## 四、验收节点材料 — 已完成

| 里程碑 | 材料 | 状态 |
|---|---|---|
| 11/18 带纹理球体闭环 | `build/module_c_demo.png`、`module_c_seam.png` | ✅ |
| 11/19-23 土星环、双面观察、纹理接缝 | `module_c_ring_up.png`、`module_c_ring_down.png` | ✅ |
| 12/1-4 球面纹理接缝检查 | 接缝数值自测 + 接缝特写图 | ✅ |
| 测试/结果/论文方法说明 | `tests/test_module_c.cpp`（31 项）、`docs/VERIFICATION.md`、论文草稿 | ✅ |

## 五、明确不属于 C 的文件（由其他模块负责）

以下项目**不是 C 的交付物**，需要对应负责人完成，C 已提供接口配合：

| 模块 | 未完成内容 |
|---|---|
| A | `SolarSystemScene`、`SimulationClock`、行星参数表、公转自转层次变换、`SceneSnapshot`、NASA 数据整理 |
| B | 生产级 CPU 光栅器：变换/裁剪/覆盖/深度/透视正确插值（C 的 demo 内光栅器只用于自验证） |
| D | 单采样 / 4× SSAA / 4× MSAA、`SampleBuffer`、Resolve、着色计数、`PixelTrace`、参考图生成器 |
| E | GLFW/Metal/Dear ImGui、`CameraController`、射线拾取、像素显微镜、同步对比界面 |
| F | CMake 工程检查、`BenchmarkRunner`、PNG/CSV 导出、性能统计、Release 打包、论文统筹整合 |

## 六、需要在集成阶段确认的事项（非文件缺失）

1. `CMakeLists.txt` 已写好，但本机未安装 cmake，未能实跑；需在目标 MacBook Pro 上做一次干净构建。
2. 当前纹理全部程序化生成，没有外部图片文件；如最终论文决定使用 NASA 行星贴图，需由 A/F 整理来源并把
   PPM 文件挂到材质的 `albedo`，C 的 `loadPPM()` 接口已就绪。
3. C 的 `shade()` 尚未与 B 的生产光栅器联调（B 模块完成后按 `INTERFACE_CONTRACT.md` 接入即可）。

> 团队协作规则要求核心模块由协作组另一人审查：建议 A（场景）或 B（渲染管线）负责人对
> `docs/INTERFACE_CONTRACT.md` 做一次接口确认。
