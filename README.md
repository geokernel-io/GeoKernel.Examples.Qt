# GeoKernel Qt Examples

Official Qt/C++ examples for the GeoKernel native GIS SDK.

## Requirements

- Windows 10 or later
- Qt 6 with MSVC
- CMake and Ninja
- A valid GeoKernel license

## Getting Started

Choose an example directory and follow its included build scripts or CMake configuration.

## Resources

- [GeoKernel Website](https://geokernel.io)
- [Documentation](https://docs.geokernel.io)
- [Sample Data](https://github.com/geokernel-io/GeoKernel.SampleData)

## Support

For questions, contact [admin@geokernel.io](mailto:admin@geokernel.io).

## 3D examples

1. [TerrainLoading](TerrainLoading/README.md): DEM loading, live color ramps, a relief legend and vertical exaggeration.
2. [TerrainAndImagery](TerrainAndImagery/README.md): load a DEM, add an optional orthophoto, and navigate the terrain.
3. [CameraNavigation](CameraNavigation/README.md): camera presets, smooth transitions, saved views and navigation controls.
4. [RoadsOnTerrain](RoadsOnTerrain/README.md): drape roads on terrain and adjust road visibility, color and opacity.
5. [Buildings3D](Buildings3D/README.md): extrude building footprints on terrain with adjustable height, color, opacity and visibility.
6. [ModelPlacement](ModelPlacement/README.md): place a textured model with longitude, latitude, terrain offset, orientation and scale controls.
7. [FeaturePicking](FeaturePicking/README.md): select buildings and roads, inspect attributes, and focus or clear selections.
8. [Measurement3D](Measurement3D/README.md): measure terrain point polylines and horizontal polygon areas with visible measurement lines.
9. [Tiles3DStreaming](Tiles3DStreaming/README.md): stream local 3D Tiles over terrain with camera-driven detail, geometry budget and live streaming diagnostics.
10. [PointCloudViewer](PointCloudViewer/README.md): automatically load an RGB LAZ sample, switch RGB/height colours and choose preview sampling density.
11. [TerrainAnalysis](TerrainAnalysis/README.md): DEM slope and aspect display with matching legends, mesh resolution and vertical exaggeration.
12. [ElevationProfile](ElevationProfile/README.md): draw a terrain route and view sampled distance/elevation, ascent/descent and missing-data gaps.
13. [ViewshedAnalysis](ViewshedAnalysis/README.md): choose an observer, adjust heights/radius and calculate a north-up terrain visibility grid with pause/resume.
14. [LayerStylingAndFiltering](LayerStylingAndFiltering/README.md): per-layer visibility, opacity, uniform/category colors and attribute filters for roads and buildings.
15. [ProjectSaveLoad](ProjectSaveLoad/README.md): save and reopen local scenes with source paths, camera, layer styles and attribute filters.
16. [GlobeNavigation](GlobeNavigation/README.md): full-globe navigation, coordinate/preset jumps, live camera values and a session bookmark.

### Published SDK builds for 3D examples

The 15 Viewer3D examples above use the latest published SDK through the same
checksum-verified downloader as HelloMap. Their standard presets reset a previous
local `GEOKERNEL_ROOT` override; package discovery is restricted to the acquired
SDK so a stale `GeoKernel_DIR` cannot select an older installation.
An explicit command-line `-DGEOKERNEL_ROOT=...` can still select a development SDK.
GlobeNavigation work is currently paused and is excluded from this change.

After publishing a release containing Viewer3D, configure and build from an x64
Visual Studio developer terminal, inside the desired example directory:

```bat
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build --preset windows-msvc-release
```

Reconfigure after each new release to resolve the latest SDK. Existing outputs
are not updated until rebuilt. Sample data is downloaded by examples that need it;
Release download/build validation must be performed
after the new SDK has been published.

## Local demos

- [Terrain3D_Demo](Terrain3D_Demo/README.md): Anitkabir DEM terrain with ECW imagery.
