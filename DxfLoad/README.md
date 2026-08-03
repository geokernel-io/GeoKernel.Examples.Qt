# DxfLoad

DxfLoad demonstrates how to download, open, and inspect CAD DXF vector data with GeoKernel.

## Overview

It prepares the required `geog_25000` sample data from `geog_25000_dxf.zip`, opens the `.dxf` file with `GisLayerDXF`, and displays supported CAD entities in a GeoKernel viewer. A side panel reports layer and file metadata, lists the generated attribute schema, and shows sample attribute values for the imported DXF features.

## GIS Workflow

This example shows how to:

- Download and extract remote DXF sample data
- Open a CAD DXF file with `GisLayerDXF`
- Parse supported DXF entities into vector features
- Apply a simple vector layer style
- Display the loaded layer in a map viewer
- Inspect geometry type, feature count, and extent
- Read generated DXF attribute definitions
- Display feature attribute values
- Inspect DXF file metadata
- Zoom the viewer to the loaded layer extent
