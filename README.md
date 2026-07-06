# GeoKernel Qt Examples

This repository contains Qt/C++ example applications for the GeoKernel native SDK.

## Configure

The default presets expect the GeoKernel SDK repository next to this repository:

```text
D:/projects/GeoKernel
D:/projects/GeoKernel.Examples.Qt
```

If GeoKernel is somewhere else, pass `GEOKERNEL_ROOT` when configuring.

## Windows

```bat
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release --target HelloMap
```

### Visual Studio

Open `D:\projects\GeoKernel.Examples.Qt` with **File > Open > Folder**.
Visual Studio should pick the `windows-msvc` CMake preset, configure the folder, and list the executable CMake targets such as `HelloMap`, `AddLayers`, and `SamplesGallery`.
In Solution Explorer, switch to **Targets View**, right-click `HelloMap`, then choose **Set as Startup Item** or **Build**.

If the build/run targets do not appear, close Visual Studio and delete the local `.vs` folder. It is only a Visual Studio cache and can keep stale solution state after moving from MSBuild projects to CMake.

## Linux

```sh
cmake --preset linux
cmake --build --preset linux-release --target HelloMap
```

## macOS

```sh
cmake --preset macos
cmake --build --preset macos-release --target HelloMap
```

Sample executables and runtime files are written under `outputs/<platform>/<Configuration>/bin/`, for example `outputs/windows/Release/bin/HelloMap.exe`.
The build also copies sample data and icons under `outputs/assets/` so existing sample code can resolve `assets/data` and `assets/images` at runtime.
