# CameraNavigation

**Example 03:** after TerrainLoading and TerrainAndImagery.

Learn the Viewer3D camera API using a fixed Sagrada FamÃ­lia terrain and orthophoto
as a backdrop. No file selection is required.

## Features

- Overview, top (85-degree pitch), east, west and close oblique presets.
- Smooth 1.2-second camera transitions, or immediate changes when disabled.
- Shortest-path heading interpolation.
- Zoom in/out, north up and reset to the initial overview.
- Named saved views: save the current camera, restore it, or delete it.
- Up to 20 views persisted in Qt QSettings for this application and dataset.
- Mouse press, wheel, touch begin or key input in the viewport interrupts a transition.
- Live heading and tilt readout.
- Vertical exaggeration: 0.25×–10× (default 1×), applied without reloading. Changing it stops the current camera transition. The value is retained on sample reload; camera bookmarks do not override it.

Saved views store camera state only; they do not change the source DEM, imagery
or height reference. Camera positions/distances are SDK render-space values, not
geographic coordinates. This example does not implement longitude/latitude entry.
Terrain collision/clearance constraints remain active.

## Build

Uses the standard example CMake presets, SDK acquisition helper, resources and
runtime deployment. Requires Qt, a Vulkan-capable driver and a 3D-enabled GeoKernel SDK.

From an x64 Native Tools command prompt:

```bat
cmake --preset windows-msvc -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build --preset windows-msvc-release
```

Add `-DGEOKERNEL_ROOT=C:/path/to/sdk` to use an extracted 3D-enabled SDK.
Otherwise the helper acquires the latest public SDK. Configuration reports a
missing Viewer3D target. Windows Release was tested; Linux presets are supplied
in the repository's standard format but not validated here.

Executable: `outputs/windows/Release/bin/CameraNavigation.exe`.

## Sample data and startup

On first launch the application downloads these packages and loads them automatically:

- [Terrain](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_terrain.zip)
- [Orthophoto](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_ortophoto.zip)

Extracted files are cached in `data` next to the executable. Later launches work
from this cache without a download. The folder must be writable. ZIP extraction
requires tar on Windows or unzip on Linux. Downloads can be cancelled; use
**Reload sample** to retry.

The sample DEM uses an unknown-datum preview. No geoid correction is applied.

## Controls

Choose a preset, then click **Go to preset**. **Stop transition** freezes the
current camera. **Reset view** returns to the overview using the chosen transition mode.
Saving a view during a transition stops it and records the actual current camera.
Saved views are specific to this fixed Sagrada sample.

Mouse: left drag pans, wheel/right drag zooms, middle/Ctrl+left drag orbits,
Shift+left drag looks.

## Implementation

`CameraPanel.h` uses public `ViewerController::camera/setCamera`, the validated
camera JSON helpers and QVariantAnimation. Main loads the sample on a CPU worker
and commits it on the GUI thread, following TerrainAndImagery.

## Validation

`--smoke-report C:/temp/camera.json` runs without activating the window on the
last monitor. It checks a preset transition, saved-view restore and deletion,
persistence through a newly constructed panel, and interruption by viewport input.
Settings are redirected to a temporary INI directory so tests do not modify user
bookmarks. A viewport PNG and JSON result are saved beside the report.

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
