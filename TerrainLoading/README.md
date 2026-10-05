# TerrainLoading

**Example 01:** start here, then continue to
[TerrainAndImagery](../TerrainAndImagery/README.md).

Load a DEM and explore terrain relief. No imagery, vector layers or 3D models are
loaded, including files next to the selected DEM.

## Features

- Georeferenced DEM loading with a background CPU worker and cancellation.
- Mesh sample limit: 256, 512 or 1024 per side (reload to apply).
- Live Terrain, Grayscale, Viridis, Inferno and Ocean color ramps.
- Matching low-to-high color legend.
- Vertical exaggeration from 0.25 to 10, without changing source elevations.
- Camera pan, zoom, orbit, look and reset.
- Automatic sample download, extraction, cache reuse and loading.

Color ramps interpolate five stops across the loaded mesh's **local ENU height
range**, before vertical exaggeration. This is relative relief, not an absolute
elevation legend. A flat surface uses the ramp midpoint. Terrain shading
modulates the displayed colors. Viridis and Inferno use five-stop approximations.

## Build

Windows x64, MSVC, CMake, Ninja, Qt 6.11.0 (msvc2022_64), Vulkan driver and a
GeoKernel SDK containing the new Viewer3D `TerrainColorRamp` API are required.
The standard AcquireGeoKernelSdk helper fetches the latest public SDK when
GEOKERNEL_ROOT is omitted. If that package lacks Viewer3D or the color-ramp API,
configuration reports the requirement; use a newer 3D-enabled package.

From an x64 Native Tools command prompt in this directory:

```bat
cmake --preset windows-msvc -DGEOKERNEL_ROOT=C:/path/to/sdk -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build --preset windows-msvc-release
```

Executable: `outputs/windows/Release/bin/TerrainLoading.exe`.

## Use

Launch TerrainLoading with no arguments. On first launch, it downloads
[sagrada_familia_terrain.zip](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_terrain.zip),
extracts the DEM and opens it automatically. A progress dialog allows download cancellation.
A failed download can be retried with **Reload sample**.

Like the other Qt examples, data is cached beside the executable:
`data/sagrada_familia_terrain/sagrada_familia_terrain.tif`.
Subsequent launches use this copy without a network request. The executable's
folder must be writable. Windows uses `tar` to extract the ZIP; Linux uses `unzip`.

Change the color scale or vertical exaggeration immediately. To change mesh
sampling, choose 256/512/1024 and click **Reload sample**; this reuses the cached DEM.
Use **Reset view** to frame the surface.

The published package does not establish a verified vertical datum. The example
therefore loads it as an unknown-datum preview, without silently assuming
ellipsoidal heights or applying a geoid correction.

Left drag: pan. Wheel/right drag: zoom. Middle or Ctrl+left drag: orbit.
Shift+left drag: look.

## SDK integration

`loadPreparedScene` prepares the DEM on a worker. The GUI thread commits it
with `ViewerController::setScene`. Imagery, roads and building paths are cleared.
`setTerrainColorRamp` changes shader rendering without regenerating geometry.
`setHeightScale` changes visual exaggeration only.

Cancellation or preparation failure retains the previous scene. Closing cancels
and joins the worker before the viewer is destroyed; a native raster read may
need to finish before cancellation is observed.

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
