# C++ + Graphviz `dot` 布局示例

这个小项目演示将 Graphviz 16.1.0 的有向图布局功能静态链接到 C++ 应用中。项目保留自己的图数据结构，使用 Graphviz `libgvc` 的 C API 调用 `dot`，输出节点和子图边界、边 spline 控制点，以及边标签的文本、中心坐标和宽高，供 ImGui 等 UI 绘制层消费。

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
ctest --test-dir build -R '^(log_tests|dot_layout_tests|render_graph_tests|make_graph_tests|performance_pipeline_tests|smc_error_markers_tests|smc_editor_language_tests|user_settings_tests)$' --output-on-failure
./build/dot_layout_demo tests/fsm_graph.sm
```

Graphviz 源码包会自动获取到构建目录中的 `_deps/graphviz-src/`；配置文件、对象文件和静态归档放在 `build/deps/graphviz/graphviz-build/`。
`dot_layout_tests`、`render_graph_tests`、`make_graph_tests` 和 `performance_pipeline_tests` 不需要图形界面；另有 `dot_layout_demo_runs` 测试会启动交互窗口，需手动关闭。

可使用以下命令测量指定输入的分析 CPU 时间，以及无图形界面时缓存帧和每帧强制重新布局的平均 CPU 时间：

```sh
./build/performance_pipeline_tests --benchmark deps/smc.cpp/SmcParser.sm
```

强制重新布局用于对比布局缓存，并不是完整旧版本的端到端基准；结果受构建类型和运行环境影响。

## 包装层

- [include/dot_layout.hpp](include/dot_layout.hpp) 定义应用图数据和布局结果类型。
- `dot_layout` 是可复用的静态 C++ 包装库目标；[src/dot_layout.cpp](src/dot_layout.cpp) 校验输入，构造 Graphviz 有向图，执行 `dot` 布局并返回节点/边几何信息。每个 spline group 保留 Graphviz 返回的控制点序列。
- [src/static_plugins.cpp](src/static_plugins.cpp) 显式注册静态链接的 `dot_layout` 与 Pango 插件，再通过 `gvContextPlugins(..., 0)` 创建不依赖运行时插件搜索的 Graphviz 上下文。
- [deps/graphviz/CMakeLists.txt](deps/graphviz/CMakeLists.txt) 获取上游源码，驱动静态构建，并链接 Graphviz 归档及其系统依赖。

每个节点可以分别设置 `NodeShape::Box`、`Circle`、`Ellipse` 或 `Point`，默认是 `Box`。`NodeLayout::shape` 返回选定形状，供绘制层使用；`Point` 按 Graphviz 的行为不显示节点文字。每条边可以分别设置整数 `weight`（1–1000，默认 1）；较高权重使 `dot` 更倾向于缩短、拉直边，并使其沿层级方向对齐，但具体几何取决于整张图，不能保证每条边都严格变化。权重不会作为标签显示，已有边标签仍正常显示。非法形状或权重会抛出 `std::invalid_argument`。旧的两字段节点和三字段边聚合初始化仍可使用：

```cpp
dot_layout::DirectedGraph graph{
    {{"a", "A"}, {"b", "B", dot_layout::NodeShape::Circle}},
    {{"a", "b", "label", 5}}};
