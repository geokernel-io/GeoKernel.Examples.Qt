# Live DSM to DTM processing

Fourth DSM-to-DTM example: **run a classical conversion on the input DSM**, change
parameters, inspect the newly generated terrain and export numeric GeoTIFFs.
The three linked GeoKernel 2D maps show the original DSM, generated DTM and a
selectable diagnostic map. Application code is in `main.cpp`.

## Build and run

From an x64 Visual Studio developer terminal:

```bat
cd /d D:\projects\GeoKernel.Examples.Qt\DsmToDtmProcessing
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build --preset windows-msvc-release
outputs\windows\Release\bin\DsmToDtmProcessing.exe
```

Requires Qt 6 and GeoKernel **1.5.40 or newer**. The unified SDK CMake package also
requires Vulkan SDK discovery, although this example uses native 2D maps.
CMake acquires and SHA-256 verifies the latest published SDK by default.
An explicit `-DGEOKERNEL_ROOT=<sdk>` selects an SDK installation. Linux presets
are supplied; Windows/MSVC is the validated runtime. SDK license rules apply.

## Workflow

1. Startup downloads and verifies the existing **Zurich v2 sample** and displays
   its original DSM. No conversion is run until **Run DSM → DTM** is pressed.
   No new SampleData upload, Python environment or model weights are required.
2. Choose **Balanced**, **Remove more objects** or **Preserve more relief**, or
   adjust the six parameter controls. Then press **Run**. Computation runs in a
   worker; the status bar reports the current phase and progress. **Cancel**
   discards unfinished work while retaining the last completed source/result.
3. The middle map shows the newly generated DTM with the same light and contrast
   as the original. Pan/zoom is linked by centre and map units per pixel, including
   viewports that differ slightly in width. Click a map pixel to inspect source,
   generated, removed and optional reference heights and QA bits.
4. The right map can show **removed height** (DSM minus DTM, fixed 0–20 m colors),
   **support/edge quality**, or **signed reference error** (fixed −2 to +2 m colors).
   Display saturation does not clip numeric results or scores. NoData is gray.
5. Changing parameters leaves the last completed result and scores visible,
   explicitly marked as needing a new run. The result summary and export retain
   the parameters actually used, not the current unexecuted controls.
6. **Open DSM GeoTIFF** loads another source and clears the old result/reference.
   **Open reference (optional)** attaches an exactly aligned evaluation raster;
   it updates scores without recomputing or changing the generated DTM.
7. **Save result GeoTIFFs** saves the chosen DTM plus companion removed-height,
   QA and JSON files. Source DSM/reference paths cannot be used as output names.
   Existing companions require a replacement decision.

The sample download supports `--data-dir <directory>`,
`--sample-archive <exact-v2.zip>` and `--sample-url <HTTPS-mirror>`.
Archive and extracted-file checks still apply. Cached data live beside the
executable under `data/zurich-dsm-to-dtm-v2`.

## Processing method

The SDK provides raster reading, CRS validation, native map display and the GDAL
runtime. **The classical filter is implemented explicitly in this example**;
GeoKernel 1.5.40 has no dedicated classical DSM-to-DTM API. It does not invoke
ArcGIS Terrain Filter, an AI model or an external script.

Method ID: `native-multiscale-morphological-tin-v1`.

1. Build a working lower-envelope grid by selecting each block's minimum valid
   source height. Record its **actual source pixel centre**, avoiding relocation
   to the block centre on sloping terrain. Working-grid holes receive propagated
   values for filtering; they do not become ground support observations.
2. Use reflected-boundary morphological openings at increasing window sizes.
   A pixel remains a candidate only if it passes every scale. The height allowance
   combines a capped base/slope term with a DSM-derived regional terrain-slope
   allowance, constrained by local slope and reduced in rough areas. Gaussian
   scales and the 0.6 m roughness scale are fixed implementation parameters.
