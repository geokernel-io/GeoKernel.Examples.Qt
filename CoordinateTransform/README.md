# CoordinateTransform

CoordinateTransform demonstrates how to transform coordinates between spatial reference systems with GeoKernel.

## Overview

It prepares the world boundary sample data, loads the layer in EPSG:4326, and tracks the mouse position over the map. Each longitude/latitude coordinate is transformed to EPSG:3857 Web Mercator meters with `CoordinateTransformer`, and both coordinate values are displayed live in the status bar.

## GIS Workflow

This example shows how to:

- Download and load vector sample data
- Assign EPSG:4326 to a vector layer and viewer
- Create source and target coordinate systems from EPSG codes
- Construct a `CoordinateTransformer`
- Transform longitude and latitude to Web Mercator meters
- Read map coordinates from mouse movement
- Display source and transformed coordinates interactively
- Return the viewer to the full world extent
