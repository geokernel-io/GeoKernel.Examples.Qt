# GetCrossings

GetCrossings demonstrates how to find every intersection point between two polyline geometries with GeoKernel.

## Overview

It creates two zigzag polylines entirely in memory and arranges their segments to cross multiple times. Running `GisTopology::GetCrossings` returns the crossing coordinates, which are displayed as labeled point overlays. A details panel reports the source vertex counts and extents together with the number and coordinates of all detected crossings.

## GIS Workflow

This example shows how to:

- Create polyline geometries in memory
- Build multipart-ready lines from ordered vertices
- Display and label source polylines with distinct styles
- Initialize the GeoKernel topology engine
- Find all crossings between two line geometries
- Inspect returned intersection coordinates
- Add labeled point overlays for topology results
- Compare source extents and crossing counts
