# Tiles3DStreaming

Example 10: camera-driven 3D Tiles streaming over terrain and orthophoto.

The example follows the other Qt examples: CMake acquires the latest SDK unless
GEOKERNEL_ROOT is supplied. On first launch it downloads and extracts the terrain,
orthophoto and sagrada_familia_3d_tiles.zip from GeoKernel.SampleData release v1,
plus the official ICGC EGM08D595 grid. Subsequent launches reuse local data beside
the executable. No dataset selection is needed. A loading page hides the initial
synthetic terrain until the real scene is ready.

## Controls

- Move the camera to update tile selection automatically.
- Start / apply budget restarts streaming with a 64–128 MiB CPU geometry batch budget.
- Stop streaming retains current geometry; it can wait for in-flight work.
- Hide buildings clears their model; showing them restarts streaming.
- Focus loaded tiles frames current geometry, not necessarily the whole tileset.
- The panel reports tile selection, deferred tiles, CPU cache and GPU upload state.

The archive must contain tileset.json and all referenced content. It is downloaded
in full before local streaming starts; this is not an HTTP tile streaming demo.
The loading preflight rejects missing or non-local content references. Preserve
relative paths when packaging the archive.

Terrain exaggeration stays at 1x as required by the SDK tileset streamer. The DEM
is orthometric and uses the EGM08D595 correction. Building/terrain alignment is
approximate, not independently surveyed. Cache statistics are not total process
RAM or device VRAM. GPU upload pending and draw counts do not prove source completeness.

## Build

Use CMakePresets.json as in the other examples. Supply Qt through CMAKE_PREFIX_PATH
or QTDIR. The SDK must export GeoKernel::Viewer3D; a 2D-only SDK is rejected with a
clear configure error. GEOKERNEL_ROOT can point to an extracted local 3D SDK while
the updated release is being prepared.

Windows output: outputs/windows/Release/bin/Tiles3DStreaming.exe.

This example has not yet been built or run; runtime acceptance is pending.

The updated SDK replaces the old 8-tile/16-content caps with 256/512 safety limits.
Geometry and atlas admission still enforce byte budgets. A partial batch is never
committed by the streamer: it coarsens the subtree with the lowest projected error
first, preserving higher priority nearby detail within the budget and retaining
the previous model if none fits. Reduced detail is reported in the panel.
Rebuild and install the modified SDK before rebuilding this example; the existing
local SDK binaries do not pick up source changes automatically.
