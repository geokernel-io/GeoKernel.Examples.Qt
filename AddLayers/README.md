# GeoKernel AddLayers

Standalone Qt/C++ example that loads raster, polygon and point layers into one
GeoKernel viewer. The example downloads the latest public GeoKernel SDK and the
required sample data automatically; it does not depend on a local GeoKernel
source tree.

Downloaded sample files are cached under the executable's `data` directory.

## Requirements

- CMake 3.22 or newer
- Ninja
- C++17 compiler (MSVC on Windows, GCC/Clang on Linux)
- Qt 6: Core, Gui, Widgets, OpenGL, OpenGLWidgets, Network, Sql and Svg
- Linux only: `unzip`

## Windows

Run from an x64 Visual Studio Developer Command Prompt:

```bat
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
outputs\windows\Release\bin\AddLayers.exe
```

## Linux

```sh
cmake --preset linux-x64
cmake --build --preset linux-x64-release
./outputs/linux/Release/bin/AddLayers
```

Generated files are stored only in `build`, `outputs` and `.geokernel-sdk`.
