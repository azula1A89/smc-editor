[English](README.md) | [简体中文](README_zh.md)

# smc-editor

`smc-editor` 是一款用于编辑 SMC 状态机定义的桌面应用。你可以编辑 `.sm` 文件、查看状态图、检查诊断信息，并从状态机定义生成 C++ 源文件。

https://github.com/user-attachments/assets/5cfe3e06-39d9-4964-97de-bfec31b2acde

## 功能

- 编辑 SMC 文件，提供语法高亮和诊断信息。
- 通过交互式图表查看状态映射、状态和转换。
- 在源代码与图表元素之间导航。
- 根据 SMC 定义生成 `.hpp` 和 `.cpp` 文件。

## 构建和运行

### 环境要求

- CMake 3.24 或更新版本
- C 和 C++ 编译器、GNU Make、Git
- Pango、Cairo、Expat 和 zlib 的开发文件
- `pkg-config`、Python 3、Flex 和 Bison

Debian 或 Ubuntu 用户可通过以下命令安装所需依赖：

```sh
sudo apt install build-essential cmake pkg-config python3 gawk flex bison \
  libexpat1-dev zlib1g-dev libcairo2-dev libpango1.0-dev
```

在项目目录中配置并构建：

```sh
cmake -S . -B build
cmake --build build --parallel
```

使用 SMC 文件启动编辑器：

```sh
./build/smc-editor tests/fsm_graph.sm
```

如果未指定文件，应用将打开内置示例。

## 文档

更多构建说明、使用指南和技术细节请参见 [Detail.md](Detail.md)。

## 第三方代码

本项目获取并使用了以下第三方代码：

| 库 | 版本 | 许可证 |
| --- | --- | --- |
| [Dear ImGui](https://github.com/ocornut/imgui) | v1.92.9b-docking | [MIT](https://github.com/ocornut/imgui/blob/v1.92.9b-docking/LICENSE.txt) |
| [GLFW](https://github.com/glfw/glfw) | 3.5.1 | [zlib/libpng](https://github.com/glfw/glfw/blob/3.5.1/LICENSE.md) |
| [GLEW](https://github.com/nigels-com/glew) | 2.3.1 | [BSD 风格及其他声明](https://github.com/nigels-com/glew/blob/glew-2.3.1/LICENSE.txt) |
| [FreeType](https://github.com/freetype/freetype) | 2.14.3 | [FTL 或 GPL-2.0-or-later](https://github.com/freetype/freetype/blob/VER-2-14-3/LICENSE.TXT) |
| [Graphviz](https://gitlab.com/graphviz/graphviz) | 16.1.0 | [EPL-2.0](https://gitlab.com/graphviz/graphviz/-/blob/16.1.0/COPYING) |
| [ImGuiFileSelector](https://github.com/goossens/ImGuiFileSelector) | v1.93.0 | [MIT](https://github.com/goossens/ImGuiFileSelector/blob/v1.93.0/LICENSE) |
| [ImGuiColorTextEdit](https://github.com/goossens/ImGuiColorTextEdit) | v1.93.0 | [MIT](https://github.com/goossens/ImGuiColorTextEdit/blob/v1.93.0/LICENSE) |
| [smc.cpp](https://github.com/azula1A89/smc.cpp) | commit `8e9b609` | [MIT](https://github.com/azula1A89/smc.cpp/blob/8e9b6096454881d205e159fe83b194f5bf1c35e0/LICENSE) |

这些第三方项目均受其各自许可证条款约束，详情请参见表格中的许可证链接。系统提供的依赖未列在此处。

## 许可

本项目采用 [MIT 许可证](LICENSE)。
