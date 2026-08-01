# LayerVisibility

Standalone GeoKernel Qt/C++ example converted from `examples/LayerManagement/LayerVisibility`.

The project downloads the latest public GeoKernel SDK for the host platform
and caches it under `.geokernel-sdk`. Sample data is downloaded at runtime
into the executable's `data` directory.

## Windows

Run from an x64 Visual Studio Developer Command Prompt:

```bat
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
outputs\windows\Release\bin\LayerVisibility.exe
```

## Linux

```bash
cmake --preset linux-x64
cmake --build --preset linux-x64-release
./outputs/linux/Release/bin/LayerVisibility
```
