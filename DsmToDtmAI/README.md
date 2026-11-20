# Live AI / classical DSM to DTM comparison

Fifth DSM-to-DTM example: run the experimental **pix_grad** model through
**GeoKernel.AI**, run the native classical filter independently on the same DSM,
and compare newly generated heights. Three linked native 2D maps retain the
original DSM on the left. Neither method receives the reference or the other
method's terrain estimate.

Application/UI and inference orchestration are in `main.cpp`. `TerrainSupport.h`
contains raster IO, display helpers and the native morphological/TIN prototype
copied from [DsmToDtmProcessing](../DsmToDtmProcessing/README.md), keeping this
example independently buildable. No prepared DTM is used as a conversion result.

## Build and run

From an x64 Visual Studio developer terminal:

```bat
cd /d D:\projects\GeoKernel.Examples.Qt\DsmToDtmAI
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build --preset windows-msvc-release
outputs\windows\Release\bin\DsmToDtmAI.exe
```

Requires Qt 6, GeoKernel **1.5.40 or newer**, and the Vulkan SDK discovery required
by the unified SDK CMake package. The application uses 2D maps and CPU inference.
CMake downloads and checksum-verifies the latest public SDK by default; an
explicit `-DGEOKERNEL_ROOT=<sdk>` selects an installed SDK. Windows/MSVC is the
validated platform; Linux presets do not imply a tested Linux runtime.
No Python, TensorFlow, PyTorch or ArcGIS installation is needed at runtime.
GeoKernel license/evaluation rules apply.

## Workflow

1. Startup verifies the existing Zurich v2 support ZIP (including the matching
   PROJ database), then downloads **the original full 1 km DSM and reference**
   directly from swisstopo. Both are 2000 × 2000 at 0.5 m, EPSG:2056. File sizes
   and SHA-256 identities are pinned in [terrain-assets.json](terrain-assets.json).
   No new terrain ZIP needs to be uploaded to SampleData.
2. The model is automatically acquired and every package file checked against
   [model-assets.json](model-assets.json). **Use model ZIP** supplies the same
   pinned package locally; it does not accept arbitrary model weights.
3. Choose tile overlap (default **64 px**, range 0–128), CPU threads (default 4)
   and a classical preset, then press **Run AI + classical**. The native classical
   result is computed first, followed by an independent GeoKernel.AI inference
   from the same source DSM. The progress bar reports phases and AI tile counts.
4. Compare terrain relief, removed heights or signed reference errors. Both
   terrain maps share identical lighting/contrast. Removed heights use one fixed
   0–20 m color scale; signed errors use one fixed −2 to +2 m scale. Display limits
   never clip numeric metrics. Gray represents NoData.
5. **Full input** evaluates all common valid pixels. **Shared comparison crop**
   zooms to the exact 250 × 250 m window used by the earlier Zurich examples:
   bounds `2677100,1251350,2677350,1251600`, source window column 200 / row 800,
   500 × 500 cells. The dashed box marks evaluated cells; surrounding visible
   pixels are dimmed. Both methods still process the full 4-million-cell input.
6. Pan/zoom is linked by centre and map units per pixel. Click a cell to inspect
   original, classical, AI and optional reference heights. Changing display,
   evaluation scope or the viewport never reruns inference.
7. Changing processing settings leaves the last completed comparison visible and
   marks the controls as needing a new run. **Cancel** discards unfinished work
   and retains the completed comparison. Closing during work cancels/joins it.
8. **Open DSM GeoTIFF** supports a custom source and clears the prior reference
   and estimates. **Open reference** attaches an exactly aligned evaluation-only
   raster; it rescales neither input nor result and does not rerun inference.
   For custom sources the comparison crop is the fixed central 60% in each axis.

## Model and acquisition

