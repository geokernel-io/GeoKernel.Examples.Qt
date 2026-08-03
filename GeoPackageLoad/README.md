# GeoPackageLoad

GeoPackageLoad demonstrates how to download, open, and inspect a GeoPackage vector dataset with GeoKernel.

## Overview

It prepares the required detailed Europe sample data from `europe_detailed.zip`, opens the `.gpkg` dataset with `GisLayerGPKG`, and displays the loaded vector features in a GeoKernel viewer. A side panel reports layer and file metadata, lists the attribute schema, and shows values from the first features so GeoPackage geometry and tabular content can be inspected together.

## GIS Workflow

This example shows how to:

- Download and extract remote GeoPackage sample data
- Open a GeoPackage dataset with `GisLayerGPKG`
- Apply a simple vector layer style
- Display the loaded layer in a map viewer
- Inspect geometry type, feature count, and extent
- Read GeoPackage attribute definitions
- Display feature attribute values
- Inspect GeoPackage file metadata
- Zoom the viewer to the loaded layer extent
