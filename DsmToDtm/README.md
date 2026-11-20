# DSM to DTM — ground beneath buildings

First terrain-conversion presentation example: an original DSM remains visible
beside a prepared terrain estimate in two synchronized **native GeoKernel 3D**
viewers. Uses the published SDK downloader, icon resources, build presets,
runtime layout and automatic sample acquisition used by the other Qt examples.
All application C++ is in `main.cpp`. No Python, external GDAL installation,
model weights or AI service is required to run the example.

## Build and run

From an x64 Visual Studio developer terminal:

```bat
cd /d D:\projects\GeoKernel.Examples.Qt\DsmToDtm
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build --preset windows-msvc-release
outputs\windows\Release\bin\DsmToDtm.exe
```

Requires Qt 6, Vulkan and GeoKernel **1.5.40 or newer**. Configure downloads the
latest published SDK and verifies the release SHA-256 manifest. Package discovery
is restricted to that SDK. An explicit `-DGEOKERNEL_ROOT=<sdk>` permits a local
development SDK. Linux presets are included; Windows validation does not imply
a tested native Linux run. The published SDK's runtime/license rules apply.

## Workflow

1. Startup automatically downloads the small fixed sample ZIP from the
   `GeoKernel.SampleData` **v1 release**. Both the ZIP and every extracted file
   are SHA-256 checked against the compiled resource. A complete valid cache
   under the executable's `data/zurich-dsm-to-dtm-v2` works offline.
2. The left view always shows **original DSM: roofs, trees and ground**. The
   right initially shows the **classical DTM at 100%**. Pan, orbit and zoom either
   view; the other follows. Reset / Top view / Zoom and source-data folder actions
   use the same toolbar conventions as the other examples.
3. Select **Classical / morphological-TIN** or **AI / DSM-only pix_grad**. These
   are independent prepared estimates from the same original DSM. Neither is
   the other method's input. **No new inference runs in this presentation example.**
4. Move the surface slider, then release. **0% is exactly the original DSM;
   100% is exactly the selected prepared DTM.** Intermediate values linearly
   interpolate numeric heights: `(1-t)*DSM+t*DTM`. The right-hand geometry is
   rebuilt asynchronously; this is neither an image crossfade nor a new terrain
   algorithm. Keyboard changes use a short debounce. The original remains visible.
5. The profile samples all four original numeric rasters on the same fixed
   building transect. Gray: DSM; teal: classical; purple: AI; dashed orange:
   reference DTM. It compares final source endpoints, independently of the slider
   and display exaggeration. A two-endpoint 3D guide marks the same transect;
   the connecting guide segment is not a densely draped measurement line.
6. Both panes use the same constant synthetic gray texture and lighting, avoiding
   independently normalized elevation-color ramps or roof imagery on bare terrain.
   Vertical exaggeration affects both views and is explicitly shown (default 1×).
7. Progress appears in the status bar. Cancel stops further preparation and keeps
   the previous completed views. Failed/cancelled method/slider changes restore
   the last committed selection. Closing during preparation cancels and joins work
   before the native viewers / Vulkan instance are destroyed.

Intermediate GeoTIFFs are temporary session files, cleaned up on exit. Original
sample rasters are never rewritten. The reference DTM is used only for profile
comparison and error scoring; it never enters terrain estimation or interpolation.

## Sample and upload

The v2 sample is published in the existing SampleData v1 GitHub release.
See [UPLOAD.md](UPLOAD.md) for the exact archive and release-asset convention.
The default public URL has been validated with an empty sample cache.

URL:

https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/zurich_dsm_to_dtm_v2.zip

The resource [sample-assets.json](sample-assets.json) pins archive size, SHA-256
and each file hash. The archive is approximately **3.51 MiB**, containing:

