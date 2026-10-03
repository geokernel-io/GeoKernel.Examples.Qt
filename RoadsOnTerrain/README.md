# RoadsOnTerrain

**Example 04:** after TerrainLoading, TerrainAndImagery and CameraNavigation.

Display vector roads on a terrain surface with optional orthophoto visibility.
This example focuses on road draping and appearance; it does not load buildings.

## Features

- Automatic download and opening of the Sagrada Família DEM, orthophoto and roads.
- Roads clamped to terrain, ignoring source Z coordinates.
- Road visibility, color picker and opacity controls.
- Orthophoto visibility toggle.
- Vertical exaggeration from 0.25× to 10×, default 1×.
- Standard camera controls and Reset view.

Road styling and exaggeration apply without reloading. The sample's vertical
datum is not verified; it is displayed as a preview without a geoid conversion.

## Build

Uses the standard example CMake presets, SDK acquisition helper, application
resources and runtime deployment. Requires a Vulkan-capable driver, Qt and a
3D-enabled GeoKernel SDK.

From an x64 Native Tools command prompt:

```bat
cmake --preset windows-msvc -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build --preset windows-msvc-release
```

Add `-DGEOKERNEL_ROOT=C:/path/to/sdk` to use an extracted SDK. Otherwise the helper
downloads the latest public package. It must contain GeoKernel::Viewer3D.
Windows Release was tested; Linux presets follow the repository convention but
have not been validated for this example.

Executable: `outputs/windows/Release/bin/RoadsOnTerrain.exe`.

## Sample data

Launch without arguments. These packages are downloaded and extracted automatically:

- [Terrain](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_terrain.zip)
- [Orthophoto](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_ortophoto.zip)
- [Roads](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_roads.zip)

The application caches each package in its own folder under `data` next to the
executable. Later launches reuse the local files without downloading.
Shapefile cache validation checks SHP, SHX, DBF and PRJ. Keep all companion files,
including the supplied CPG encoding file, together.

The executable folder must be writable. Extraction uses tar on Windows and
unzip on Linux. Downloads can be cancelled; **Reload sample** retries using any
already completed packages. CPU loading can also be cancelled.

## Controls

- **Show roads:** display/hide the road layer.
- **Road color:** choose an opaque base color.
- **Road opacity:** change coverage from 0% to 100%; the SDK uses screen-door coverage.
- **Show orthophoto:** compare roads with the bare terrain.
- **Vertical exaggeration:** visually scale terrain and draped roads; source data is unchanged.
- **Reset view:** frame the scene.

Left drag pans; wheel/right drag zooms; middle or Ctrl+left drag orbits;
Shift+left drag looks.

## SDK integration

SceneOptions selects the road shapefile, sets ClampToTerrain and disables
source Z and building discovery. CPU scene preparation runs on a worker.
GUI-thread setScene commits the result. Road styling uses setLayerAppearance
and setLayerColor. Pending load failure/cancellation preserves the previous scene.

## Validation

`--smoke-report C:/temp/roads.json` runs without activating the window on the
last monitor. It checks nonempty road geometry, absence of buildings, a changed
frame when roads are hidden, appearance changes and exaggeration.
PNG/JSON results are written beside the report. These checks do not establish
survey-grade alignment.

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
