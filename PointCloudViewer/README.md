# PointCloudViewer

Example 11: view the EuroSDR P4 RGB point-cloud crop without a DEM.

CMake uses the shared SDK acquisition/runtime helpers. With no GEOKERNEL_ROOT,
it acquires the latest public SDK; that SDK must export GeoKernel::Viewer3D.
The existing local 3D SDK can be supplied while public packaging is pending.

At first launch, the sample downloads automatically from:
https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/eurosdr_p4_pointcloud.zip

The LAZ is extracted below data next to the executable and opens automatically.
Subsequent launches reuse the cached file. A loading page is shown until data
is ready; there is no initial synthetic terrain scene.

Controls: RGB or height colouring; sample limits of 50,000, 100,000 or 250,000
points (press Load / apply sample limit); cancel loading; reset camera.
Left drag pans, wheel/right drag zooms, middle/Ctrl+left orbits, Shift+left looks.

The source contains 2,221,954 points in a 40 x 40 metre crop. It is photogrammetric,
unfiltered and provisionally georeferenced to EPSG:32630, with unverified vertical
reference. This example uses the SDK local normalized point preview, not a
georeferenced terrain overlay or COPC streaming. The file remains unchanged.
RGB was normalized to LAS 16-bit range during sample preparation. Intensity and
classification controls are intentionally omitted because this reconstruction
is not a classified LiDAR dataset. The current SDK fixes point size at 1 pixel
and supports at most 250,000 preview points; the panel states this limit.

Windows build: configure with CMakePresets.json windows-msvc and build the
windows-msvc-release preset, supplying Qt and optionally GEOKERNEL_ROOT.
Output: outputs/windows/Release/bin/PointCloudViewer.exe.

Release build passed. Interactive rendering and first-download acceptance remain
to be checked; the application was not launched during preparation.