| File | Role |
|---|---|
| `dsm.tif` | Original observed surface, Float32 metres |
| `dtm_classical.tif` | Prepared roughness-aware morphological / TIN estimate |
| `dtm_ai.tif` | Prepared experimental DSM-only pix_grad estimate |
| `dtm_reference.tif` | Provider terrain for evaluation only |
| `neutral_surface.tif` | Synthetic constant RGB albedo; not an orthophoto |
| `proj-data/proj.db` | PROJ 9.8.1 coordinate database matching the SDK runtime |
| `PROJ-LICENSE.txt` | PROJ database redistribution notice |
| `source-manifest.json` | Grid, source URLs/hashes, methods, model identity and crop metrics |
| `ATTRIBUTION.txt` | Source and adaptation credits |
| `MODEL-LICENSE-CC-BY-4.0.txt` | Pretrained-model license notice |
| `README.txt` | Data roles and interpretation |

All rasters share 500 × 500 pixels at 0.5 m, EPSG:2056, bounds
`2677100,1251350,2677350,1251600`. Heights retain the source-declared LN02 reference
(EPSG:5728), metres. There is **no vertical datum conversion**. The native local
3D preview explicitly uses the SDK's unknown/unverified height mode rather than
claiming these are verified ellipsoidal heights.

DSM/reference filename years are **2018/2019**, not verified simultaneous surveys.
The 250 m crop is the existing fixed urban presentation window from a development
scene; it was not chosen using reference error. Prepared results were generated
on the complete 1 km tile, preserving surrounding context, then cropped without
resampling. The upstream pretrained model's training overlap has not been ruled
out; this demonstration is **not a held-out generalization benchmark**.

For this crop's 250,000 common valid reference pixels:

| Method | RMSE (m) | MAE (m) |
|---|---:|---:|
| Original DSM | 6.2744 | 3.6840 |
| Classical DTM | 1.0067 | 0.3500 |
| AI DTM | 0.6594 | 0.4070 |

The metrics rank differently: AI has lower RMSE here; classical has lower MAE.
These are crop-specific reference errors, not scores for the current viewport or
transition state, and do not override the larger gallery's evaluations.

Data: **Federal Office of Topography swisstopo**, swissSURFACE3D Raster and
swissALTI3D. [OGD terms](https://www.swisstopo.admin.ch/en/terms-of-use-free-geodata-and-geoservices)
permit redistribution/processing with mandatory source attribution. Retain the
archive notices. AI: David Perez's [pix_grad checkpoint](https://doi.org/10.5281/zenodo.7973800),
CC BY 4.0, adapted for deterministic GeoKernel inference (patch statistics,
dropout disabled). The archive contains derived output, **no weights or upstream
notebook implementation**. The full tile and model SHA-256 identities are in the
source manifest. No orthophoto or point-cloud input is needed for this first example.

The v2 ZIP retains the complete EPSG:2056 metadata and includes the matching PROJ
coordinate database. The application prefers SDK-local `proj-data` when supplied;
otherwise it uses the verified database from this sample. Process-local
`PROJ_DATA` / `PROJ_LIB` and packaged `GDAL_DATA` take precedence over stale
machine-wide Conda/ArcGIS paths. No system environment settings are modified.
The earlier v1 upload candidate is superseded and is not used by this example.

## Local data and maintenance

Use the exact pinned local ZIP without changing download standards:

```bat
outputs\windows\Release\bin\DsmToDtm.exe --sample-archive outputs\sample-upload\zurich_dsm_to_dtm_v2.zip
```

`--sample-url <https-url>` can select an HTTPS mirror. All hash checks still apply.
`--data-dir <directory>` selects the sample cache location. An unavailable release asset reports its URL and a
retry instruction; no local machine-specific source path is compiled into the app.

Maintainer-only regeneration (Python + NumPy + rasterio; not a runtime dependency):

```bat
python tools\prepare_sample.py --models-root D:\projects\GeoKernel.Models --proj-data <PROJ-9.8.1-data-folder>
```

This validates frozen full-tile input hashes, writes deterministic sample ZIP
contents to `outputs/sample-upload`, and updates the resource hash pins. If the
content changes after publication, issue a **new archive version**, update the
pins, and rebuild; do not replace the pinned v2 asset with different bytes.
