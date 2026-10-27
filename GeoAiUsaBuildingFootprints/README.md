# GeoAI USA Mask R-CNN building footprints

Standalone Qt / C++ example using the GeoKernel SDK. Downloads the fixed GeoAI USA RGB Mask R-CNN package from Cloudflare and the same `buildings.tif` as the HOTOSM example from GitHub. No Python, external GDAL installation or model training is required at runtime.

## Build and run

In an x64 Visual Studio developer terminal:

```bat
cd /d D:\projects\GeoKernel.Examples.Qt\GeoAiUsaBuildingFootprints
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 -DGEOKERNEL_ROOT=
cmake --build --preset windows-msvc-release
outputs\windows\Release\bin\GeoAiUsaBuildingFootprints.exe
```

With an empty GEOKERNEL_ROOT, CMake acquires the latest SDK using the shared example conventions. To use an extracted SDK, pass `-DGEOKERNEL_ROOT=<sdk-directory>`. Tested with SDK 1.5.36 and Qt 6.11.0 on Windows x64 Release. Linux presets are supplied but untested.

## Workflow

At startup, model/data ZIPs are downloaded, SHA-256 verified, extracted and cached. The map initially shows the raster only. There are no model-path, raster-path, URL, resolution or dataset-navigation controls and no separate preparation button.

Choose an execution provider (Auto/CPU/CUDA/DirectML), optionally adjust **Building confidence** (default 0.50), and click **Run building segmentation inference**. If assets are missing, Run retries preparation first. Only clicking Run starts inference. Processing runs on a worker thread with cancellation; close is deferred while work is active.

The full raster is processed at its original 2503 x 1126 resolution, independently of map zoom. Building polygons are displayed in red with yellow outlines. Results are session-only; temporary GeoJSON intermediates are removed after loading polygons into memory. No instance-label TIFF is saved and old results are never reloaded at startup.

After successful inference, **Save Shapefile** opens a save dialog. Choose a `.shp` destination; the `.shp`, `.shx`, `.dbf`, `.prj` and `.cpg` sidecars are exported together. CRS and building IDs are retained (DBF field names obey the Shapefile ten-character limit). Export is the only persistent result-saving action in the interactive example.

## Model contract

GeoAI USA `building_footprints_usa.pth`, exported as Mask R-CNN ResNet50 FPN ONNX, package version 1.0.0. Fixed input `[1,3,512,512]`, RGB Float32 pixel/255. ImageNet normalization and torchvision resizing are inside ONNX and must not be applied twice.

Each tile returns associated `boxes[N,4]`, `scores[N]`, `classes[N]` and `masks[N,1,512,512]`. N may be zero. The example uses `AiModelPackage` and `AiSession`, 512px source tiles with 128px overlap and zero padding on small inputs. Actual pixel sampling uses the base-resolution raster. Incomplete edge areas are clipped.

Building confidence filters detection scores. Mask probability is fixed at 0.50. Cross-tile duplicate boxes use class-1 IoU NMS at 0.50. Per-instance masks are kept separate in an internal, in-memory Int32 buffer for polygonization; overlap is assigned to the highest-confidence instance. Regions with fewer than 16 pixels are filtered. This is an example adapter, not a cadastral building delineation algorithm: tile edges may leave duplicate/partial objects, nearby instances may overlap, and irregular/small masks may split into multiple polygon parts. Polygon count can differ from the model instance count. Input size is capped at 64 million pixels.

The checkpoint was trained for USA RGB imagery. No global accuracy or independent accuracy benchmark is claimed. CPU is validated; availability of CUDA/DirectML depends on the SDK runtime and hardware.

## Assets and notices

Model: https://pub-00d214f25f754884b22349c19ac37bbf.r2.dev/geokernel-models/giswqs-geoai-usa-maskrcnn-resnet50-fpn-geokernel-onnx-v1.zip

Raster: https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/buildings.zip

Source model: https://huggingface.co/giswqs/geoai

Model MIT declaration and PyTorch/torchvision notices remain in the downloaded package. See ATTRIBUTION.txt for provenance limitations. The raster archive contains no verified source/license notice. SDK licensing is separate.

## Verification




Verified CPU result on the sample: 21 native-resolution tiles, 746 retained instances, 857 valid polygon parts, about 41 seconds on the local test machine. Overlay rendering and reopened Shapefile CRS/count passed. A uniform 512px raster returned zero instances/polygons without error. These are functional checks, not accuracy metrics or guaranteed timings.

## Published SDK1.5.38

Requires GeoKernel1.5.38 or newer. An empty GEOKERNEL_ROOT downloads the latest public SDK and verifies the release SHA-256. No SDK from GeoKernel/outputs/sam3-sdk is required.

To replace an older local SDK cached in CMake, configure with:

```bat
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 -DGEOKERNEL_ROOT= -UGeoKernel_DIR -DGEOKERNEL_SDK_VERSION=1.5.38
cmake --build --preset windows-msvc-release
```

The explicit version above reproduces the validated release; omit GEOKERNEL_SDK_VERSION on a fresh build to follow latest. SDK downloads and model downloads are separate caches.

The executable contains only the interactive application workflow; command-line test/verification branches have been removed. Model/data preparation runs automatically at startup.

Model and imagery provenance, download hashes and license notes: [DATA-SOURCES.md](DATA-SOURCES.md).