3. Triangulate surviving DSM support points and use linear interpolation directly
   at the original raster's pixel centres through the SDK's
   [GDAL gridding C API](https://gdal.org/en/stable/api/gdal_alg.html).
   Points outside the TIN hull use a four-neighbour grid-propagated support fallback
   and are flagged. Such fallbacks are not exact Euclidean nearest neighbours.
4. Retain source samples in candidate cells when they are within the requested
   keep tolerance. Clamp terrain predictions above the DSM back to the DSM and
   flag that operation. Preserve original NoData and internal validity masks.

This is a new native prototype, adapted from the prior morphological/TIN work.
It interpolates directly onto the native grid and is **not a byte-for-byte port**
of the earlier prepared Python baseline. The reference is never an input to
`estimate`; it is read separately and used only afterward for evaluation.
The prepared classical/AI rasters in the sample ZIP are not used for processing.

| Control | Meaning |
| --- | --- |
| Working grid | Requested support-cell spacing; rounded to an integer source-pixel factor. Effective spacing is reported. |
| Max. window | Largest opening scale; larger objects require larger surrounding support. |
| Base threshold | Initial allowed height above the morphological lower envelope. |
| Slope allowance | Additional base allowance per metre of filter window, capped at 3 m with the base term. |
| Relief allowance | Weight of the DSM-derived terrain-slope term. |
| Keep tolerance | Maximum difference for retaining original samples in candidate cells. |

Smaller allowances tend to remove more features but can also remove real terrain.
Preset names describe parameter choices, not guaranteed results or Esri-equivalent
ground detection methods. Inspect errors and terrain shape before using an output.

## Numeric output and quality

Choosing `terrain-result.tif` produces:

| File | Content |
| --- | --- |
| `terrain-result.tif` | Float32 terrain elevations in metres |
| `terrain-result.removed.tif` | Float32 `DSM - DTM`, nonnegative on valid cells |
| `terrain-result.qa.tif` | Byte bit mask, combinations allowed |
| `terrain-result.json` | Parameters, effective working grid, source hashes/CRS, support statistics, evaluation scores and provenance |

Numeric rasters retain the source dimensions, affine transform and CRS. Height
NoData is `-9999`; QA NoData is `1`. Export uses an atomic write for **each file**,
not a transaction across all four files. Cancellation/failure can leave already
committed files; the status reports the committed paths. Session intermediates
are temporary and are removed when the application exits.

QA bits:

| Bit | Meaning |
| ---: | --- |
| 1 | Source NoData |
| 2 | Interpolated rather than explicitly retained source sample |
| 4 | Outside TIN convex hull: support-grid fallback |
| 8 | Edge-sensitive band, half the requested maximum window |
| 16 | Prediction clamped to the original DSM height |

The QA display prioritizes fallback purple, edge orange, interpolation pale and
retained source teal. The exported raster preserves full bit combinations.
The edge flag is a context warning, not a confidence probability.

Reference scores report RMSE, MAE and signed bias (`DTM - reference`) over shared
valid source/reference cells. The app separately reports the interior after
excluding the edge band. If no valid interior/reference overlap exists, scores
are unavailable and JSON uses null values. All scores use raw numeric heights.

## Supported inputs and limitations

- Single-band north-up GeoTIFF, square pixels, projected CRS with metre XY units.
  Heights must already be metres; unspecified band units are assumed metres.
  Nonidentity band scale/offset must be applied explicitly before opening.
- At least 8 × 8 and at most **4 million source cells**; at least 5% valid coverage.
  Working grid must be 3 × 3 or larger and at most **262,144 cells**. Increase the
  working spacing for larger inputs. The maximum window must span at least two
  working cells. A failure retains the previous completed result.
- Optional reference must match dimensions, affine grid and equivalent CRS.
  There is no automatic reprojection, alignment or vertical datum conversion.
- Filtering checks cancellation between rows/scales, and gridding has a progress
  cancellation callback. GDAL's TIN construction has no cancellation callback;
  cancellation is checked immediately before and after that phase.
- Dataset boundaries, large roofs, dense vegetation, noise and steep terrain can
  produce poor supports or interpolation artifacts. More surrounding DSM context
  helps: process a larger raster and crop the result afterward. Existing prepared
  estimates were computed on a full 1 km tile; this live example processes the
  supplied raster only, so their scores do not measure an isolated algorithm change.

## Sample and attribution

Reuses [zurich_dsm_to_dtm_v2.zip](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/zurich_dsm_to_dtm_v2.zip),
**3,681,613 bytes**, SHA-256
`91962e0029c57738ed34d2bdef8d104ef751f7b988a582571faddf658a2b0331`.
[sample-assets.json](sample-assets.json) pins the ZIP and all 11 extracted files.
The included PROJ database supplies the SDK runtime when an SDK-local database
is unavailable; environment paths change only within this process.

Source: **Federal Office of Topography swisstopo**, swissSURFACE3D Raster /
swissALTI3D, 500 × 500 at 0.5 m, EPSG:2056, source-declared LN02 heights
(EPSG:5728). Keep the archive attribution and license notices. See
[DsmToDtm](../DsmToDtm/README.md) for source preparation and full inventory.
This fixed development scene is not a held-out benchmark. DSM/reference filename
years are 2018/2019; simultaneous acquisition is unverified. Provider reference
errors do not establish surveyed truth beneath every object or performance
elsewhere. No model weights or ArcGIS-generated result is redistributed here.
