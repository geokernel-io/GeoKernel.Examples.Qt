# RasterTileCache

RasterTileCache demonstrates how an in-memory raster tile cache affects repeated raster reads in GeoKernel.

## Overview

It prepares the required world GeoTIFF sample data and loads the same raster with caching disabled, a small cache budget, or a larger cache budget. The example reports cache capacity and usage statistics, runs the same tile requests twice, and compares elapsed time and cache hits between the first and second passes.

## GIS Workflow

This example shows how to:

- Download and load remote GeoTIFF sample data
- Enable or disable the raster tile cache
- Configure cache pixel and item budgets
- Inspect raster tile cache statistics
- Clear the in-memory tile cache
- Run repeated raster tile requests
- Compare first-pass and cached read performance
- Observe how cache size affects retained tiles
