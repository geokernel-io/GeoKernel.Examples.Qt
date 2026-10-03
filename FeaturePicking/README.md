# FeaturePicking

**Example 07:** click buildings and roads to inspect their source attributes.

## Behavior

- Automatically downloads and opens DEM, orthophoto, roads and building footprints.
- Displays a loading page until the real scene is installed.
- Click a visible building or road to select it using the SDK's native picking.
- Shows layer, feature ID, source FID and all source attributes in a read-only table.
- Multiple selection lets successive clicks add or remove features. The selected
  feature list activates an existing selection for inspection.
- Zoom to selection frames the selection; Clear selection or Esc clears it.
- Buildings, roads and orthophoto can be hidden separately.
- Vertical exaggeration ranges from 0.25 to 10, initially 1.

Building heights are illustrative (9 m); they are not measured source attributes.
The DEM datum remains an unverified preview in this example. This example teaches
feature inspection, not height calibration. A driver FID is distinct from osm_id;
both are displayed under their own names when available.

## Build

Uses the same CMake presets, checksum-verified SDK download and runtime copying
as the other Qt examples. From an x64 Visual Studio developer command prompt:

```bat
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build --preset windows-msvc-release
```

The public SDK must export GeoKernel::Viewer3D. The inspected v1.5.27 public
Windows SDK does not include it. Until a 3D SDK is published, configure with
`-DGEOKERNEL_ROOT=C:/path/to/3d-sdk -UGeoKernel_DIR` to use a local SDK.
To return to automatic acquisition use `-UGEOKERNEL_ROOT -UGeoKernel_DIR`.

Output: `outputs/windows/Release/bin/FeaturePicking.exe`.
Qt and a Vulkan-capable graphics driver are required.

## Data

Sample packages are automatically cached under `data` beside the executable:

- [Terrain](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_terrain.zip)
- [Orthophoto](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_ortophoto.zip)
- [Roads](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_roads.zip)
- [Buildings](https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_buildings.zip)

Keep SHP, SHX, DBF, PRJ and CPG companions together. The executable directory
must be writable. Download cancellation and retry are supported. Reload prepares
the next scene on a worker and preserves the old scene if preparation fails.

## SDK integration

addSelectionChangedHandler queues a UI refresh after the SDK notification returns.
vectorSelection retains the selected scene snapshot; layerId and featureId resolve
source attributes. Scene-local range indices are only kept in the current selected
list and are replaced after scene changes. activateSelection, focusSelection,
clearSelection and setMultipleSelectionEnabled implement the selection controls.

## Validation status

Source/API and configuration review completed. No build or application was run.
An optional `--smoke-report C:/temp/picking.json` checks programmatic selection,
attribute table population, clearing and nonempty road/building geometry.
It does not replace a manual mouse-picking test.
