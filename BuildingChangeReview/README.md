# Building change review

Standalone Qt / C++ example for two-date building-change proposal, human review,
vertex editing and approved GIS export. Uses the same SDK acquisition, runtime
deployment, navigation icons and automatic SHA-256 checked sample preparation as
the other GeoKernel examples. No Python or separate GDAL installation is needed.

## Build and run

From an x64 Visual Studio developer terminal:

```bat
cd /d D:\projects\GeoKernel.Examples.Qt\BuildingChangeReview
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build --preset windows-msvc-release
outputs\windows\Release\bin\BuildingChangeReview.exe
```

Requires GeoKernel 1.5.39 or newer and Qt 6. An empty `GEOKERNEL_ROOT` acquires the
latest published SDK and verifies its release checksum. Explicit
`-DGEOKERNEL_ROOT=<extracted-sdk>` selects a development installation. Linux
presets are provided; native Linux validation is separate from the Windows run.

## Workflow

1. Startup automatically downloads and verifies the fixed imagery pair and the
   HOTOSM DINOv3-S building model. Archives and extracted assets are cached under
   the executable's `data` directory. Run retries interrupted preparation.
2. The two synchronized maps show Beeldmateriaal Nederland / PDOK's 2016 and
   2025 annual orthophoto layers for Almere / Oosterwold, Netherlands.
   Initial extent is a 320 m square inside the 1.024 km square
   dataset. **Demo area (fixed)** analyses that exact crop for a reproducible first
   run; the **Demo area** toolbar action returns the maps there. Select **Current
   view (both dates)** to analyse a pan/zoom selection, or **Full dataset** for the pair.
3. Choose an execution provider and candidate filter (**Conservative** by default;
   **Balanced** accepts weaker evidence). Expand **Advanced candidate filters**
   to inspect the raw-score and coverage limits. **Run building change
   analysis** performs independent inference on both dates on a worker thread.
   Cancel stops further processing; no partial result is published.
4. Green polygons appear on the 2025 map as addition candidates; red polygons
   appear on the 2016 map as removal candidates. Thinner fills preserve roof
   visibility. Selection highlights the same location on both dates for comparison.
   Double-click a row or use **Locate selected candidate** to compare close up.
   Uncheck **Show candidate overlays** to inspect the underlying imagery. All
   candidates start pending. Hover a row to inspect both dates' mean model scores.
5. Enter a reviewer and optional note, then approve, reject or return a candidate
   to pending. Rejected geometry is gray; approved geometry has stronger fill.
6. **Edit Vertices (2025)** edits the candidate overlay on the right map: drag a
   vertex, double-click a segment to insert a vertex, or Delete the active vertex.
   Undo/redo is available. A geometry edit resets its approval to pending and is
   mirrored on the left. Source imagery is never rewritten.
7. **Save approved GeoJSON** exports approved polygons only, in WGS84
   longitude/latitude. Source-metre areas, review notes, imagery hashes, model
   identity/hash, analysis parameters and the session review event chain are
   retained. Export is written atomically. This is a review history, not a signed
   identity or independently tamper-proof audit service.

Analysis and review results are session-only until explicitly exported. Startup
does not load old predictions. A successful new analysis replaces the previous
session results after confirmation; cancellation/failure retains completed results.

## Comparison method and limits

The same model produces mask, boundary and distance outputs for each date. The
SDK separates predicted instances and polygonizes them with four-connectivity.
The building separation threshold is the package default, **0.4371**. Raising
it can miss existing roofs and create false changes, so it is not a direct
change-confidence control. Each instance is checked against the other date's
**raw building scores**, including evidence missed by instance separation.
The conservative candidate policy uses area at least **50 m²**, own-date mean
score at least **0.75**, other-date mean score at most **0.20**, and less than
**10%** coverage by other-date scores >= **0.25**, buffered by a **2 m** square
matching tolerance. Balanced uses 20 m² / 0.55 / 0.35 / 20% / 1 m respectively;
the weak-score coverage threshold stays 0.25. These are demo-tuned heuristics,
not independently calibrated operating points. Instances touching the analysis boundary are excluded
to avoid treating clipped buildings as complete changes. Small/invalid polygon
parts are excluded and reported. Candidate counts are polygon counts; one
predicted instance can have disconnected polygon parts.

