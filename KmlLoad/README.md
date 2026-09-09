# KmlLoad

KmlLoad demonstrates how to download, open, and inspect KML/KMZ vector data with GeoKernel.

## Overview

It prepares the required travel sample data from `travel.zip`, extracts the KML document contained in `travel.kmz`, opens it with `GisLayerKML`, and displays its features in a GeoKernel viewer. A side panel reports layer and file metadata, lists the available attribute schema, and shows sample attribute values so the imported KML/KMZ content can be inspected interactively.

## GIS Workflow

This example shows how to:

- Download and extract remote KML/KMZ sample data
- Extract a KML document from a KMZ archive
- Open a KML dataset with `GisLayerKML`
- Apply a simple vector layer style
- Display the loaded layer in a map viewer
- Inspect geometry type, feature count, and extent
- Read KML attribute definitions
- Display feature attribute values
- Inspect the loaded KMZ file metadata
- Zoom the viewer to the loaded layer extent