Weights: **David Perez**, [Zenodo 7973800](https://doi.org/10.5281/zenodo.7973800),
**CC BY 4.0**. The original release is a TensorFlow checkpoint; this example uses
the independently reconstructed deterministic ONNX adaptation previously checked
against adapted TensorFlow and native SDK outputs. The package includes a model
card, license, manifest and conversion/parity records. Those records verify
conversion, not terrain accuracy.

- One Float32 NCHW input/output, **256 × 256** pixels; DSM heights in metres only.
- Per-patch DSM min/max normalization and inverse scaling are inside the graph.
  Do not add RGB, reference terrain, labels or classical results as channels.
- Learned kernels and affine normalization parameters are retained. Patch batch
  statistics are used; dropout is disabled. Unused NaN running variance is not
  read. This differs from the author's stochastic prediction procedure.
- The defined flat-patch case returns its elevation. Training region is reported
  as Brussels/UrbIS; training GSD and generalization to this input are unverified.
- ONNX SHA-256:
  `e400e27f6afc75bcf4e1a61432ee9557529683903996dd953d3e8e3fcbe90eb6`.

Model ZIP: `davpergan-dtm-dsm-gan-pix-grad-geokernel-onnx-v1.zip`,
**200,717,538 bytes**, SHA-256
`0ee47ae568bd0e3af5896c4421a688958ad156b017f911ab03623383cd6aed60`.
The [model ZIP is published on Cloudflare R2](https://pub-00d214f25f754884b22349c19ac37bbf.r2.dev/geokernel-models/davpergan-dtm-dsm-gan-pix-grad-geokernel-onnx-v1.zip).
Automatic acquisition was verified on 2026-11-06 through normal application
startup with an empty model cache and no local ZIP argument. The downloaded
archive size/SHA-256 and all six extracted package-file hashes matched the pins.
The upload bundle and publication record live under `outputs/model-upload`;
verification evidence is in `outputs/validation/public-model-download.json`.
For offline/local use, the same pinned ZIP can be supplied explicitly:

```bat
outputs\windows\Release\bin\DsmToDtmAI.exe --model-archive outputs\model-upload\davpergan-dtm-dsm-gan-pix-grad-geokernel-onnx-v1.zip
```

Optional arguments:

- `--data-dir <directory>`: cache root for terrain, support and model files.
- `--model-archive <zip>` / `--model-url <HTTPS-url>`: exact pinned model ZIP.
- `--sample-archive <zip>` / `--sample-url <HTTPS-url>`: existing pinned Zurich v2
  support ZIP, not the full 1 km source. Hash checks still apply to all mirrors.

No model weights are embedded in the executable or source repository. Models
remain an experimental example package outside the generic SDK model catalog.

## Numeric behavior

The SDK request is created with `AiRasterInferenceExecutor::requestFromPackage`.
The primary DSM defines the original grid and validity. Missing DSM values are
filled for input context using four-neighbour propagation from valid DSM samples;
the filled raster is a separate input band. The reference does not participate.
Edge padding handles inputs smaller than 256 px. The original mask is restored
on output through the SDK, and valid output dimensions/CRS/grid are checked.

The graph already bounds output above by the DSM. The example retains the raw
blended AI raster and applies a final upper bound to handle any overlap rounding
overshoot; its count is recorded. Invalid source pixels become `-9999`. Classical
height/QA behavior follows the previous native example. On large custom inputs
the selected classical working spacing is increased as needed to respect the
262,144-support-cell limit; effective settings are recorded.

Scores use Float32-derived raw height values accumulated in double precision:
RMSE, MAE and mean signed bias, `estimate - reference`, in metres. The full and
shared-crop metrics are both retained in JSON regardless of the current view.
Without a reference, inference works and scores are unavailable/null.

Supported custom inputs: one north-up metre-height band, projected metre XY CRS,
square pixels, identity band scale/offset, 8 × 8 to **4 million cells**, at least
5% valid coverage. Internal validity masks and NoData are respected. Reference
CRS must be equivalent and dimensions/affine grid identical; no implicit warping,
resampling, height-unit conversion or vertical datum conversion occurs.

## Export

**Save comparison folder** creates a unique `dsm-dtm-ai-<time>-<id>` directory
inside the chosen parent. Files are staged in that parent and the directory is
renamed only after every file and JSON is complete. Existing files are not
overwritten; failed/cancelled staging is cleaned up.

| File | Content |
| --- | --- |
| `classical-dtm.tif`, `ai-dtm.tif` | Full-grid Float32 terrain heights |
| `ai-raw.tif` | SDK blended heights before the final upper-bound check |
| `classical-removed.tif`, `ai-removed.tif` | Full-grid DSM minus final terrain |
| `classical-qa.tif` | Native classical quality bit mask |
| `comparison-dsm.tif` | Exact source-pixel comparison crop |
| `comparison-classical.tif`, `comparison-ai.tif` | Exact matching output crops |
| `comparison-reference.tif` | Optional matching evaluation crop |
| `comparison.json` | Source/model hashes, parameters, SDK provenance, full/crop metrics, output inventory and attribution |

All height files retain source CRS, metre units and the original grid (translated
for crops). Source/reference files are read-only inputs. `classical-qa.tif` uses
bits 1 NoData, 2 interpolated, 4 hull fallback, 8 edge band and 16 upper-bound
clamp. These are processing flags, not calibrated confidence probabilities.

To compare ArcGIS fairly, process the **same full DSM** there and extract the
same comparison crop. A Terrain Filter result generated only on the small crop
has a different boundary/context setup.

## Interpretation and attribution

Terrain: **Federal Office of Topography swisstopo**, swissSURFACE3D Raster and
swissALTI3D. Source links, byte sizes and hashes are in `terrain-assets.json`.
Retain the original support archive attribution/license notices. Source height
reference is LN02 / EPSG:5728 as declared by the provider; no conversion occurs.
Source filenames identify DSM 2018 and reference 2019; simultaneous capture is
unverified. The selected development scene is not a held-out benchmark.

This AI model can smooth real terrain, introduce bias and leave tile-related
artifacts; a smaller RMSE in one crop does not establish superiority elsewhere.
Neither the provider DTM nor the QA flags establish surveyed ground beneath
every object. Display original/terrain/errors together and inspect multiple
areas before choosing a method.
