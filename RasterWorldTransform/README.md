# RasterWorldTransform

RasterWorldTransform demonstrates how to convert between raster pixel coordinates and geographic map coordinates in GeoKernel.

## Overview

It prepares the required world GeoTIFF sample data, loads `world_8km.tif`, and reads its `GisRasterWorldTransform`. Pixel controls convert a selected raster column and row into world coordinates, while clicking the map performs the inverse world-to-pixel calculation. A marker layer visualizes the selected position and the details panel shows the transform coefficients and conversion results.

## GIS Workflow

This example shows how to:

- Download and load remote GeoTIFF sample data
- Read a raster world transform
- Convert pixel coordinates to world coordinates
- Convert world coordinates back to raster pixels
- Inspect pixel size, origin, and rotation coefficients
- Select raster positions from numeric controls
- Pick world positions interactively from the map
- Visualize the converted position with an in-memory marker
