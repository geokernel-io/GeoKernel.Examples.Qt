# MifLoad

MifLoad demonstrates how to download, open, and inspect a MapInfo Interchange Format dataset with GeoKernel.

## Overview

It prepares the required Albania sample data from `albania.zip`, opens the `.mif` dataset with `GisLayerMIF`, and displays the loaded vector features in a GeoKernel viewer. A side panel reports layer metadata and MIF/MID sidecar information, lists the attribute schema, and shows values from the first features so geometry and tabular content can be inspected together.

## GIS Workflow

This example shows how to:

- Download and extract remote MapInfo MIF sample data
- Open a MapInfo Interchange Format dataset with `GisLayerMIF`
- Apply a simple vector layer style
- Display the loaded layer in a map viewer
- Inspect geometry type, feature count, and extent
- Read MIF attribute definitions
- Display feature attribute values
- Inspect `.mif` and `.mid` companion files
- Zoom the viewer to the loaded layer extent
