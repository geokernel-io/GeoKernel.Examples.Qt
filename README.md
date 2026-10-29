# GeoKernel Qt Examples

Official Qt/C++ examples for the GeoKernel native GIS SDK.

- [BuildingChangeReview](BuildingChangeReview/README.md): two-date Almere / Oosterwold imagery,
  local HOTOSM building inference, synchronized maps, review and vertex correction,
  and approved GeoJSON export with source/model provenance. Sample/model assets
  are acquired automatically and SHA-256 verified.

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

## Globe examples

- [GlobeHello](GlobeHello/README.md): Interactive globe with atmosphere, Esri imagery, streamed DEM and lighting controls. Layers start disabled and can be enabled from the toolbar.
- [GlobeTerrainNavigation](GlobeTerrainNavigation/README.md): Barcelona globe with terrain, Esri imagery, atmosphere, lighting and camera navigation.
- [GlobeVectorStyling](GlobeVectorStyling/README.md): Sagrada Familia building footprints with fill, categorical colors, opacity, labels and identification.
- [GlobeBuildings](GlobeBuildings/README.md): Sagrada Familia footprints. Use Extrude to enable extrusion and adjust building height and colors.
- [GlobeModelPlacement](GlobeModelPlacement/README.md): Textured Sagrada Familia model with position, rotation, scale and height controls.
- [GlobeRasterServices](GlobeRasterServices/README.md): Sagrada Familia orthophoto and DEM enabled at startup, with Esri background and atmosphere.
- [GlobeTerrainAnalysis](GlobeTerrainAnalysis/README.md): Barcelona terrain analysis: select Line of sight, Viewshed, Slope / aspect or Measure, then click the globe to choose analysis points. Results appear in a fixed-height bottom panel.

These standalone examples use the HelloMap SDK download/checksum workflow and
the `windows-msvc` / `windows-msvc-release` presets. Required sample archives are
downloaded and verified automatically. Binaries and data are placed in
`outputs/windows/Release/bin`. Qt 6.11.0, Vulkan and a Windows SDK exporting
`GeoKernel::Globe` are required. Published SDK 1.5.34 includes Globe; configure selects the latest release automatically.

## SAM3 AI example

- [Sam3SegmentationInference](Sam3SegmentationInference/README.md): automatically prepare pinned SAM3 text/point/box models, generate a draft overlay in GisViewer, accept/discard and export source-aligned GeoTIFF/GeoPackage outputs. Requires a SDK release containing the new native SAM3 APIs and model assets in the SampleData v1 release.

- [HotosmBuildingFootprints](HotosmBuildingFootprints/README.md): HOTOSM DINOv3-S building footprints on Barafu, Dallu and Stone Town imagery; automatic SDK/model/data downloads, Left/Right dataset navigation, cancellable inference and georeferenced label/GeoJSON outputs.

- [GeoAI USA Mask R-CNN building footprints](GeoAiUsaBuildingFootprints/README.md): automatic SDK/model/sample acquisition, native RGB inference, session-only overlays and explicit Shapefile export.

- [DynamicWorldInference](DynamicWorldInference/README.md): nine-class Sentinel-2 L1C land-cover classification with automatic SDK/model/data preparation, session-only colored overlays and explicit Shapefile export.

## PrithviFloodAnalysis

[Prithvi flood analysis](PrithviFloodAnalysis/README.md): automatic SDK/model/sample acquisition, native six-band Sentinel-2 water/flood segmentation with explicit geographic location, session-only overlay, transparency control and Shapefile export.

## PrueFieldBoundaries

[PRUE field boundaries](PrueFieldBoundaries/README.md): automatic SDK/R2-model/upstream-raster acquisition, eight-band two-period Sentinel-2 field segmentation, session-only overlay, transparency slider, cancellation and Shapefile export.

## Sen2srSuperResolution

[SEN2SRLite super-resolution](Sen2srSuperResolution/README.md): automatic SDK/model/raster acquisition, full RGBN x4 model with Fourier constraint, synchronized before/after swipe, Cancel and explicit AI-tagged Float32 GeoTIFF export.

## GeoAI SDK baseline —1.5.37

Sam3SegmentationInference, HotosmBuildingFootprints, GeoAiUsaBuildingFootprints, DynamicWorldInference, PrithviFloodAnalysis, PrueFieldBoundaries and Sen2srSuperResolution require the public SDK1.5.37 or newer. Their default acquisition remains latest; source builds from a local SAM3 SDK are no longer necessary. See each README for commands to replace a stale local SDK cache. CUDA remains an optional GPU runtime deployment, independent of the model ZIPs and the standard CPU SDK runtime.
