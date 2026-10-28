# Prithvi flood analysis

Standalone Qt/C++ example using the GeoKernel SDK and the pinned IBM/NASA Prithvi EO 2.0 300M-TL / Sen1Floods11 ONNX package. No Python, TerraTorch, external GDAL installation or external GDAL headers are required at runtime.

## Build

Run in an x64 Visual Studio developer terminal:

```bat
cd /d D:\projects\GeoKernel.Examples.Qt\PrithviFloodAnalysis
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 -DGEOKERNEL_ROOT=
cmake --build --preset windows-msvc-release
outputs\windows\Release\bin\PrithviFloodAnalysis.exe
```

An empty GEOKERNEL_ROOT acquires the SDK through the shared CMake pattern. Set it to an extracted SDK root to use a local SDK. Release validated against SDK1.5.36 / Qt6.11.0 / MSVC x64.

## Workflow

The app downloads and SHA-256 verifies the fixed R2 model (about1.19GB) and GitHub flood_analysis.zip. Prepared assets are cached beside the executable under data/. The display is generated from B4/B3/B2 with a2-98percent per-channel stretch, preserving the original13-band raster.

Startup shows only the RGB sample. Run flood analysis performs native AiSession inference on a worker thread. Provider selection, cancellation, zoom/pan and a result-transparency slider are available. Moving the slider RIGHT makes the result MORE visible. The slider is disabled until a result exists. Save Shapefile opens a destination dialog and exports water polygons with class_id=1, class_name and source CRS. Predictions live only for the current session; temporary inference files are removed when released. No saved predictions are reloaded. Source/model caches remain.

## Input and model contract

The fixed input is India_900498_S2Hand.tif,512x512,13Sentinel-2 L1C bands, WGS84. One-based bands2,3,4,9,12,13 are selected as B2/B3/B4/B8A/B11/B12. Raw DN is passed unchanged; DN0.0001 scaling and Sen1Floods11 standardization are embedded in the model.

The ONNX model also requires location_coords[1,2]. Every tile center is transformed from the raster CRS to WGS84 and passed as latitude,longitude, using traditional GIS axis order explicitly during transformation. No location is hardcoded. This package is an undated single-timestamp profile with no temporal encoding.

Native-resolution224px windows cover the512px raster using starts0,192,288 on both axes. Overlapping logits are averaged before argmax; no scene downsampling. Both input tensors are supplied through AiSession because the single-input raster convenience executor cannot populate geographic metadata. Invalid/nodata/negative input pixels are masked as-1 after inference, independent of valid non-water class0. Only class1 is colored and polygonized. Clouds/shadows are not automatically identified. The model maps surface water, including permanent water; it does not prove which pixels became flooded during an event.

GDAL is accessed through the stable C ABI of the SDK runtime to read raw bands, create a Byte RGBA preview, transform coordinates and export polygons. No third-party build dependencies are introduced.

## Verification

Explicit offscreen verification (only these test modes write test rasters/GeoJSON/PNGs):

```bat
set QT_QPA_PLATFORM=offscreen
```

Smoke verification runs the real downloaded model/data, checks source/result rendering, fully transparent and half-opacity rendering, cancellation after a tile, and optional Shapefile export. See DATA-SOURCES.md for source terms. Execution/parity checks do not constitute an independent flood-accuracy benchmark. CPU verified; GPU providers require separate hardware validation.

## Published SDK1.5.39

Requires GeoKernel1.5.39 or newer. An empty GEOKERNEL_ROOT downloads the latest public SDK and verifies the release SHA-256. No SDK from GeoKernel/outputs/sam3-sdk is required.

To replace an older local SDK cached in CMake, configure with:

```bat
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 -DGEOKERNEL_ROOT= -UGeoKernel_DIR -DGEOKERNEL_SDK_VERSION=1.5.39
cmake --build --preset windows-msvc-release
```

The explicit version above reproduces the validated release; omit GEOKERNEL_SDK_VERSION on a fresh build to follow latest. SDK downloads and model downloads are separate caches.

The executable contains only the interactive application workflow; command-line test/verification branches have been removed. Model/data preparation runs automatically at startup.
