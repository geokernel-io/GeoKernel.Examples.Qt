# HOTOSM DINOv3-S building footprints

Standalone Qt / C++ example. CMake acquires the SDK; the application downloads the model and a single sample raster, `buildings.tif`, from the SampleData `buildings.zip` release asset. Archives are SHA-256 verified and cached. No Python or GDAL development installation is required.

## Build and run

In an x64 Visual Studio developer terminal:

```bat
cd /d D:\projects\GeoKernel.Examples.Qt\HotosmBuildingFootprints
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 -DGEOKERNEL_ROOT=
cmake --build --preset windows-msvc-release
outputs\windows\Release\bin\HotosmBuildingFootprints.exe
```

With no GEOKERNEL_ROOT, the acquisition script downloads the latest GitHub SDK.
To reuse an extracted SDK, pass `-DGEOKERNEL_ROOT=<sdk-directory>` instead.
Tested: Windows x64, SDK 1.5.39, Qt 6.11.0, Release. Linux presets are supplied
but were not tested in this session. Qt and C++ build tools must be installed.

## Workflow

1. Launch the application; the model and `buildings.tif` are prepared automatically. There is no separate download button. Run retries preparation if required assets are missing before starting inference.
2. Choose an execution provider. The building threshold defaults to **0.50**.
3. Click **Run building segmentation inference**. Building polygons appear on the raster.
4. Zoom and pan to inspect the displayed polygons. Inference always processes the entire raster at native resolution.

Dataset navigation has been removed. Inference runs on a worker thread with cancellation. The original raster is preserved. Results are displayed only for the current session after clicking Run. Startup displays the original raster only and never loads earlier predictions. The progress bar shows Ready until Run starts inference; map drawing progress does not mark inference complete. Temporary GeoJSON/RGB intermediates are automatically removed after the overlay is materialized in memory, including on cancellation or failure. No persistent result or label TIFF is written by the interactive demo.

The demo uses the mask, boundary and distance outputs for seeded separation and filters regions smaller than 28 pixels. The threshold overrides the model manifest's foreground threshold. Predictions can miss buildings, merge adjacent roofs or include false positives; polygon count is not an accuracy measure.

## Assets and licensing

Raster: https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/buildings.zip

Model: https://pub-00d214f25f754884b22349c19ac37bbf.r2.dev/geokernel-models/hotosm-dinov3s-buildings-geokernel-onnx-v1.zip

The raster archive does not provide verified source/license provenance. Model credits: HOTOSM and Meta DINOv3; retain the package's CC-BY-4.0, separate DINOv3 License and attribution documents. SDK licensing is separate.

## Processing behavior


The inference-resolution selector is removed. The interactive example processes all pixels of `buildings.tif` at its original 2503 x 1126 resolution without resampling. Map zoom does not affect inference.

The interactive UI has no model-package, input-raster or model-download fields. It always uses the fixed model download URL and cached `buildings.tif`.

## Save Shapefile

After successful inference, **Save Shapefile** opens a save-file dialog. Choose a `.shp` destination; the export includes `.shp`, `.shx`, `.dbf`, `.prj` and UTF-8 encoding metadata. Polygon geometry, instance IDs and the source CRS are preserved. Results remain session-only until explicitly exported; the button is disabled before inference and while a new inference is running.

## Published SDK1.5.39

Requires GeoKernel1.5.39 or newer. An empty GEOKERNEL_ROOT downloads the latest public SDK and verifies the release SHA-256. No SDK from GeoKernel/outputs/sam3-sdk is required.

To replace an older local SDK cached in CMake, configure with:

```bat
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 -DGEOKERNEL_ROOT= -UGeoKernel_DIR -DGEOKERNEL_SDK_VERSION=1.5.39
cmake --build --preset windows-msvc-release
```

The explicit version above reproduces the validated release; omit GEOKERNEL_SDK_VERSION on a fresh build to follow latest. SDK downloads and model downloads are separate caches.

The executable contains only the interactive application workflow; command-line test/verification branches have been removed. Model/data preparation runs automatically at startup.
