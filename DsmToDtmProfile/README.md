# Interactive DSM / DTM elevation profile

Second DSM-to-DTM example: draw your own **A → B transect** on linked native
GeoKernel 2D maps and compare original DSM, classical DTM, AI DTM and provider
reference heights on one chart. This uses the same published SDK acquisition,
runtime layout, navigation icons and SHA-256 checked sample download as the other
Qt examples. All application C++ is in `main.cpp`.

## Build and run

From an x64 Visual Studio developer terminal:

```bat
cd /d D:\projects\GeoKernel.Examples.Qt\DsmToDtmProfile
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build --preset windows-msvc-release
outputs\windows\Release\bin\DsmToDtmProfile.exe
```

Requires Qt 6 and GeoKernel **1.5.40 or newer**. CMake downloads and verifies the
latest public SDK by default. An explicit `-DGEOKERNEL_ROOT=<sdk>` can select a
development SDK. Linux presets are included; Windows validation does not imply a
tested Linux runtime. SDK license/evaluation rules apply.

## Workflow

1. Startup downloads the existing **Zurich v2 ZIP** automatically, checks the
   archive and all 11 extracted files, then reads the four numeric height rasters.
   No additional sample upload or model download is required. Verified sample data
   are cached next to the executable under `data/zurich-dsm-to-dtm-v2`.
2. The left map remains the original DSM. Select **Classical / AI / Reference**
   for the right map. Both maps share position, scale and EPSG:2056. Navigation
   uses native `GisViewer` pan/zoom; the 3D terrain LOD path is not used here.
3. The familiar fixed transect is loaded initially. Choose **Draw transect**,
   click **A**, then **B** on either map. The same line appears on both maps and
   the chart updates immediately. Dragging still pans. Toggling Draw off cancels
   an unfinished line and retains the previous completed profile. Clicks outside
   valid pixel centres are rejected; endpoints must be at least 0.5 m apart.
4. The height chart shows **gray DSM, teal classical, purple AI and dashed orange
   reference** on a common source-height scale. Switch to **Compare errors against
   reference** to see classical-minus-reference and AI-minus-reference around zero.
5. Hover over the chart to mark the corresponding location on both maps and read
   its distance, coordinates and all four numeric elevations. Adjust **Sample
   spacing** to resample the same line (0.5–10 m). Clear removes the transect;
   Example restores the original fixed transect.
6. **Export profile CSV** writes distance, EPSG:2056 easting/northing, all four
   heights and classical/AI signed errors. `<name>.csv.metadata.json` retains the
   source manifest, archive hash, attribution and sampling information. Each output
   is written atomically. A metadata failure is reported after the CSV is saved.
7. The status bar shows loading progress. Cancel stops data preparation; closing
   during loading cancels/joins work before teardown. Reload acquires/reads the
   same pinned sample again. Display relief files live only in a temporary session
   directory and never replace the original rasters.

## Numeric interpretation

Profiles sample **Float32 height arrays**, never display RGB. Bilinear interpolation
uses pixel centres on the shared 0.5 m grid; no extrapolation or implicit datum
conversion is performed. Distances are Euclidean lengths in the EPSG:2056 metre
grid. The sample count is `ceil(length / requested spacing) + 1`, with at most 4096
samples; the actual equal spacing is displayed. Finer sampling cannot add source
detail and can interpolate across sharp roof edges.

The RMSE and MAE card describes **the selected line's shared sample positions**,
not the whole scene or current viewport. It changes with the line and sample
spacing. Error plots use the same reference samples for both methods. The source
reference is evaluation-only and is never fed into the two prepared estimates.

Both maps use derived hillshade with identical light direction, altitude and
contrast, with no per-map normalization. These RGB files are visualization assets;
profiles and metrics use the original numeric rasters.

## Data, attribution and limits

Reuses exactly:

https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/zurich_dsm_to_dtm_v2.zip

ZIP bytes: **3,681,613**. SHA-256:
`91962e0029c57738ed34d2bdef8d104ef751f7b988a582571faddf658a2b0331`.
The compiled [sample-assets.json](sample-assets.json) also pins individual files.
See [DsmToDtm](../DsmToDtm/README.md) for the full inventory and preparation method.

500 × 500 pixels, 250 × 250 m, horizontal EPSG:2056, source-declared LN02 heights
(EPSG:5728) in metres. Source: **Federal Office of Topography swisstopo**,
swissSURFACE3D Raster / swissALTI3D. Retain the archive attribution and notices;
[swisstopo OGD terms](https://www.swisstopo.admin.ch/en/terms-of-use-free-geodata-and-geoservices)
permit processing and redistribution with attribution. PROJ 9.8.1 database and
license are included; process-local data paths avoid stale Conda/ArcGIS settings.
No machine-wide environment variables are changed.

Classical result: roughness-aware morphological / TIN prototype. AI result: David
Perez's experimental **DSM-only pix_grad** adaptation, [Zenodo 7973800](https://doi.org/10.5281/zenodo.7973800),
CC BY 4.0. These are independently prepared results; **this example does not run
new terrain inference**. No model weights, orthophoto or point cloud is required.
The estimates were produced on the complete source tile before cropping.

DSM/reference filename years are **2018/2019**; simultaneous capture is unverified.
This is a fixed development/training scene, not a held-out generalization benchmark.
Line errors are descriptive comparisons against that provider surface, with the
same temporal and reference limitations as the first example.

`--sample-archive <zip>` can use the exact pinned local ZIP; `--sample-url <https-url>`
selects a mirror while retaining all hash checks; `--data-dir <directory>` chooses
the sample cache. Normal startup needs no arguments.
