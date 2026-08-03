# RasterOverview

RasterOverview demonstrates how raster overview pyramids improve zoomed-out rendering performance in GeoKernel.

## Overview

It prepares the required world GeoTIFF sample data, creates a writable working copy, and loads the raster with or without overview generation. The example reports overview files, pyramid levels, load progress, and provider metadata, then runs a repeated downsample benchmark so the two loading modes can be compared directly.

## GIS Workflow

This example shows how to:

- Download and prepare remote GeoTIFF sample data
- Create a writable raster working copy
- Load a raster with overview generation disabled
- Build raster overview pyramids during loading
- Configure overview thresholds and resampling
- Inspect generated overview files and pyramid levels
- Benchmark zoomed-out raster reads
- Compare rendering performance with and without overviews