auto layout = dot_layout::DotLayout{}.layout(graph);
```

示例的 `settings` 菜单提供逐节点形状、逐边权重、Compound 开关和 Ratio 设置。关闭 Compound 时，示例暂时将边连接到节点而不是 cluster；重新开启后恢复边的 cluster 端点。Ratio 可选择默认、Fill、Compress、Expand 或数值模式；命名模式可设置宽高（英寸）。边控件编号用于区分平行边；更改仅保留在当前运行期间。示例绘制 cluster 边框和标签，对非 point 节点显示节点标签，不显示 point 文字。

`make_graph(input_file)` 读取并校验 SMC 文件，将每个 state map 转换为同名 cluster，将其全部状态（包括解析器补充的 `Default`）转换为节点。各 map 的同名状态使用不同 ID，只有 `%start` 指定的状态使用椭圆，其余默认使用矩形。每个 transition guard 对应一条边；标签包含事件名、参数、条件、动作以及 push/pop/jump 信息。普通目标、jump 和 push 支持当前 map 中的名称及 `map::state` 名称；push 边指向压入的状态，标签保留返回状态。`nil` 表示自环；pop 的真实目标由运行时状态栈决定，图中用标有 `dynamic destination` 的自环占位，不表示实际返回当前状态。文件读取、解析或校验失败时返回 `std::nullopt` 并输出诊断。运行示例时可通过第一个命令行参数指定文件；不指定时读取当前目录的 `mapmanager.sm`。

示例 UI 包含独立的 SMC 编辑器、图形画布和状态/诊断窗口。编辑器使用 SMC 语法高亮；停止编辑 500 毫秒后，后台线程解析、校验并计算 DOT 布局，期间继续显示最后一次成功的图。布局设置修改也在后台计算；过期结果不会覆盖最新输入或设置。只有分析和布局均成功时才替换当前有向图，布局结果会缓存，不会在每帧重算。解析得到的 state map、state、transition 和 guard 的 `SourceLocation` 会转换为源文本一维区间，并映射到对应的 cluster、node 或 edge。光标所在对象变化时，图画布自动平移使该对象居中，并以绿色边框/线条/文字高亮；transition 覆盖其全部 guard edges。输入无效或布局失败时，状态窗口会显示诊断并保留最后一次成功的图，源码修改后会停用过期的光标映射；启动文件读取或分析失败时，显示内置示例图，仍可在编辑器中输入有效内容。

SMC 解析和语法校验错误会以红色背景标记编辑器中的行号和源码，悬浮可查看错误列和消息；同一行的多个错误合并显示。编辑源码时立即清除旧标记，最新源码分析完成后再更新；警告、布局错误和 C++ 生成错误仍通过日志报告，不添加行标记。

在画布上单击节点、边（包括曲线和标签）或 cluster 的边框、标题，可高亮命中元素并在编辑器中选中其完整源码范围；边选中所属 transition 的完整范围，但只高亮被点击的 edge。编辑器会滚动到选区开头，画布不会因此移动；再次操作编辑器时恢复光标驱动的联动。点击空白处保持原选择，拖拽仍用于平移。源码映射过期或元素没有对应源码（例如自动生成的 `Default` 节点、内置示例图）时，只改变画布焦点，不改变源码选区。

编辑器的 `ast` 菜单按 state map、state、transition、guard 层级提供快捷跳转；点击节点选中其完整源码并聚焦对应画布元素，transition 聚焦全部 guard edges，guard 聚焦单条 edge。没有源码范围的合成节点不显示，源码修改后菜单暂不可用，直到最新分析成功。`C++` 菜单根据当前编辑器内容生成同目录下同名 `.hpp` 和 `.cpp`；若输出文件已存在，需确认后才覆盖，生成或写入失败时会在日志窗口显示诊断。

日志接口位于 [include/log.hpp](include/log.hpp)：`DiagnosticLog::write` 接收等级、来源和详情，并记录产生时间；写入、清空和快照均可跨线程安全调用。默认保留最近 10,000 条记录，超出时淘汰最旧记录；同时输出到标准错误。后台分析的诊断仅在对应结果被接受时发布，过期结果不会显示或输出。状态窗口按 Debug、Info、Warning、Error 着色，可分别开关等级并按来源的大小写敏感子串过滤；空来源过滤显示全部。多行记录计为一条，窗口按行裁剪渲染；Clear 清空历史，过滤不会删除记录。

UI 代码按职责拆分：[`src/main.cpp`](src/main.cpp) 只保留入口，`src/application.cpp` 管理 GLFW/ImGui 生命周期和每帧合并，`src/editor_window.cpp`、`src/graph_window.cpp`、`src/log_window.cpp` 分别渲染窗口；`AppContext` 汇集运行时状态，`UserSettings` 通过 ImGui INI 保存窗口尺寸和画布设置。节点形状与边权重仅在图结构和标签匹配时恢复，避免将旧偏好误用到不同的图。

### 子图、compound 和 ratio

`DirectedGraph::clusters` 定义作为 Graphviz cluster 布局的子图，可通过 `parent` 指定嵌套关系（顺序不限）。节点须先在 `DirectedGraph::nodes` 定义，`Cluster::nodes` 只列出直接成员；一个节点只能直接属于一个 cluster，父 cluster 自动包含子 cluster 的节点。cluster ID 必须非空且唯一，每个 cluster 的子树至少包含一个节点。标签由 Graphviz 用于布局，`LayoutResult::clusters` 按输入顺序返回 ID、边界及非空标签的中心坐标和尺寸，供调用方绘制。此接口不表示普通的非 cluster 子图。

```cpp
using namespace dot_layout;
DirectedGraph graph{
    {{"a", "A"}, {"b", "B"}},
    {{"a", "b", "", 1, "left", "right"}},
    {{"left", "Left", {"a"}}, {"right", "Right", {"b"}}}};
