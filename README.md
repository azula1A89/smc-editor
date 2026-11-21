[English](README.md) | [简体中文](README_zh.md)

# smc-editor

`smc-editor` is a desktop editor for SMC state-machine definitions. Edit `.sm` files, explore their state diagrams, review diagnostics, and generate C++ source files from your work.


https://github.com/user-attachments/assets/5cfe3e06-39d9-4964-97de-bfec31b2acde


## Features

- Edit SMC files with syntax-aware highlighting and diagnostics.
- Explore state maps, states, and transitions in an interactive diagram.
- Navigate between source code and diagram elements.
- Generate `.hpp` and `.cpp` files from an SMC definition.

Pre-built binaries are available on the releases page: 

- [Linux x64](https://github.com/azula1A89/smc-editor/releases/latest/download/smc-editor-linux-x86_64.tar.gz)
- [Windows x64](https://github.com/azula1A89/smc-editor/releases/latest/download/smc-editor.exe)

## Build and run

### Requirements

- CMake 3.24 or newer
- A C and C++ compiler, GNU Make, and Git
- Development packages for Pango, Cairo, Expat, and zlib
- `pkg-config`, Python 3, Flex, and Bison

On Debian or Ubuntu, install the required packages with:

```sh
sudo apt install build-essential cmake pkg-config python3 gawk flex bison \
  libexpat1-dev zlib1g-dev libcairo2-dev libpango1.0-dev
```

Configure and build from the project directory:

```sh
cmake -S . -B build
cmake --build build --parallel
```

Launch the editor with an SMC file:

```sh
./build/smc-editor tests/fsm_graph.sm
```

If no file is specified, the application opens with its built-in example.

### Windows

Install Visual Studio 2022 or newer with the **Desktop development with C++**
workload and clone [vcpkg](https://github.com/microsoft/vcpkg). From a
Developer PowerShell, run:

```powershell
cmake -S . -B build -DBUILD_TESTING=OFF `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
```

## Documentation

See [Detail.md](Detail.md) for additional build notes, usage guidance, and technical details.

## Third-party code

The project fetches and uses the following third-party code:

| Library | Version | License |
| --- | --- | --- |
| [Dear ImGui](https://github.com/ocornut/imgui) | v1.92.9b-docking | [MIT](https://github.com/ocornut/imgui/blob/v1.92.9b-docking/LICENSE.txt) |
| [GLFW](https://github.com/glfw/glfw) | 3.5.1 | [zlib/libpng](https://github.com/glfw/glfw/blob/3.5.1/LICENSE.md) |
| [GLEW](https://github.com/nigels-com/glew) | 2.3.1 | [BSD-style and additional notices](https://github.com/nigels-com/glew/blob/glew-2.3.1/LICENSE.txt) |
| [FreeType](https://github.com/freetype/freetype) | 2.14.3 | [FTL or GPL-2.0-or-later](https://github.com/freetype/freetype/blob/VER-2-14-3/LICENSE.TXT) |
| [Graphviz](https://gitlab.com/graphviz/graphviz) | 16.1.0 | [EPL-2.0](https://gitlab.com/graphviz/graphviz/-/blob/16.1.0/COPYING) |
| [ImGuiFileSelector](https://github.com/goossens/ImGuiFileSelector) | v1.93.0 | [MIT](https://github.com/goossens/ImGuiFileSelector/blob/v1.93.0/LICENSE) |
| [ImGuiColorTextEdit](https://github.com/goossens/ImGuiColorTextEdit) | v1.93.0 | [MIT](https://github.com/goossens/ImGuiColorTextEdit/blob/v1.93.0/LICENSE) |
| [smc.cpp](https://github.com/azula1A89/smc.cpp) | commit `8e9b609` | [MIT](https://github.com/azula1A89/smc.cpp/blob/8e9b6096454881d205e159fe83b194f5bf1c35e0/LICENSE) |

These are upstream projects with their own license terms; see each linked license for details. System-provided dependencies are not included in this list.

## License

This project is distributed under the [MIT License](LICENSE).
