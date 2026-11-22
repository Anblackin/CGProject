# 模块 C 验证记录

日期：2026-11-21。环境：Windows，MinGW g++ 16.1.0（C++17），Python 3 / Pillow 12.3。

## 1. 自测

命令：

```bash
cd D:/anzhuang/bishe/作业/ame
g++ -std=c++17 -Wall -Wextra -Iinclude src/*.cpp tests/test_module_c.cpp -o build/test_module_c.exe
./build/test_module_c.exe
```

结果：

```
=== Module C self-checks ===
[sphere]
[ring]
[texture]
[shading]
=== 31 checks, 0 failures ===
```

覆盖内容：

- 球体：顶点数 `(su+1)(sv+1)`、极点退化三角形剔除、半径与法线长度、经度接缝位置相同而 UV 分别为 0/1、
  全部三角形绕序朝外、`MeshStats` 计数；
- A+C 精度档位：Low=16×8、Medium=32×16、High=64×32，且各档保持半径；
- 环带：双面顶点数、半径范围、UV 范围、上下表面法线方向、上表面绕序、轨道细带宽度；
- 纹理：sRGB↔线性转换与往返、Repeat 跨接缝连续、Mirror 对称、双线性接缝色一致、Clamp 边界保持；
- 着色：Lambert 正反面明暗、环境光非零、法线归一化、太阳自发光与方向无关、unlit 返回基色。

## 2. 视觉 demo

命令（注意先把 MinGW 的 bin 放到 PATH 前面，避免系统里另一个 `libstdc++-6.dll` 导致启动崩溃）：

```bash
export PATH="/d/360Downloads/bdpan/mingw64/bin:$PATH"
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude src/*.cpp demo/demo_render.cpp -o build/demo_render.exe
./build/demo_render.exe build/module_c_demo.ppm     640 400 main
./build/demo_render.exe build/module_c_seam.ppm     480 360 seam
./build/demo_render.exe build/module_c_ring_up.ppm  480 360 ring-up
./build/demo_render.exe build/module_c_ring_down.ppm 480 360 ring-down
python3 -c "from PIL import Image; [Image.open('build/'+n+'.ppm').save('build/'+n+'.png') for n in ['module_c_demo','module_c_seam','module_c_ring_up','module_c_ring_down']]"
```

四张输出图（PPM + PNG）均已生成，并在本机逐张目视核对：

| 文件 | 模式 | 核对内容 | 结果 |
|---|---|---|---|
| `module_c_demo.png` (640×400) | main | 自发光太阳、带纹理与 Lambert 明暗的行星、倾斜环带、轨道细带 | 正常 |
| `module_c_seam.png` (480×360) | seam | 从 +X 方向正视 `u=0/1` 接缝，16×16 棋盘纹理在接缝处连续 | 无可见断裂 |
| `module_c_ring_up.png` (480×360) | ring-up | 从 +Y 观察环带上表面，能看到两处环缝暗带 | 正常 |
| `module_c_ring_down.png` (480×360) | ring-down | 从 -Y 观察环带下表面，同样有明暗与环缝 | 正常，证明双面几何与法线正确 |

这组图对应 `PROJECT_PLAN.md` 中 11/18「带纹理球体闭环」与 11/19–23「土星环、双面观察与纹理接缝」
两项验收，可直接作为论文中的模块 C 示意图（需注明「模块 C 独立验证图，非最终系统截图」）。

## 3. 已知事项

- demo 里视口 y 映射曾多写一次 `+ halfH`，导致画面整体下移半屏；已修正为 `(1 - y) * halfH`。
- Windows 上若 PATH 中存在别的 `libstdc++-6.dll`，优化编译的程序可能在启动阶段崩溃；
  将 MinGW `bin` 置前即可，与源码无关。
- 本模块不含外部图片资源，所有测试纹理程序化生成；如需 NASA 贴图，使用 `loadPPM()` 替换材质的 `albedo`。
