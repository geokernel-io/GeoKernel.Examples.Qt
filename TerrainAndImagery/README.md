# TerrainAndImagery

**Example 02:** after TerrainLoading. Display a DEM with an orthophoto
draped on its surface. Roads, buildings, 3D Tiles and globe layers are not loaded.

## Build

Uses the same CMake presets, AcquireGeoKernelSdk helper, application resources and
runtime deployment as the other Qt examples. Requires a Vulkan-capable driver and
a GeoKernel SDK containing Viewer3D.

From an x64 Native Tools command prompt in this directory:

```bat
cmake --preset windows-msvc -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build --preset windows-msvc-release
```

The helper downloads the latest public SDK. To use a local 3D-enabled package,
add `-DGEOKERNEL_ROOT=C:/path/to/sdk` during configuration.
The executable is `outputs/windows/Release/bin/TerrainAndImagery.exe`.
Linux presets follow the repository convention but were not tested in this change.

## Automatic sample data

Run the application without arguments. It downloads and extracts these packages,
then automatically loads the terrain and orthophoto:

- [Terrain](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_terrain.zip)
- [Orthophoto](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_ortophoto.zip)

Data is cached beside the executable, following the other examples:

```text
data/
  sagrada_familia_terrain/sagrada_familia_terrain.tif
  sagrada_familia_ortophoto/sagrada_familia_ortophoto.tif
```

Later launches reuse the extracted files without downloading. Each extraction is
marked complete only after the expected file is found. The executable directory
must be writable. ZIP extraction uses tar on Windows and unzip on Linux.

A progress dialog supports cancelling the download. Use **Reload sample** to
retry after a failure. Already prepared packages are retained.

The package does not establish a verified vertical datum. DEM values are treated
as metres with an unknown datum for preview; no geoid correction is applied.

## Controls

- **Show orthophoto:** toggle the image to compare it with the terrain.
- **Vertical exaggeration:** change visual terrain height from 0.25× to 10× (default 1×), without reloading or changing source elevations. The value is retained when reloading the sample.
- **Reset view:** frame the terrain; keeps the selected exaggeration.
- Left drag: pan; wheel/right drag: zoom.
- Middle drag or Ctrl+left: orbit; Shift+left: look.

CPU terrain loading happens on a worker and can be cancelled. GPU scene commit
happens on the GUI thread. Preparation failure/cancellation retains the previous
scene. Closing joins the loading worker before SDK teardown.

## Automatic SDK and data setup

Use the standard presets without GEOKERNEL_ROOT to download the latest public
SDK. The common acquisition helper verifies the release checksum and caches the
SDK in `.geokernel-sdk`. No local GeoKernel source tree or private SDK is required.
To switch an existing build from an old manually selected SDK back to automatic
acquisition, configure with:

```bat
cmake --preset windows-msvc -UGEOKERNEL_ROOT -UGeoKernel_DIR -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build --preset windows-msvc-release
```

An explicit `-DGEOKERNEL_ROOT=C:/path/to/sdk` remains optional, as in other Qt
examples. A release exporting GeoKernel::Viewer3D is mandatory. SDK acquisition
succeeding does not imply that a 2D-only release supports this example.

At first launch, sample ZIPs are downloaded, extracted under `data` beside the
executable and opened automatically. Later launches reuse validated local files.
The initial loading page remains visible until the real scene is installed;
there is no temporary synthetic scene. Download cancellation and retry are supported.
