# DSM + orthophoto to DTM

Compare three newly computed terrain estimates on **the same full DSM**:

1. Native classical morphological/TIN filtering, DSM only.
2. Experimental `pix_grad`, DSM only.
3. Experimental `rgb_grad_fm`, DSM plus an aligned RGB orthophoto.

Four linked GeoKernel 2D maps retain input context on the left and show all three
estimates beside it. Switch the input between the real orthophoto and original
DSM relief. Reference terrain is evaluation-only; methods do not feed each other.

## Build and run

From an x64 Visual Studio developer terminal:

```bat
cd /d D:\projects\GeoKernel.Examples.Qt\DsmOrthoToDtm
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build --preset windows-msvc-release
outputs\windows\Release\bin\DsmOrthoToDtm.exe
```

Requires Qt 6 and GeoKernel **1.5.40 or newer**. The unified SDK CMake package also
requires Vulkan SDK discovery, although these maps are 2D and inference uses CPU.
CMake acquires and verifies the latest published SDK by default. An explicit
`-DGEOKERNEL_ROOT=<sdk>` selects an SDK. Windows/MSVC is the validated runtime;
Linux presets are included. No Python, TensorFlow, PyTorch or ArcGIS is needed at
runtime. GeoKernel license/evaluation rules apply.

## Published sample ZIP

