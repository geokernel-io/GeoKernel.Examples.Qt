# ProjectSaveLoad

Example 16: save and reopen a local terrain project with camera and layer settings.

Uses the standard SDK acquisition/runtime helpers. With no GEOKERNEL_ROOT,
CMake acquires the latest public SDK. GeoKernel::Viewer3D with Project I/O is
required; use a local 3D SDK until the public package provides it.

First launch automatically downloads the terrain, orthophoto, roads and
buildings archives from the GeoKernel.SampleData v1 release and opens them:
https://github.com/geokernel-io/GeoKernel.SampleData/releases/tag/v1
Files are cached under data next to the executable. No synthetic scene is shown.

Use the right panel to change visibility, opacity, uniform/category colors and
attribute filters. Navigate the camera or adjust vertical exaggeration, then
choose Save (Ctrl+S) or Save as. Open project restores these settings after
loading its local source assets. The toolbar uses .gk3d files containing the
SDK GeoKernel3D JSON format; Open also accepts .json project files.

Persisted: four source paths and identities, placement/DEM settings, camera,
exaggeration, imagery/road/building visibility and opacity, vector colors,
attribute filters and category theme fields. The SDK writes atomically using
QSaveFile. Paths are relative to the project directory where possible.

Projects reference data; they are not self-contained packages. Retain the
cached data and shapefile companion files when moving/sharing projects.
Missing primary sources are reported before replacing the current scene.
Load errors or cancellation retain the previous scene. Reload sample returns
to an unsaved sample scene. Save before opening/reloading: no dirty-state
tracking or automatic save is provided by this introductory example.

This example handles local DEM/imagery/roads/buildings projects. Projects with
models, point clouds, streamed tiles, globe sources, time series, saved analyses
or orthometric geoid conversion are rejected explicitly rather than silently
losing unsupported content. Source data is never modified. Building heights
are illustrative (9 m); the sample vertical datum is unverified.

Build with windows-msvc / windows-msvc-release presets, supplying Qt and
optionally GEOKERNEL_ROOT. Executable:
outputs/windows/Release/bin/ProjectSaveLoad.exe.
