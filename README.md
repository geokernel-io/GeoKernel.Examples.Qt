# GeoKernel Qt Examples

This repository contains Qt/C++ example applications for the GeoKernel native SDK.

## Configure

On Windows x64, the default preset downloads the pinned GeoKernel C++ SDK from
the GitLab Generic Package Registry, verifies its SHA256 checksum, and caches it
under `.geokernel-sdk/`. No sibling GeoKernel source checkout is required.

For the first download, provide a GitLab token that can read the package
registry. Keep the token in the process environment; do not add it to a preset,
CMake cache, or source file.

## Windows

```powershell
$env:GEOKERNEL_GITLAB_TOKEN = "<read-package token>"
$env:GEOKERNEL_GITLAB_TOKEN_TYPE = "PRIVATE-TOKEN"
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release --target HelloMap
```

`GEOKERNEL_GITLAB_TOKEN_TYPE` may be `PRIVATE-TOKEN`, `DEPLOY-TOKEN`, or
`JOB-TOKEN`. It defaults to `PRIVATE-TOKEN`. After the verified SDK is cached,
subsequent configure and build operations do not require the token.

GeoKernel developers can bypass the registry and use a local source build or
an already extracted SDK:

```powershell
cmake --preset windows-msvc -DGEOKERNEL_ROOT=D:/projects/GeoKernel
```

### Visual Studio

Open `D:\projects\GeoKernel.Examples.Qt` with **File > Open > Folder**.
Visual Studio should pick the `windows-msvc` CMake preset, configure the folder, and list the executable CMake targets such as `HelloMap`, `AddLayers`, and `SamplesGallery`.
In Solution Explorer, switch to **Targets View**, right-click `HelloMap`, then choose **Set as Startup Item** or **Build**.

If the build/run targets do not appear, close Visual Studio and delete the local `.vs` folder. It is only a Visual Studio cache and can keep stale solution state after moving from MSBuild projects to CMake.

## Linux

Automatic registry acquisition is currently Windows x64 only. Set
`GEOKERNEL_ROOT` to a compatible local SDK before configuring Linux or macOS.

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
Sample icons are embedded in the common helper code. Sample data is downloaded by examples that have been migrated to the remote-data flow.
