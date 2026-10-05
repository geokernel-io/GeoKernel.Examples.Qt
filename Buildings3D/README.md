# Buildings3D

**Example 05:** follows RoadsOnTerrain.

Extrude building footprints on terrain with an orthophoto background. This focused
example loads buildings, DEM and imagery automatically.

## Features

- Automatic sample download, extraction and loading; no file selection required.
- Terrain-clamped building footprints with adjustable default height (1–300 m).
- Building visibility, color and opacity controls.
- Orthophoto visibility and vertical exaggeration (0.25–10×, default 1×).
- Camera navigation and Reset view.

The supplied footprints have **no measured height attributes**. The default 9 m
height is illustrative. Change **Default building height**, then click
**Apply height / Reload sample** to rebuild the buildings from cached data.
The sample DEM vertical datum is unverified; no vertical datum conversion is applied.

## Build

Requires Qt, a Vulkan-capable driver and a 3D-enabled GeoKernel SDK.
From an x64 Native Tools command prompt:

```bat
cmake --preset windows-msvc -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build --preset windows-msvc-release
```

Add `-DGEOKERNEL_ROOT=C:/path/to/sdk` to use an extracted SDK. Otherwise the
standard acquisition helper downloads the latest public SDK package. It must
contain GeoKernel::Viewer3D. Windows Release was validated; Linux was not tested.

Executable: `outputs/windows/Release/bin/Buildings3D.exe`.

## Sample data

Launch without arguments. The following packages download automatically:

- [Terrain](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_terrain.zip)
- [Orthophoto](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_ortophoto.zip)
- [Buildings](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_buildings.zip)

Each package is cached in a separate folder under `data` beside the executable.
Subsequent launches reuse the files. Shapefile validation checks SHP, SHX, DBF
and PRJ; keep the supplied CPG encoding file with them too.
The executable folder must be writable. Extraction uses tar on Windows and
unzip on Linux. Downloads and CPU scene preparation can be cancelled; completed
packages remain cached for retry. A failed reload preserves the previous scene.

## Controls

- **Default building height / Apply height:** rebuild using the chosen illustrative height.
- **Show buildings / Building color / Building opacity:** apply appearance immediately.
  Opacity uses the SDK's screen-door coverage.
- **Show orthophoto:** toggle the terrain imagery.
- **Vertical exaggeration:** scale displayed scene heights without changing source data.
- **Reset view:** frame the scene.

Left drag pans; wheel/right drag zooms; middle or Ctrl+left drag orbits;
Shift+left drag looks.

## SDK integration

SceneOptions selects the building shapefile, default height and ClampToTerrain
placement, ignoring source Z. Road loading is disabled. loadPreparedScene runs
on a worker; setScene commits on the GUI thread. Appearance uses
setLayerAppearance and setLayerColor; exaggeration uses setHeightScale.

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
