# PRUE field boundaries

Standalone Qt/C++ example using the GeoKernel SDK and FTW PRUE EfficientNet-B5 CCBY ONNX package. Follows the existing example conventions: automatic SDK acquisition, pinned model/data downloads with SHA-256 verification, map toolbar, session-only predictions, explicit Shapefile export and result transparency.

## Build and run

In an x64 Visual Studio developer terminal:

```bat
cd /d D:\projects\GeoKernel.Examples.Qt\PrueFieldBoundaries
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 -DGEOKERNEL_ROOT=
cmake --build --preset windows-msvc-release
outputs\windows\Release\bin\PrueFieldBoundaries.exe
```

An empty GEOKERNEL_ROOT uses automatic SDK acquisition. Set an extracted SDK root to use a local installation. Release validated with SDK 1.5.36 and Qt 6.11.0 on Windows x64.

## Use

At startup the app prepares/caches model and source raster and shows RGB only. Click Run field boundary analysis to infer on a worker thread. Provider selection and Cancel are available. Green indicates field interiors, yellow field boundaries; background remains transparent. Move the transparency slider RIGHT for a more visible result. Save Shapefile opens a destination dialog and exports field polygons with class_id=1, class_name and source CRS. Results are not permanently saved or loaded at startup. Only source/model caches and a derived display RGB remain; temporary predictions are released with the session.

The sample ZIP field_analysis.zip was not available when this example was created. The working sample source is therefore the original TIFF from the pinned FTW GitHub repository revision. Download paths are fixed and hidden from the UI. See DemoAssets.h for exact URLs/hashes.

## Model contract

Input: source 578 x 379 GeoTIFF, eight UInt16 Sentinel-2 bands containing two RGB+NIR periods. All eight bands are read as raw Float32 DN. The model graph performs DN/3000; manifest normalization is identity. Do not apply the normalization twice. The packaged model documents canonical window B RGBN followed by window A RGBN; the original example TIFF is passed in its existing upstream order. Training swaps whole four-band temporal windows. This example does not invent a second observation by duplicating a single date.

AiRasterInferenceExecutor uses the model manifest input/output names, eight-band adapter, tile 256 with overlap 64, and softmax decoder. Native source resolution/CRS are preserved. Invalid/nodata/negative/nonfinite input samples are masked separately as -1, never confused with valid background class 0. Output classes: 0 background, 1 field interior, 2 field boundary. Only class 1 is polygonized; boundary pixels keep adjoining fields apart. No cadastral or ownership boundary claim is made. Cloud/shadow detection and accuracy assessment are outside this demonstration.

GDAL C ABI from the SDK runtime handles raw band reads, Byte RGBA previews and explicit polygon export, without external GDAL headers/libraries or Python at runtime.

## Verification

```bat
set QT_QPA_PLATFORM=offscreen
```

These explicit verification modes write test rasters/GeoJSON/PNGs. Normal interactive use does not save predictions. Smoke tests check real inference, RGB/result rendering, fully transparent and half-opacity rendering, cancellation without partial output and optional Shapefile export. Numerical/execution checks are not an independent accuracy benchmark. CPU validated; GPU providers require separate hardware validation.

## Published SDK1.5.37

Requires GeoKernel1.5.37 or newer. An empty GEOKERNEL_ROOT downloads the latest public SDK and verifies the release SHA-256. No SDK from GeoKernel/outputs/sam3-sdk is required.

To replace an older local SDK cached in CMake, configure with:

```bat
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 -DGEOKERNEL_ROOT= -UGeoKernel_DIR -DGEOKERNEL_SDK_VERSION=1.5.37
cmake --build --preset windows-msvc-release
```

The explicit version above reproduces the validated release; omit GEOKERNEL_SDK_VERSION on a fresh build to follow latest. SDK downloads and model downloads are separate caches.

The executable contains only the interactive application workflow; command-line test/verification branches have been removed. Model/data preparation runs automatically at startup.