LayoutOptions options;
options.compound = true;
options.size = Size{8.0, 6.0}; // 英寸
options.ratio = RatioMode::Fill; // 或 Compress、Expand、1.0
auto result = DotLayout{}.layout(graph, options);
```

边仍以实际节点为端点；可选的 `tail_cluster` / `head_cluster` 对应 Graphviz 的 `ltail` / `lhead`，要求 `compound=true`，对应节点在该 cluster 内、另一端节点在该 cluster 外（允许嵌套）。未设置时仍连接节点。无效引用、重复成员、空 cluster、父级循环及无效复合边均抛出 `std::invalid_argument`。布局结果中的曲线起点和箭尖是 Graphviz 裁剪后的 cluster 边界位置。

`ratio` 可省略（保留 Graphviz 默认）、设为 `RatioMode::Fill`、`Compress`、`Expand`，或正的有限浮点数（高/宽）。三种命名模式要求 `size`，`size` 的宽高是正的有限英寸值；尺寸也可单独设置。Graphviz 在绘制前应用 ratio，节点大小不随之缩放，且最终输出渲染阶段可能还会按 `size` 缩放；本包装层返回布局坐标，不做渲染缩放。因此 fill 不保证返回的坐标恰好等于指定英寸尺寸，数值 ratio 也不保证最终可见包围框严格等于指定比例。

节点和边的坐标/尺寸均以 Graphviz point 为单位（72 point = 1 inch）；节点的 Graphviz `pos` 属性以 point 返回，`width`/`height` 属性以 inch 返回，包装类统一换算为 point。边 spline 控制点、边标签中心及标签尺寸直接从布局后的 Graphviz 结构读取；其中标签尺寸由 Graphviz 布局估算。`EdgeLayout::arrow_tips` 与 `spline_points` 一一对应：有箭头的组包含箭尖坐标；普通节点终点沿节点中心到 Graphviz 箭尖的方向校正到节点绘制边界，cluster 终点保留 Graphviz 的裁剪位置；没有箭头的组为 `std::nullopt`。曲线终点是 Graphviz 为箭头预留空间后的位置，绘制时应连接终点与箭尖，并以箭尖为三角形顶点；自定义箭头大小不改变箭尖位置。边几何使用 Graphviz 16.1.0 的 `common/types.h` 布局结构，升级 Graphviz 时应重新核对这些字段。`bounds` 包含节点和 cluster 包围矩形、边 spline 包围框、箭尖和边标签范围，不包含自定义箭头三角形的全部范围。

ImGui 绘制时，可按自己的缩放比例将 Graphviz 坐标从图形坐标变换到屏幕坐标。例如以 `bounds` 左上角作为屏幕原点时：

```cpp
const float x = origin.x + (node.center.x - layout.bounds.min_x) * scale;
const float y = origin.y + (layout.bounds.max_y - node.center.y) * scale;
const float width = node.width * scale;
const float height = node.height * scale;
if (node.shape == dot_layout::NodeShape::Box) {
  drawList->AddRectFilled(
      ImVec2(x - width * 0.5f, y - height * 0.5f),
      ImVec2(x + width * 0.5f, y + height * 0.5f),
      IM_COL32(220, 220, 220, 255));
} else {
  drawList->AddEllipseFilled(
      ImVec2(x, y), ImVec2(width * 0.5f, height * 0.5f),
      IM_COL32(220, 220, 220, 255));
}
// 仅为非 Point 节点绘制文字。

auto toScreen = [&](dot_layout::Point point) {
  return ImVec2(
      origin.x + (point.x - layout.bounds.min_x) * scale,
      origin.y + (layout.bounds.max_y - point.y) * scale);
};

for (const auto& edge : layout.edges) {
  for (std::size_t group = 0; group < edge.spline_points.size(); ++group) {
    const auto& spline = edge.spline_points[group];
    for (std::size_t i = 0; i + 3 < spline.size(); i += 3) {
      drawList->AddBezierCubic(
          toScreen(spline[i]), toScreen(spline[i + 1]),
          toScreen(spline[i + 2]), toScreen(spline[i + 3]),
          IM_COL32(180, 180, 180, 255), 1.0f);
    }
    if (!spline.empty() && edge.arrow_tips[group]) {
      // 连接曲线终点与箭尖，并按需以箭尖为顶点绘制箭头三角形。
      drawList->AddLine(toScreen(spline.back()), toScreen(*edge.arrow_tips[group]),
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

Graphviz 的图结构和布局 API 是 C 接口；包装类负责管理上下文/图的生命周期，并将 Graphviz 位置结果转换为应用自己的 C++ 类型。包装库不直接依赖 ImGui。
