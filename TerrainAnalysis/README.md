# TerrainAnalysis

Example 12: display DEM terrain relief, slope and aspect using Viewer3D.

CMake acquires the latest public SDK when GEOKERNEL_ROOT is not supplied.
The SDK must export GeoKernel::Viewer3D and TerrainDisplayMode. Until the
3D SDK is published, configure with an existing local 3D SDK root.

At first launch, the example downloads and extracts:
https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_terrain.zip

The DEM opens automatically, with a loading page until the real scene is ready.
Files are cached under data next to the executable. Reload reuses this cache.
No orthophoto, roads or buildings are loaded.

Choose terrain relief, slope or aspect. Legends match the SDK shader palette:
slope bins are [0,5), [5,15), [15,30), [30,45), [45,90] degrees; aspect
has eight 45-degree sectors centered on compass directions and gray for
slopes below 0.01 degrees. Aspect is downhill, clockwise from local ENU north.

Analysis is face-based visualization of the sampled mesh, not a GIS raster
analysis export. Choose 256, 512 or 1024 samples per side, then Reload sample
to apply. Results depend on mesh resolution. Vertical exaggeration (0.25–10)
changes display geometry but not slope/aspect colors or source elevations.
The source vertical datum remains unverified; no vertical conversion is applied.
Terrain relief colors show relative ENU height, not absolute elevation.

Reset view restores the camera. Left drag pans, wheel/right drag zooms,
middle/Ctrl+left orbits, and Shift+left looks. Loading can be cancelled.

Build using the windows-msvc and windows-msvc-release CMake presets,
supplying Qt and optionally GEOKERNEL_ROOT. Output:
outputs/windows/Release/bin/TerrainAnalysis.exe.
