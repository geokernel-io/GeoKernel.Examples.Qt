# ElevationProfile

Example 13: draw a terrain route and inspect its distance/elevation profile.

The standard CMake helpers acquire the latest public GeoKernel SDK unless
GEOKERNEL_ROOT is provided. Requires GeoKernel::Viewer3D with TerrainProfile
and terrainRenderHeight APIs. Use the local 3D SDK until public packaging
includes these APIs.

On first launch, the standard sample downloader fetches these release assets:
- https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_terrain.zip
- https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/sagrada_familia_ortophoto.zip

Files are cached under data next to the executable and open automatically.
A loading page remains visible until the real terrain is ready.

With Add points enabled, click two or more terrain positions. The route is
shown as a polyline joining the picked points. Finish route disables capture;
re-enable Add points to extend it. Undo removes the last vertex; Clear resets
both route and chart. Dragging still navigates. Reset view resets the camera.

The SDK plans samples along the horizontal ENU route and queries the loaded
DEM mesh incrementally (32 queries per timer tick). Requested spacing is
1–100 metres; the 4096-sample cap may coarsen it. The panel reports actual
maximum interval, sample count, horizontal distance, ascent and descent.
At most 64 route vertices are accepted. Changing spacing recomputes the profile.
Missing samples break the graph and do not contribute to ascent/descent.
The overlay joins clicked vertices; the chart samples the intervening terrain.

Heights are source-datum metres with an unverified vertical reference, not
survey-verified absolute heights. No geoid conversion is applied. The mesh
uses 512 samples per side; tighter profile spacing does not increase DEM
accuracy. Ascent/descent depend on sampling. Vertical exaggeration changes
only the 3D display, not the profile. No raster or CSV export is included.

Build with the windows-msvc / windows-msvc-release presets, supplying Qt
and optionally GEOKERNEL_ROOT. Executable:
outputs/windows/Release/bin/ElevationProfile.exe.

Release compilation passed using the local 3D SDK. Interactive rendering and
first-download acceptance have not been run during preparation.
