# SEN2SRLite super-resolution

Standalone Qt/C++ example with automatic SDK/model/sample acquisition, Run/Cancel, synchronized before/after comparison and explicit GeoTIFF export. Uses the full SEN2SRLite RGBN x4 model including the original Fourier radiometric constraint. No Python or external GDAL installation required at runtime.

## Build and run

In an x64 Visual Studio developer terminal:

```bat
cd /d D:\projects\GeoKernel.Examples.Qt\Sen2srSuperResolution
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 -DGEOKERNEL_ROOT=
cmake --build --preset windows-msvc-release
outputs\windows\Release\bin\Sen2srSuperResolution.exe
```

Empty GEOKERNEL_ROOT acquires the SDK via CMake; set it to an extracted SDK root for local use. Validated with SDK 1.5.36 / Qt 6.11.0 / Windows MSVC x64.

## Workflow

Startup downloads and SHA-256 verifies the fixed model from R2 and the existing upstream FTW raster. Only the original RGB preview appears. Click Run super-resolution to create the AI result on a worker thread; Cancel stops without retaining partial output. Results are session-only and never reloaded from previous runs. Model/source caches remain beside the executable under data/.

The before/after slider reveals the AI image on the right; move it RIGHT to show more AI coverage. At the left endpoint only the original is shown; at the right endpoint only the AI image is shown. Both images use the same viewport/extent and the same source-derived color stretch. Pan, zoom and zoom rectangle operate on the shared map position. The slider clips cached map images and does not rerun inference or map rendering. Display stretch/clipping affects previews only.

Save GeoTIFF opens a file dialog and writes four Float32 surface-reflectance bands (B4 Red, B3 Green, B2 Blue, B8 NIR), source CRS, unchanged geographic extent, fourfold dimensions and 2.5m pixel size. Band 4 is NIR, NOT alpha. AI_GENERATED and model/provenance metadata are included. Values remain unclipped, preserving the model's Fourier-corrected output including possible small negative or above-one values.

## Input and processing

The fixed eight-band FTW sample is 578 x 379 pixels at 10m, EPSG:32633; only its first RGB+NIR period (bands 1..4) is used. No second date is required for SEN2SRLite. Raw finite nonnegative L2A DN is passed to the graph, which embeds division by10000. No double normalization. This is not the L1C raster from DynamicWorld/Prithvi or an arbitrary aerial RGB image. Clouds/shadows are not identified; other scenes need external validity handling.

The bundled Sen2srRasterAdapter uses AiModelPackage + AiSession because the SDK1.5.36 generic raster executor assumes equal input/output tile size. It retains the full128->512 ONNX pipeline including spectral hard constraint, with 16px input halo on each side,96px retained core,reflect scene edges and64px output halo crop. The fixed scene uses28tiles and produces2312 x1516 pixels. All pixel-vector affine coefficients divide by4; origin/extent/CRS stay unchanged. The adapter is included in the model package and copied here for a standalone source example.

The viewer uses a shared QPainter comparison canvas over the native GisViewer navigation layer. Both snapshots come from GisViewer.renderToImage with the exact same requested extent and viewport; navigation updates the paired cached images together. No independent per-side auto-stretch is used.

## Verification

```bat
set QT_QPA_PLATFORM=offscreen
```

Explicit smoke mode writes a scientific GeoTIFF and before/after comparison PNGs. It runs the real downloaded assets, checks visible original/AI differences, actual comparison slider endpoints, synchronized zoom and cancellation. Normal use writes predictions only after Save GeoTIFF. Numerical/native execution validation is not a guarantee of true2.5m sensor detail or improved downstream analysis. CPU validated; CUDA/DirectML may fall back for DFT and need hardware validation.

## Published SDK1.5.39

Requires GeoKernel1.5.39 or newer. An empty GEOKERNEL_ROOT downloads the latest public SDK and verifies the release SHA-256. No SDK from GeoKernel/outputs/sam3-sdk is required.

To replace an older local SDK cached in CMake, configure with:

```bat
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 -DGEOKERNEL_ROOT= -UGeoKernel_DIR -DGEOKERNEL_SDK_VERSION=1.5.39
cmake --build --preset windows-msvc-release
```

The explicit version above reproduces the validated release; omit GEOKERNEL_SDK_VERSION on a fresh build to follow latest. SDK downloads and model downloads are separate caches.

The executable contains only the interactive application workflow; command-line test/verification branches have been removed. Model/data preparation runs automatically at startup.
