# 模块 C：模型、纹理与光照 — 可配置球体/环带模型与材质着色模块

本目录是小组项目「基于软件光栅化的交互式太阳系可视化与抗锯齿分析」中 **C 模块** 的独立实现。
按 `PROJECT_PLAN.md` 的分工，C 负责场景视觉与着色：球体网格、土星环、轨道细带、UV 与法线、
球面纹理接缝、纹理采样、太阳自发光、行星漫反射与环境光。

## 目录结构

```
ame/
├── CMakeLists.txt                  # 静态库 + 自测 + demo 的构建脚本
├── include/solarc/
│   ├── math.h                      # Vec2/Vec3/Mat4，公共坐标与矩阵约定
│   ├── types.h                     # Vertex / Mesh / Texture / Material（公共接口）
│   ├── geometry.h                  # 球体、环带、轨道带、测试三角形的构造函数
│   ├── texture.h                   # sRGB 转换、PPM 读写、采样、程序化测试纹理
│   └── shading.h                   # 材质求值与工厂函数
├── src/                            # 上述接口的实现
├── tests/test_module_c.cpp         # 31 项自测：网格/法线/绕序/UV/采样/光照/精度档位
├── demo/demo_render.cpp            # 独立 CPU 光栅器，四种验证模式
├── docs/
│   ├── INTERFACE_CONTRACT.md       # 与 A/B/D/E/F 的接口约定（坐标系、颜色空间）
│   ├── MODULE_C_PAPER_DRAFT.md     # 论文对应章节草稿（网格、纹理、光照、环带、资源）
│   └── VERIFICATION.md             # 构建、运行、验证记录
└── build/                          # 本地构建产物与验证图
    ├── module_c_demo.png           # 主场景：带纹理光照的球体 + 环带
    ├── module_c_seam.png           # 球面纹理接缝验证（u=0/1 无可见断裂）
    ├── module_c_ring_up.png        # 环带上表面（+Y 法线）
    └── module_c_ring_down.png      # 环带下表面（-Y 法线）
```

demo 支持四种模式：`main | seam | ring-up | ring-down`，见 `docs/VERIFICATION.md`。

## 快速开始

需要 C++17 编译器（本机 MinGW g++ 16.1 可用）：

```bash
cd D:/anzhuang/bishe/作业/ame

# 自测（应输出 31 checks, 0 failures）
g++ -std=c++17 -Iinclude src/*.cpp tests/test_module_c.cpp -o build/test_module_c.exe
./build/test_module_c.exe

# 视觉验证（输出 4 张 PPM）
export PATH="/d/360Downloads/bdpan/mingw64/bin:$PATH"   # 避免系统里另一个 libstdc++-6.dll 冲突
g++ -std=c++17 -O2 -Iinclude src/*.cpp demo/demo_render.cpp -o build/demo_render.exe
./build/demo_render.exe build/module_c_demo.ppm 640 400 main
./build/demo_render.exe build/module_c_seam.ppm 480 360 seam
./build/demo_render.exe build/module_c_ring_up.ppm 480 360 ring-up
./build/demo_render.exe build/module_c_ring_down.ppm 480 360 ring-down
python3 -c "from PIL import Image; [Image.open('build/'+n+'.ppm').save('build/'+n+'.png') for n in ['module_c_demo','module_c_seam','module_c_ring_up','module_c_ring_down']]"
```

有 CMake 时可直接 `cmake -S . -B build-cmake && cmake --build build-cmake && ctest --test-dir build-cmake`。

## 交给其他模块的内容

- **A（场景）**：用 `makeSphereParams(radius, SphereDetail::Low|Medium|High)` 选精度（16×8 / 32×16 / 64×32），
  再交给 `buildSphere`；环带用 `buildRing`，轨道用 `buildOrbitRibbon`。材质用 `makeSunMaterial` /
  `makePlanetMaterial` / `makeRingMaterial` 创建。场景层只保存网格实例与材质参数，不复制几何数据。
- **B（渲染管线）**：消费 `Vertex`（position/normal/uv）与 `Mesh`（vertices/indices）；在无 albedo 贴图时
  用 `Material::baseColor`。采样点在 `ShadingInput` 中给出，调用 `shade()` 得到线性 RGBA。
- **D（采样诊断）**：`shade()` 是纯函数，每次调用只依赖输入，便于 SSAA 多次调用与 MSAA 单次调用对照着色次数。
- **E（界面）**：颜色在 `shade()` 输出中为线性空间；显示转换由 D/E 在 Resolve 阶段统一完成。

详细约定见 `docs/INTERFACE_CONTRACT.md`。
