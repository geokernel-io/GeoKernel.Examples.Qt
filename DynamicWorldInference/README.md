# Dynamic World land-cover classification

Standalone Qt / C++ example following the GeoKernel SDK acquisition, icon, map toolbar, runtime deployment and explicit-export conventions. The SDK is acquired by CMake; model and data archives are downloaded and SHA-256 checked by the app. No Python, TensorFlow installation, external GDAL headers or extra GDAL installation is required at runtime.

## Build and run

In an x64 Visual Studio developer terminal:

```bat
cd /d D:\projects\GeoKernel.Examples.Qt\DynamicWorldInference
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 -DGEOKERNEL_ROOT=
cmake --build --preset windows-msvc-release
outputs\windows\Release\bin\DynamicWorldInference.exe
```

Empty GEOKERNEL_ROOT uses the latest public SDK via the acquisition helper. To reuse an extracted SDK, pass `-DGEOKERNEL_ROOT=<sdk-directory>`. Windows x64 Release was validated with SDK 1.5.36 and Qt 6.11.0. Linux presets are provided but untested.

## Workflow

Startup prepares the fixed model and Spain/Alicante sample automatically, then displays only the RGB raster. Click **Run land-cover classification** to generate the nine-class overlay. Inference runs on a worker thread and can be cancelled. Missing assets are prepared again when Run is clicked. There are no model path, input path, URL, resolution or dataset-selection controls.

The input is the original 512 x 512, 10m, nine-band Sentinel-2 L1C TIFF. The RGB preview is for display only. Provider choices are Auto, CPU, CUDA and DirectML; CPU is validated, other providers depend on the installed runtime/hardware.

No predictions are loaded on startup. Outputs are kept for the current session only. A temporary Byte RGBA TIFF supplies the classification overlay with the exact Dynamic World colors and fixed transparency; it is cleaned up after the map releases it. No result is automatically written to a permanent location.

After inference, **Save Shapefile** opens a save dialog for class polygons. It writes `.shp`, `.shx`, `.dbf`, `.prj` and `.cpg` files, retaining CRS and `class_id` / `class_name` attributes. Pixel-sized regions are retained; this is land-cover polygonization, not building-instance extraction.

## Model/data contract

- Model ID `google-dynamicworld`, ONNX package 1.0.0, pinned source revision `0dd288fae5fb5291d988e5ea3c12eb91df481ec5`.
- Input bands: **B2, B3, B4, B5, B6, B7, B8, B11, B12**, in that order, aligned on a 10m grid.
- Input values: raw L1C TOA reflectance scaled by 10000 (DN). The graph embeds the upstream nonlinear log/percentile/sigmoid normalization; manifest normalization is identity. Do not normalize or divide the input a second time.
- SDK `AiRasterInferenceExecutor` uses 256px tiles with 64px overlap, softmax on channel axis 1 and ClassMask output. The entire raster is processed at source resolution, independently of map zoom.
- Classes 0..8: water, trees, grass, flooded vegetation, crops, shrub/scrub, built, bare, snow/ice.
- **Class 0 is valid water.** The supplied nodata validity mask is applied after inference; invalid pixels are -1, transparent in the preview and excluded from polygons. It is not a cloud/shadow mask. Cloud/shadow handling remains external.
- RGB data never enters the model. Mask dimensions, CRS and affine grid must match the nine-band source.

## Downloads and attribution

Model: https://pub-00d214f25f754884b22349c19ac37bbf.r2.dev/geokernel-models/google-dynamicworld-geokernel-onnx-v1.zip

Model ZIP SHA-256: `326bb2899cdea6e620a02f5e16fd330ec848e0a5b76f72f576038d7bc1e0aa63`.

Dataset: https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/spain_sentinel2_l1c.zip

Dataset ZIP SHA-256: `07811e9600882c526185268c5b280134e5715c95042754e414d6736fa5647839`.

Model: Dynamic World Project, Google in partnership with National Geographic and the World Resources Institute. Retain Apache-2.0 LICENSE, NOTICE, attribution and modification notices in the downloaded package. This is the public local forward model, not the complete Earth Engine production/cloud-masking pipeline.

Imagery: **Contains modified Copernicus Sentinel data 2019.** Source Sentinel-2B L1C scene 2019-09-18, processing baseline 02.08. The data archive includes the original legal notice, source metadata and transformation records. No Earth Engine dataset or Sen1Floods11 imagery is used by this example. SDK licensing is separate.

## Verification



The verified 512px sample uses nine inference tiles and takes about two seconds of CPU inference on the local machine. These functional checks are not an independent land-cover accuracy benchmark.

## Published SDK1.5.39

Requires GeoKernel1.5.39 or newer. An empty GEOKERNEL_ROOT downloads the latest public SDK and verifies the release SHA-256. No SDK from GeoKernel/outputs/sam3-sdk is required.

To replace an older local SDK cached in CMake, configure with:

```bat
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 -DGEOKERNEL_ROOT= -UGeoKernel_DIR -DGEOKERNEL_SDK_VERSION=1.5.39
cmake --build --preset windows-msvc-release
```

The explicit version above reproduces the validated release; omit GEOKERNEL_SDK_VERSION on a fresh build to follow latest. SDK downloads and model downloads are separate caches.

License key: define licenseKey immediately after the interactive GisViewer is constructed in main.cpp; setLicenseKey is called before map tools and layers are initialized. The example no longer contains command-line verification or smoke-test execution branches.

Model and imagery provenance, download hashes and license notes: [DATA-SOURCES.md](DATA-SOURCES.md).
