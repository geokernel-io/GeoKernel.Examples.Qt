# ModelPlacement

**Example 06:** place a textured glTF landmark on terrain after Buildings3D.

The example automatically downloads the Sagrada Família model, DEM, orthophoto
and official ICGC EGM08D595 geoid grid. Completed packages are cached under
`data` next to the executable. No file chooser is needed. The viewport stays hidden behind a loading page until the real scene and model are installed; synthetic terrain is never shown during startup.

## Controls

- Longitude and latitude: WGS84 degrees.
- Above terrain: metre offset above the DEM at the model anchor, not per-vertex draping.
- Heading, pitch and roll: degrees; heading is clockwise.
- Scale: dimensionless, default approximately 0.958139.
- Apply placement / Reload: prepare and install the new placement.
- Focus model and Reset scene view.
- Show model, Show orthophoto and vertical exaggeration (0.25–10×).

The default location and orientation reproduce an approximate demo fit, not a
surveyed placement. Changing the controls requires Apply; visibility and
exaggeration apply immediately. Preparation runs on a worker and can be cancelled.
Failed preparation preserves the current scene. Source files are not modified.

## Data and height reference

- [Model](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_3d_model.zip)
- [Terrain](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_terrain.zip)
- [Orthophoto](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_ortophoto.zip)
- [ICGC regional geoid](https://icgc-web-pro.s3.eu-central-1.amazonaws.com/produccio/s3fs-public/EGM08D595_19839.zip)

The published terrain samples were compared with the prepared orthometric DEM
and match exactly. EGM08D595 correction is therefore required for placement:
`h = H + N`. The official grid is SHA-256 checked, then written as an ASCII
raster with ETRS89 geographic CRS, preserving its samples and north-to-south row
order. No executables supplied in the geoid archive are run.
This is not an epoch-specific ETRS89/WGS84 survey transformation.

Model source: **PeeJaa, La Sagrada Familia**. The archive's `license.txt` records
Sketchfab Standard licensing and the original model URL. The original license
and README are retained in the cache; this example does not grant additional
rights to the model.

## Geometry handling

The model exceeds the SDK's 4 MiB limit per placement operation. Triangle-aligned
chunks are placed with the same transform and merged, retaining their shared
material and texture. If a chunk contains a triangle that collapses at scene
precision, it is subdivided to identify that triangle. Only those collapsed
triangles are skipped and their count is shown. Other placement errors remain
errors. At default placement, 142,883 triangles render and two are skipped.
This example is specialized to the supplied static model; it is not a generic
glTF metadata or animation editor.

## Build

Requires Qt, a Vulkan-capable GPU and a GeoKernel SDK with Viewer3D.
From an x64 Native Tools command prompt:

```bat
cmake --preset windows-msvc -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build --preset windows-msvc-release
```

Optionally add `-DGEOKERNEL_ROOT=C:/path/to/sdk`; otherwise the standard helper
downloads the latest SDK. Output: `outputs/windows/Release/bin/ModelPlacement.exe`.
The executable folder must be writable. ZIP extraction uses tar on Windows or
unzip on Linux. Windows Release is tested; Linux has not been validated.

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
