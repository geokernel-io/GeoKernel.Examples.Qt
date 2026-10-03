# ViewshedAnalysis

Example 14: choose an observer on a DEM and calculate sampled terrain visibility.

## Data and SDK

Uses the same SDK acquisition and runtime helpers as the other Qt examples.
Without GEOKERNEL_ROOT, CMake downloads the latest public SDK. The SDK must
export GeoKernel::Viewer3D and include Viewshed.h. A local 3D SDK can be used
until public packaging includes these APIs.

On first launch these sample archives download automatically:
- https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_terrain.zip
- https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_ortophoto.zip

Data is cached next to the executable and opens automatically. The loading
page remains visible until the actual DEM is ready. No synthetic terrain is
shown during loading. Reload reuses the downloaded files.

## Use

1. Click the loaded terrain with Select observer enabled. A vertical marker
   joins the picked ground point to the observer eye height.
2. Set observer and target heights above terrain, radius, grid density and
   requested ray sample spacing.
3. Click Calculate. The north-up result grid fills incrementally. Green means
   visible at sampled positions, red blocked, gray unknown/NoData, and pale
   cells pending. Empty space outside the circular radius is excluded.
4. Pause preserves partial results. Resume continues them. Changing parameters
   or selecting another observer clears obsolete results. Clear removes both
   observer and result; enable Select observer to choose another position.

Completed results (and paused partial results) also appear on the 3D terrain
at 55% opacity. Show result on terrain toggles this overlay. It uses the same
classified cells as the north-up map. DEM triangles are clipped to cell edges,
then raised 0.15 m to avoid depth flicker; no geometry is created over missing
DEM triangles. Pending and outside cells stay uncolored. Overlay preparation
runs incrementally after calculation or pause; changing settings, clearing or
resuming removes the previous overlay. This example uses the otherwise unused
model layer for this display geometry. Vertical exaggeration follows the DEM.

The north-up map remains available. Its center cross is the observer. The radius label describes the
sampling domain; squares depict sample locations, not continuous visibility.
The panel reports counts, actual grid spacing and maximum ray interval used.

Grid sizes are 17, 33 or 65 samples per side; radius is 10–2000 m. Requested
ray spacing is 1–50 m. The SDK ray interval cap may coarsen this spacing.
The GUI timer performs at most 256 work steps per tick, preserving navigation
and permitting pause. No background thread accesses the viewer controller.
Scene replacement clears observer and analysis. Exaggeration changes only
the 3D display, never physical ENU visibility calculations.

## Interpretation

Analysis uses the loaded DEM mesh (512 samples per side), not buildings,
vegetation or a surveyed city model. Small obstacles between samples can be
missed. Atmospheric refraction is excluded. Missing target terrain is unknown;
missing intermediate samples are unknown unless another sampled obstruction
establishes blockage. The vertical reference remains an unverified preview.
No raster export is included. A finer grid cannot add detail absent in the DEM.

## Build

Configure with the windows-msvc preset and build windows-msvc-release,
supplying Qt and optionally GEOKERNEL_ROOT as in the other examples.
Executable: outputs/windows/Release/bin/ViewshedAnalysis.exe.