The tolerance cushions small boundary differences; it does not register imagery.
This method proposes mostly new/removed buildings, not a complete roof-extension
or semantic change inventory. Matching coverage is not a calibrated confidence.
Shadows, parallax, radiometry, alignment and model omissions can still create false
changes: an existing roof missed in one date may pass even conservative filters.
Score filtering cannot repair this model failure. The startup block was chosen
because development from fields to residential plots is visible in the image pair;
it is not an unbiased evaluation set.
Stricter filters also omit real changes with weaker evidence. There are no supplied
reference labels and no accuracy claim. A review
approval is a human decision, not proof of construction date or regulatory status.

## Fixed assets and attribution

Imagery archive (approximately 11.6 MB):

https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/almere_oosterwold_change_2016_2025_v1.zip

SHA-256: `ecc35ef2c6bbdd586adc6ecc812ce1c74aee62fbf8e404de1d63b75bf7137a88`

Both rasters are also checked against their individual SHA-256 values. They share
EPSG:25831, a 0.5 m grid and 2048 × 2048 RGB pixels. Bounds are
`657533,5800382,658557,5801406` (min easting/northing, max easting/northing).
The fixed demo bounds are `657885,5800734,658205,5801054`. They are JPEG WMS
renders from PDOK's 25 cm annual RGB layers, cropped/reprojected at 50 cm and
converted to GeoTIFF without further pixel loss; source JPEG compression remains.
Annual layer
years are not verified exact local capture dates. Retain the archive's
`ATTRIBUTION.txt` and `source-manifest.json`; imagery is CC BY 4.0. Credit
Beeldmateriaal Nederland / PDOK, 2016 & 2025; see
https://www.beeldmateriaal.nl/dataroom for the provider license statement.
The cache is `data/oosterwold-2016-2025`; older sample caches are independent.

Model (approximately 192 MB):

https://pub-00d214f25f754884b22349c19ac37bbf.r2.dev/geokernel-models/hotosm-dinov3s-buildings-geokernel-onnx-v1.zip

SHA-256: `3a6cd11e013f81b46b3db700feddef2d98319780285b8320e1200573684b24bb`

Retain HOTOSM/Meta credits, CC-BY-4.0 documents and the separate DINOv3 license
included in the package. SDK licensing is separate. Internet access is needed for
uncached downloads; cached inference is local.

## Verification

Optional comparison tests use deterministic instance masks: different IDs for an
unchanged roof, additions/removals, square-metre area filtering, offset tolerance,
edge clipping, incompatible grids/CRS, malformed labels and cancellation. Raw-score
checks cover missed instance labels, weak own-date evidence, other-date score
gating, score-grid misalignment, nonfinite scores and missing score dates.

```bat
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 -DBUILD_TESTING=ON
cmake --build --preset windows-msvc-release
ctest --test-dir build/windows-msvc -C Release --output-on-failure
```

Tests are a separate executable; the interactive application has no test-mode
command-line branches. Model inference and GPU/provider acceptance require the
actual model and the target hardware.

The offscreen publication regression uses the same window/result-publication
code as the application. It verifies source-layer preservation, candidate-layer
editing and selection, date-specific symbology, reversible overlay inspection,
undo/redo after layer reordering, repeated publication,
empty results, and failed-publication cleanup followed by recovery. Viewer layers
are looked up by pointer; adding a new SDK layer prepends it to the stack.

An opt-in real-image test uses the application's asset preparation to verify and
extract the pinned imagery ZIP and open both rasters. It also exercises both
inference passes, publishes actual results into both offscreen viewers, and checks
pending/approved/rejected export gating, edit-triggered approval invalidation and
geographic GeoJSON export. The model cache must be present, or it is downloaded:

```bat
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 -DBUILD_TESTING=ON -DBUILD_INFERENCE_TESTS=ON
cmake --build --preset windows-msvc-release
ctest --test-dir build/windows-msvc -C Release --output-on-failure
```

Validated on Windows x64, Qt 6.11.0 and the published SDK 1.5.39: 26 deterministic
comparison/geometry checks passed. The real-image CPU test on the documented
320 m Oosterwold square produced 0 / 68 predicted instances and 41 addition
candidates with the conservative defaults, in 13.8 seconds on this machine.
The earlier image legitimately has zero instances because the selected area was
fields. These counts are predictions, not reference-label accuracy measurements.
Pending/rejected export exclusion, approval invalidation after geometry changes,
correct imagery years in GeoJSON, and geographic export are verified.
Reports include raw per-instance evidence and candidate source geometries under
`outputs/validation/`. This is a functional verification,
not a ground-truth accuracy measurement or portable performance guarantee.