The input ZIP is published in the **v1 release** of
[GeoKernel.SampleData](https://github.com/geokernel-io/GeoKernel.SampleData/releases/tag/v1).
The locally prepared copy is:

`outputs/sample-upload/zurich_dsm_ortho_to_dtm_v1.zip`

**33,020,943 bytes**. SHA-256:
`b12db41fc66c96b8c9b46a0e1fb6f3f089516234cad82ba8c4507f29f5d42289`.

The compiled default download URL is:

https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/zurich_dsm_ortho_to_dtm_v1.zip

Normal UI startup was verified on 2026-11-06 with an empty input cache and no
local ZIP arguments. The public archive size/SHA-256 and all eight extracted-file
hashes matched. Evidence is in `outputs/validation/public-sample-download.json`.

Normal startup downloads the published package automatically. For offline/local
use, supply the same pinned archive explicitly:

```bat
outputs\windows\Release\bin\DsmOrthoToDtm.exe --sample-archive outputs\sample-upload\zurich_dsm_ortho_to_dtm_v1.zip
```

[sample-assets.json](sample-assets.json) pins the ZIP and all eight extracted
files. Data are cached beside the executable under `data/zurich-dsm-ortho-to-dtm-v1`.
The ZIP contains **inputs and evaluation reference only**, with no prepared
prediction, AI weights, executable or Python script.

| File | Role |
| --- | --- |
| `dsm.tif` | Original full 1 km surface heights, Float32 metres |
| `ortho_rgb.tif` | Real aligned RGB orthophoto, three Byte bands |
| `reference.tif` | Provider terrain for evaluation only |
| `source-manifest.json` | Source URLs/hashes, dates, grid, alignment method and comparison crop |
| `ATTRIBUTION.txt`, `README.txt` | Source notices and data roles |
| `proj-data/proj.db`, `PROJ-LICENSE.txt` | Matching native projection database and license |

All three rasters share **2000 × 2000 cells, 0.5 m, EPSG:2056** and bounds
`2677000,1251000,2678000,1252000`. Height arrays retain the source metre values.
The real SWISSIMAGE DOP10 orthophoto was bilinearly sampled from its official
0.1 m COG to the exact DSM grid before packaging. The application performs no
implicit input alignment or RGB rescaling.

Source: **Federal Office of Topography swisstopo**, swissSURFACE3D Raster,
swissALTI3D and SWISSIMAGE DOP10. The [OGD terms](https://www.swisstopo.admin.ch/en/terms-of-use-free-geodata-and-geoservices)
permit processing and redistribution with source attribution. Keep the bundled
notices when redistributing. DSM filename year is 2018; reference/RGB nominal year
is 2019. Same-day acquisition and subpixel feature registration are unverified.
Grid alignment alone does not establish temporal or feature correspondence.
Source heights are declared LN02 / EPSG:5728; no vertical datum conversion occurs.

## Workflow

1. Startup verifies/downloads the input ZIP and prepares its original DSM/RGB.
   The bundled PROJ database configures process-local paths when the SDK does not
   include one. Machine-wide environment variables are unchanged.
2. The two model ZIPs are downloaded from their pinned URLs and every package file
   is checked. The model buttons or CLI arguments can supply the same ZIPs
   locally. Model weights are separate from the GitHub data ZIP and executable.
3. Choose AI tile overlap (default 64 px, range 0–128), CPU threads (default 4)
   and the classical preset, then press **Run all three methods**. Each model has
   fixed 256 × 256 tiles; the SDK handles overlap blending and edge padding.
   Processing runs on a worker with phase/tile progress and cancellation.
4. The three estimates share identical hillshade light and contrast. Switch all
   three together to removed heights (fixed 0–20 m scale) or signed reference
   errors (fixed −2 to +2 m scale). Display saturation never clips metrics.
5. The default **Shared comparison crop** is the same 250 × 250 m window as the
   previous Zurich examples: column 200 / row 800, 500 × 500 source cells, bounds
   `2677100,1251350,2677350,1251600`. Dashed boxes mark evaluated cells; outside
   areas are dimmed. **Full input** evaluates the complete valid overlap.
   Changing view/scope never changes the full input used by any method.
6. Pan/zoom is linked by centre and map units per pixel across all four maps.
   Click a pixel to read RGB byte values, DSM, all three estimates and reference.
7. Changing processing controls retains the completed result and its settings,
   marked as needing a rerun. Cancel discards unfinished work and retains the
   previous completed comparison. Closing during work cancels/joins it.
8. **Open aligned DSM + RGB** loads custom source files and clears old reference
   and results. **Open reference** attaches a strictly aligned evaluation surface
   and updates scores/error maps without regenerating any terrain heights.

For custom sources, the comparison crop is the fixed central 60% in each axis.
No crop is selected using reference errors. Scores use common valid source and
reference cells for all methods: RMSE, MAE and signed bias, `estimate - reference`,
accumulated from numeric heights in double precision. Both full/crop scores are
saved regardless of current display. Without reference, conversion still works
and scores are unavailable/null.

## Models and inputs

- `pix_grad`: [David Perez, Zenodo 7973800](https://doi.org/10.5281/zenodo.7973800),
  CC BY 4.0; ONNX SHA-256
  `e400e27f6afc75bcf4e1a61432ee9557529683903996dd953d3e8e3fcbe90eb6`.
- `rgb_grad_fm`: [David Perez, Zenodo 7973886](https://doi.org/10.5281/zenodo.7973886),
  CC BY 4.0; ONNX SHA-256
  `5042d5abac64d6b1764265924c9910807e3a5efee7e1b9517fc9d86bf4d0e3a3`.

See [pix-model-assets.json](pix-model-assets.json) and
[rgb-model-assets.json](rgb-model-assets.json) for package URLs, sizes and hashes.
Both model packages are published on Cloudflare R2. The new RGB package's normal
UI acquisition was verified on 2026-11-06 with a fresh RGB-model cache and no
local model ZIP arguments; archive and all six extracted file hashes matched.
Both packages include license, attribution, manifests, model cards and conversion/
native parity records. Those records verify numerical conversion, not accuracy.

The SDK's `AiRasterInferenceExecutor::requestFromPackage` defines the contracts.
`pix_grad` receives **DSM**; `rgb_grad_fm` receives exactly **DSM, R, G, B**, mapped
from separate aligned rasters. No classical result, terrain reference or
reference-derived class/mask is an input. DSM normalization/inverse scaling and
RGB byte-level normalization are inside each graph. Do not preprocess RGB into
0–1 values or pass the reference as an extra channel.

These are independent deterministic ONNX interoperability adaptations: learned
weights and patch statistics retained, dropout disabled, unused checkpoint
running statistics ignored. They differ from the author's stochastic prediction
path. Training region is reported as Brussels/UrbIS; training GSD is unverified.
The original notebook source is not redistributed. Models remain experimental
example packages outside the generic production model catalog.

For missing DSM input values, source-only four-neighbour support propagation
supplies model context. Original DSM validity is restored afterward. RGB masks
are checked on all bands; RGB must cover **every valid DSM cell**. Missing RGB
outside valid DSM is context-filled only from RGB. Valid black RGB remains a
valid value. The source arrays and files are not edited. Raw blended AI outputs
are retained alongside final heights, with a counted upper bound at the DSM.

The classical code is the explicit native morphological/TIN prototype from
[DsmToDtmProcessing](../DsmToDtmProcessing/README.md). On larger custom inputs its
working spacing is increased as needed to respect the 262,144-support-cell cap;
effective parameters and quality statistics are recorded.

**Different checkpoints and training objectives mean this comparison does not
isolate the causal contribution of RGB.** It compares practical output behavior
on this scene. A rigorous RGB ablation would require controlled training and
evaluation beyond these pretrained profiles.

## Custom raster requirements

- DSM: one metre-height band, north-up square pixels, projected metre XY CRS;
  identity scale/offset, 8 × 8 to 4 million cells, at least 5% valid coverage.
- Orthophoto: exactly three Byte bands explicitly ordered Red/Green/Blue, identity
  scale/offset, exact DSM dimensions/affine grid and equivalent CRS. Missing RGB
  over a valid DSM cell is rejected. Prepare common coverage before loading.
- Optional reference: one height band with the same dimensions/grid and
  equivalent CRS. No reprojection, registration or vertical conversion is hidden
  in loading/inference. Unspecified height units are assumed to be metres.

## Export

**Save comparison folder** stages all files in a new directory under the chosen
parent, writes JSON, then atomically renames the complete directory. Existing
files are not overwritten. Cancelled/failed staging is removed.

- Full-grid `<method>-dtm.tif` and `<method>-removed.tif` for `classical`,
  `pix_grad` and `rgb_grad_fm`.
- Both `<AI-profile>-raw.tif` files before the final upper-bound check.
- `classical-qa.tif`: bits 1 NoData, 2 interpolated, 4 hull fallback, 8 edge band,
  16 upper-bound clamp. These flags are not calibrated confidence.
- Exact `comparison-dsm.tif`, `comparison-rgb.tif`, the three method crops and
  optional `comparison-reference.tif`, without further resampling.
- `comparison.json`: source/model content hashes, native SDK inference provenance,
  channel mappings, source-only context counts, parameters, full/crop metrics,
  source manifest and attribution.

Height outputs are Float32 metres with `-9999` NoData; RGB is Byte. Source CRS and
affine grid are retained, translated for crops. Keep the JSON/source attribution
with exported files. To compare ArcGIS, process this same full DSM there before
extracting the common crop.

## Command-line options and limits

`--data-dir <dir>`, `--sample-archive <zip>` and `--sample-url <HTTPS-url>` select
the cache/exact input package. `--pix-model-archive`, `--rgb-model-archive`,
`--pix-model-url` and `--rgb-model-url` select the same pinned model packages or
HTTPS mirrors. All archive/file hashes remain enforced.

Epoch mismatch, shadows, occlusion, feature misregistration, scale/domain
transfer, oversmoothing and tile seams can affect results. This fixed development
scene is not a held-out benchmark; the provider reference is not surveyed truth
beneath every object. Inspect multiple areas and source dates before drawing
general conclusions about a method.
