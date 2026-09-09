# WebMercator

WebMercator demonstrates how to configure a GeoKernel viewer for the EPSG:3857 Web Mercator coordinate system.

## Overview

It prepares world boundary data in EPSG:4326, loads the layer into a viewer configured for EPSG:3857, and displays the source layer reprojected in Web Mercator. The example presents the coordinate system metadata, uses the full Web Mercator world extent, and reports mouse coordinates in projected meters.

## GIS Workflow

This example shows how to:

- Download and load vector sample data
- Create the EPSG:3857 coordinate system
- Assign EPSG:4326 to the source vector layer
- Configure a viewer to use Web Mercator
- Reproject geographic data during rendering
- Use the Web Mercator full world extent
- Inspect projected coordinates in meters
- Navigate an interactive projected world map
