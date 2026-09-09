# ShapefileSaveAs

ShapefileSaveAs demonstrates how to export a loaded vector layer as a new ESRI Shapefile with GeoKernel.

## Overview

It prepares the required world boundary sample data, opens the source shapefile with `GisLayerSHP`, and saves a copy to a writable application output directory. The generated shapefile is reopened to verify the result, while progress, output sidecar files, layer metadata, and sample attribute values are displayed in the interface.

## GIS Workflow

This example shows how to:

- Download and prepare remote shapefile sample data
- Open an ESRI Shapefile with `GisLayerSHP`
- Export a vector layer with `saveAs`
- Report shapefile writing progress
- Replace an existing shapefile and its sidecar files
- Reopen the exported shapefile for validation
- Inspect generated `.shp`, `.shx`, `.dbf`, and projection files
- Compare source and output layer metadata
- Display attributes from the saved layer
