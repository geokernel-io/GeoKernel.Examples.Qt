# GlobeTerrainAnalysis

Barcelona terrain analysis: select Line of sight, Viewshed, Slope / aspect or Measure, then click the globe to choose analysis points. Results appear in a fixed-height bottom panel.

## Requirements

- Windows x64, MSVC x64 tools, CMake 3.23+ and Ninja.
- Qt 6.11.0 MSVC, Vulkan SDK and a Vulkan-capable GPU/driver.
- A GeoKernel SDK exporting `GeoKernel::Globe`; the latest public release is selected by default.
- Internet for the first SDK/data download and for live Esri layers.

## Build and run

From this directory in an x64 Visual Studio developer terminal:

```bat
cmake --preset windows-msvc -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build --preset windows-msvc-release
outputs\windows\Release\bin\GlobeTerrainAnalysis.exe
```

If Ninja is not on PATH, add `-DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe` to configure.
`QTDIR` can also specify Qt. The SDK is downloaded into `.geokernel-sdk` and
verified against the public release SHA-256 checksum, using the same acquisition
helper as HelloMap and Buildings3D. Reconfigure after publishing a new SDK.
Presets clear old local SDK overrides; an explicit `-DGEOKERNEL_ROOT=C:/sdk/geokernel`
can select an extracted development SDK. Package discovery does not fall back to
a source checkout or another cached installation.

These examples provide Release build presets for Windows. SDK release 1.5.34
includes Globe on Windows and Linux; these example presets currently target
Windows. If a selected SDK lacks `GeoKernel::Globe`, configure stops with a
specific error.

## Data

The Sagrada Familia terrain archive is downloaded automatically. Analysis uses the DEM; imagery does not supply building heights.

Required archives come from the [GeoKernel sample data release](https://github.com/geokernel-io/GeoKernel.SampleData/releases/tag/v1).
CMake checks their SHA-256 hashes, caches them in `.sample-data`, and copies
extracted data beside the executable. A matching archive in `data/` can be used
offline. Interrupted transfers use `.part` files and are retried on configure.
No synthetic fallback data is generated. Live Esri layers require internet.

## Runtime and validation

The executable, SDK/Qt libraries, plugins, geospatial support files and sample
data are placed together in `outputs/windows/Release/bin`. Run or copy the whole
folder. Each example is standalone; no sibling example or SDK source is needed.
Build, cache and output directories are excluded from Git.

The application opens directly without sample-specific command-line options.
The build directory contains `sdk-selection.txt` with SDK/toolchain details.
