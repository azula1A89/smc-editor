[English](README.md) | [简体中文](README_zh.md)

# smc-editor

`smc-editor` 是一款用于编辑 SMC 状态机定义的桌面应用。你可以编辑 `.sm` 文件、查看状态图、检查诊断信息，并从状态机定义生成 C++ 源文件。

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

## 许可

本项目采用 [MIT 许可证](LICENSE)。
