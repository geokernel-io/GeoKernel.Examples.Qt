# DSM / DTM reference accuracy by area

Third DSM-to-DTM example: compare **where classical and AI terrain estimates differ
from the same reference**. Three linked native GeoKernel 2D maps retain the original
DSM on the left, with classical and AI error maps beside it. Select an area to
compare numeric errors, their distribution and individual source pixels.
All application C++ is in `main.cpp`.

## Build and run

From an x64 Visual Studio developer terminal:

```bat
cd /d D:\projects\GeoKernel.Examples.Qt\DsmToDtmAccuracy
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build --preset windows-msvc-release
outputs\windows\Release\bin\DsmToDtmAccuracy.exe
```

Requires Qt 6 and GeoKernel **1.5.40 or newer**. The unified SDK CMake package also
requires Vulkan SDK discovery, although this application uses 2D maps.
CMake acquires and verifies the
latest published SDK by default. An explicit `-DGEOKERNEL_ROOT=<sdk>` selects
a different SDK. Linux presets are included; the validated runtime is Windows.
GeoKernel license/evaluation rules apply.

## Workflow

1. Startup downloads the existing **Zurich v2 sample**, verifies the ZIP and all
   11 extracted files, then prepares display maps. The status bar shows progress
   and supports cancellation. No new sample upload or model download is needed.
2. Start with **Signed reference errors**: blue is below the reference, white is
   zero and red is above it. Both methods share the exact same metre scale.
   **Absolute reference errors** highlights error magnitude; **Terrain estimates**
   shows the two surfaces with identical hillshade lighting and contrast.
3. Choose a shared **Display limit** of 1, 2, 5 or 10 m. Colors saturate at this
   limit. Changing colors or map zoom never changes the numeric evaluation.
4. Choose a geographic preset or **Select evaluation area**, then drag a box on
   any map. The same source cells are evaluated for both methods; cells outside
   the area are dimmed on all three maps. **Evaluate current view** uses the
   visible extent; **Full sample** restores the complete evaluation area.
5. Read RMSE, MAE, signed bias, P95 absolute error and the percentage exceeding
   **Large error >**. The sidebar separately identifies the lower RMSE and lower
   MAE. These metrics can favor different methods; no overall winner is inferred.
   The histogram shows six common absolute-error bins as percentages.
6. With Pan active, click a map pixel to read original, reference, classical and
   AI heights plus both signed errors. A shared marker identifies that cell on
   every map. Probing does not change the evaluation area or scores.
7. **Export area report** writes JSON containing numeric metrics, histogram counts,
   threshold, native pixel window, evaluated cell bounds, source manifest,
   archive hash and attribution. The file is written atomically.

## Numeric and display interpretation

All scores use original **Float32 source height arrays**, not rendered images.
The four rasters must have matching CRS/grid and valid heights throughout this
prepared sample. An area includes native cell centres inside its bounding box,
clipped to the sample. The displayed footprint is the union of those evaluated
0.5 × 0.5 m cells, so it can differ slightly from the drawn box.

Errors are `estimate - reference`. RMSE is the square root of mean squared error;
MAE is mean absolute error; bias is mean signed error. P95 is the nearest-rank
empirical 95th percentile of absolute errors (`ceil(0.95 * N)`, one-based).
The large-error count uses a strict `abs(error) > threshold` comparison.
Histogram bins are `[0, 0.25]`, `(0.25, 0.5]`, `(0.5, 1]`, `(1, 2]`, `(2, 5]`
and `(5, infinity)` metres. Pixel inspection reads the containing cell without
interpolation; the profile example separately demonstrates bilinear sampling.

Full-sample descriptive results (250,000 common pixels):

| Method | RMSE (m) | MAE (m) | Bias (m) |
| --- | ---: | ---: | ---: |
| Classical | 1.006692 | 0.350048 | 0.247517 |
| AI, experimental pix_grad | 0.659430 | 0.406952 | -0.277471 |

Here AI has lower RMSE while classical has lower MAE. Area selection helps reveal
where larger residuals and small widespread errors contribute to that difference.
The full/central/northern/southern presets are geographic windows, not semantic
building, forest or terrain classes.

Display GeoTIFFs are **Byte RGB** with precomputed shared colors. This preserves
the legend instead of allowing independent statistical stretches of each map.
Nineteen small display rasters cover the original relief, both terrain reliefs
and the signed/absolute color ranges. They live in a temporary session directory;
source rasters are retained unchanged. Navigation uses native `GisViewer` 2D
pan/zoom rather than terrain LOD rendering.

## Data and limitations

Reuses [zurich_dsm_to_dtm_v2.zip](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/zurich_dsm_to_dtm_v2.zip):
**3,681,613 bytes**, SHA-256
`91962e0029c57738ed34d2bdef8d104ef751f7b988a582571faddf658a2b0331`.
[sample-assets.json](sample-assets.json) pins the archive and each extracted file.
Data are cached beside the executable under `data/zurich-dsm-to-dtm-v2`.
Optional arguments are `--data-dir <directory>`, `--sample-archive <exact-v2.zip>`
and `--sample-url <HTTPS-mirror>`; hash checks still apply.

The sample is 500 × 500 cells, 250 × 250 m, horizontal **EPSG:2056**, source-declared
LN02 heights (**EPSG:5728**) in metres. No vertical datum conversion is performed.
Source: **Federal Office of Topography swisstopo**, swissSURFACE3D Raster and
swissALTI3D. Retain the sample attribution and license notices. The sample includes
a matching PROJ database and license; runtime data paths are configured only for
this process. See [DsmToDtm](../DsmToDtm/README.md) for preparation and full inventory.

Classical output is the roughness-aware morphological/TIN prototype. AI output is
David Perez's experimental DSM-only **pix_grad** adaptation, CC BY 4.0. Both are
independently prepared results; the reference is used only for evaluation. This
example performs no new inference and requires no model weights, orthophoto or
LAS/LAZ file.

This is a fixed development/training crop, **not a held-out benchmark**. Source
filename years are DSM 2018/reference 2019; simultaneous capture is unverified.
Scores describe differences from that provider reference, which is not an exact
survey of the ground under every object. They do not establish which method
generalizes better to other locations or land-cover types.
