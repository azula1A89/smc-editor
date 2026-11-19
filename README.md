[English](README.md) | [简体中文](README_zh.md)

# smc-editor

`smc-editor` is a desktop editor for SMC state-machine definitions. Edit `.sm` files, explore their state diagrams, review diagnostics, and generate C++ source files from your work.

## Features

- Edit SMC files with syntax-aware highlighting and diagnostics.
- Explore state maps, states, and transitions in an interactive diagram.
- Navigate between source code and diagram elements.
- Generate `.hpp` and `.cpp` files from an SMC definition.

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

## Documentation

See [Detail.md](Detail.md) for additional build notes, usage guidance, and technical details.

## License

This project is distributed under the [MIT License](LICENSE).
