# C++ + Graphviz `dot` 布局示例

这个小项目演示将 Graphviz 16.1.0 的有向图布局功能静态链接到 C++ 应用中。项目保留自己的图数据结构，使用 Graphviz `libgvc` 的 C API 调用 `dot`，输出节点中心坐标/宽高、边 spline 控制点，以及边标签的文本、中心坐标和宽高，供 ImGui 等 UI 绘制层消费。

Graphviz 上游使用 Autotools，不是 CMake 子项目。CMake 通过 FetchContent 下载 Graphviz 16.1.0 发布源码包，再由 `ExternalProject` 在独立构建目录运行 Graphviz 的 `configure` 和 `make`，最后将 `libgvc`、`dot_layout` 插件和 Pango 文本布局插件的静态归档链接到示例。这里的“静态链接”指 Graphviz 代码使用 `.a` 归档；系统的 Pango、Expat、zlib 等依赖仍由系统提供，不承诺生成完全静态的操作系统可执行文件。

## 依赖

- CMake 3.24+、C/C++ 编译器、GNU Make
- Graphviz 构建依赖，包含 `pangocairo`、`pangoft2`、Expat 和 zlib 的开发文件
- `pkg-config`、Python 3、Flex、Bison

Debian/Ubuntu 可安装基础依赖：

```sh
sudo apt install build-essential cmake pkg-config python3 gawk flex bison \
  libexpat1-dev zlib1g-dev libcairo2-dev libpango1.0-dev
```

## 构建和运行

在项目根目录执行：

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/dot_layout_demo
```

Graphviz 源码包会自动获取到构建目录中的 `_deps/graphviz-src/`；配置文件、对象文件和静态归档放在 `build/deps/graphviz/graphviz-build/`。

## 包装层

- [include/dot_layout.hpp](include/dot_layout.hpp) 定义应用图数据和布局结果类型。
- `dot_layout` 是可复用的静态 C++ 包装库目标；[src/dot_layout.cpp](src/dot_layout.cpp) 校验输入，构造 Graphviz 有向图，执行 `dot` 布局并返回节点/边几何信息。每个 spline group 保留 Graphviz 返回的控制点序列。
- [src/static_plugins.cpp](src/static_plugins.cpp) 显式注册静态链接的 `dot_layout` 与 Pango 插件，再通过 `gvContextPlugins(..., 0)` 创建不依赖运行时插件搜索的 Graphviz 上下文。
- [deps/graphviz/CMakeLists.txt](deps/graphviz/CMakeLists.txt) 获取上游源码，驱动静态构建，并链接 Graphviz 归档及其系统依赖。

节点和边的坐标/尺寸均以 Graphviz point 为单位（72 point = 1 inch）；节点的 Graphviz `pos` 属性以 point 返回，`width`/`height` 属性以 inch 返回，包装类统一换算为 point。边 spline 控制点、边标签中心及标签尺寸直接从布局后的 Graphviz 结构读取；其中标签尺寸由 Graphviz 布局估算。边几何使用 Graphviz 16.1.0 的 `common/types.h` 布局结构，升级 Graphviz 时应重新核对这些字段。`bounds` 仍是节点矩形的包围框，不包含边曲线或边标签的延伸范围。

ImGui 绘制时，可按自己的缩放比例将 Graphviz 坐标从图形坐标变换到屏幕坐标。例如以 `bounds` 左上角作为屏幕原点时：

```cpp
const float x = origin.x + (node.center.x - layout.bounds.min_x) * scale;
const float y = origin.y + (layout.bounds.max_y - node.center.y) * scale;
const float width = node.width * scale;
const float height = node.height * scale;
drawList->AddRect(
    ImVec2(x - width * 0.5f, y - height * 0.5f),
    ImVec2(x + width * 0.5f, y + height * 0.5f),
    IM_COL32(220, 220, 220, 255));

auto toScreen = [&](dot_layout::Point point) {
  return ImVec2(
      origin.x + (point.x - layout.bounds.min_x) * scale,
      origin.y + (layout.bounds.max_y - point.y) * scale);
};

for (const auto& edge : layout.edges) {
  for (const auto& spline : edge.spline_points) {
    for (std::size_t i = 0; i + 3 < spline.size(); i += 3) {
      drawList->AddBezierCubic(
          toScreen(spline[i]), toScreen(spline[i + 1]),
          toScreen(spline[i + 2]), toScreen(spline[i + 3]),
          IM_COL32(180, 180, 180, 255), 1.0f);
    }
  }
  if (edge.label) {
    const ImVec2 center = toScreen(edge.label->center);
    const ImVec2 half_size(edge.label->width * scale * 0.5f,
                           edge.label->height * scale * 0.5f);
    drawList->AddText(
        ImVec2(center.x - half_size.x, center.y - half_size.y),
        IM_COL32(240, 240, 240, 255), edge.label->text.c_str());
  }
}
```

Graphviz 的图结构和布局 API 是 C 接口；包装类负责管理上下文/图的生命周期，并将 Graphviz 位置结果转换为应用自己的 C++ 类型。示例不直接依赖 ImGui。
