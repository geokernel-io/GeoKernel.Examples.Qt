# ShapefileLoad

ShapefileLoad demonstrates how to download, open, and inspect an ESRI Shapefile with GeoKernel.

## Overview

It prepares the required world boundary sample data, opens the `.shp` file with `GisLayerSHP`, and displays the polygon layer in a GeoKernel viewer. A side panel reports layer metadata and shapefile sidecar sizes, lists the attribute schema, and shows values from the first features so both geometry and tabular content can be inspected together.

## GIS Workflow

This example shows how to:

- Download and prepare remote shapefile sample data
- Open an ESRI Shapefile with `GisLayerSHP`
- Apply a simple polygon layer style
- Display the loaded layer in a map viewer
- Inspect geometry type, feature count, and extent
- Read shapefile attribute definitions
- Display feature attribute values
- Inspect `.shp`, `.shx`, and `.dbf` sidecar files
- Zoom the viewer to the loaded layer extent
