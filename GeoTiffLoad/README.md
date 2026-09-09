# GeoTiffLoad

GeoTiffLoad demonstrates how to download, open, and display a GeoTIFF raster layer in GeoKernel.

## Overview

It prepares the required world raster sample data, loads `world_8km.tif` as a GeoTIFF layer, and displays its raster metadata in a side panel. The example reports the source file, raster dimensions, band count, coordinate system, geographic extent, world transform, and overview count while providing standard map navigation tools.

## GIS Workflow

This example shows how to:

- Download and extract remote GeoTIFF sample data
- Create a `GisLayerTIFF` from a raster file
- Open and add a GeoTIFF layer to the viewer
- Read raster dimensions and band information
- Inspect coordinate system and extent metadata
- Check world transform and raster overview information
- Zoom the viewer to the loaded raster extent
- Navigate a georeferenced raster map
