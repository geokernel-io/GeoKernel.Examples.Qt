# SAM 3 segmentation inference

Interactive native Qt example using GeoKernel SDK 1.5.38 or newer. Models and
sample data are prepared automatically. Inference runs only after Generate draft.

## Build

From a Visual Studio x64 developer terminal:

```bat
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 -DGEOKERNEL_ROOT= -UGeoKernel_DIR -DGEOKERNEL_SDK_VERSION=1.5.38
cmake --build --preset windows-msvc-release
```

An empty GEOKERNEL_ROOT downloads and SHA-256 verifies the public SDK. Alternatively,
set GEOKERNEL_ROOT to an extracted SDK directory. The executable is
outputs/windows/Release/bin/Sam3SegmentationInference.exe.

## Models and data

Models come from Cloudflare R2 at
https://pub-00d214f25f754884b22349c19ac37bbf.r2.dev/geokernel-models/:

- meta-sam3-text-fp32-geokernel-onnx-v1.zip
- meta-sam3-tracker-fp32-geokernel-onnx-v1.zip

The RGB sample buildings.zip comes from GeoKernel.SampleData release v1 on GitHub.
The checked-in sam3-assets.json pins archive SHA-256, exact ZIP entries, installed
file hashes and model revision. Models use the specialized sam3-package.json
contract. Runtime integrity/path checks and cancellation are retained.

Upstream model: facebook/sam3, revision 3c879f39826c281e95690f02c7821c4de09afae7.
Both profiles include the original SAM License and attribution; model licensing
is separate from the GeoKernel SDK license.

## Use

- Text segmentation: enter a category such as building, then Generate draft.
  The source is processed in 512px tiles with 96px overlap and up to 200 instances
  per tile; the default score threshold is 0.50.
- Point / box segmentation: prepare the tracker profile, clear old prompts, then
  click a positive/negative point or two opposite corners around one object.
  Green/red points and a yellow box show the prompts. Click Generate draft.
  A box describes an object; it is not an area to scan for separate buildings.
- Accept draft enables saving GeoTIFF + GeoPackage to a new folder. Discard draft
  removes the session result and enables another run. No prediction is restored
  automatically at startup.

Downloads, hashing, inference and GIS output run in a worker. Cancel is cooperative;
a model-loading/backend call may need to finish before cancellation takes effect.
The overall inference/GIS deadline is 30 minutes. Point/box mode supports up to
16 million source pixels. Output geometries preserve the source CRS.

## CUDA runtime

CUDA is selected by default when the loaded runtime advertises it. A CPU-only
runtime cannot provide CUDA merely by selecting it in the UI.

Set GEOKERNEL_ONNXRUNTIME_GPU_ROOT to the native GPU runtime directory matching
the SDK ONNX Runtime API. CMake copies its onnxruntime.dll,
onnxruntime_providers_shared.dll and onnxruntime_providers_cuda.dll after the
standard SDK runtime, preserving the override on later builds. CUDA 12 runtime,
cuBLAS, cuFFT and cuDNN 9 DLLs must also be available to the process. Keep the
runtime redistribution licenses with a deployment. Python is not used at runtime.

GEOS-enabled GDAL is required for polygon topology; the published SDK 1.5.38
includes gdal.dll, geos_c.dll and geos.dll. Sam3Overlay reads the actual GeoPackage
CRS and creates an in-memory preview layer.

The executable contains only the interactive application. Command-line test modes
and the standalone tests directory have been removed.
