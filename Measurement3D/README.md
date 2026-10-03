# Measurement3D

**Example 08:** measure a polyline or polygon by clicking the terrain.

## Usage

1. The DEM and orthophoto download and open automatically. The initial viewport
   stays on a loading page until the real scene is ready.
2. Choose Distance / polyline or Area / polygon.
3. With Add points on click enabled, click the terrain. A visible measurement
   polyline joins the points; an area boundary closes after three valid points.
4. Finish measurement disables point entry and retains the result and overlay.
   Re-enable Add points to continue.
5. Undo last point / Backspace removes the last point. Clear / Esc starts over.
   Switching measurement mode clears the current measurement.

Dragging continues to navigate. Sky and NoData clicks do not add points.
A measurement supports at most 64 points. Duplicate successive clicks within
1 mm are ignored. Self-intersecting or degenerate polygons show an error and an
open boundary; undo or clear to correct them.

## Results and units

- Horizontal length: sum of local ENU XY distances between consecutive points.
- 3D segment total: sum of straight 3D distances between picked points; not a
  terrain-following profile length.
- End-to-start delta Up: signed local ENU elevation difference for polylines.
- Plan area: area of the polygon projected onto the local ENU horizontal plane.
- Horizontal perimeter: closed perimeter only for a valid area polygon.
- Point table: local East, North and Up in metres, not longitude/latitude or
  absolute orthometric elevation.

These are local Cartesian measurements, not geodesic measurements. Area does
not include terrain slope/surface area. The sample DEM datum is unverified;
results are a demo preview, not survey-certified measurements.
Vertical exaggeration affects the display only: clicked render positions are
converted back to physical ENU metres before calculation or overlay storage.

## SDK integration

addTerrainQueryClickedHandler queues terrainPointAt on the GUI thread after the
notification returns. SceneReference::fromRender removes render scale and
vertical exaggeration. setMeasurementLine draws the physical ENU points in the
current reference. planArea validates and computes simple concave or convex
rings. Reloading a scene clears the measurement; failed loading retains it.

## Build

Uses the other Qt examples' standard SDK acquisition, checksum verification,
CMake presets and runtime copying. From an x64 Visual Studio developer terminal:

```bat
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build --preset windows-msvc-release
```

A SDK exporting GeoKernel::Viewer3D is required. The inspected public v1.5.27
Windows SDK does not contain it. Until a 3D SDK is published, add
`-DGEOKERNEL_ROOT=C:/path/to/3d-sdk -UGeoKernel_DIR` when configuring.
Use `-UGEOKERNEL_ROOT -UGeoKernel_DIR` to return to automatic acquisition.
Output: `outputs/windows/Release/bin/Measurement3D.exe`.

## Sample data

- [Terrain](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_terrain.zip)
- [Orthophoto](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_ortophoto.zip)

Files are cached under `data` next to the executable. No file selection is needed.
Downloads can be cancelled and retried. The executable directory must be writable.

## Validation status

Source/API review and whitespace checks completed. Build and application execution
have not been performed. Manual acceptance should cover picking with 1x and 5x
exaggeration, a concave area, a crossing boundary, undo/clear, reload and NoData clicks.
