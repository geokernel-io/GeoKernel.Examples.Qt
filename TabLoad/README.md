# TabLoad

TabLoad demonstrates how to download, open, and inspect a MapInfo TAB dataset with GeoKernel.

## Overview

It prepares the required Paris sample data from `paris_tab.zip`, opens the `.tab` dataset with `GisLayerTAB`, and displays the loaded vector features in a GeoKernel viewer. A side panel reports layer metadata and MapInfo sidecar files, lists the attribute schema, and shows values from the first features so geometry and tabular data can be inspected together.

## GIS Workflow

This example shows how to:

- Download and extract remote MapInfo TAB sample data
- Open a MapInfo TAB dataset with `GisLayerTAB`
- Apply a simple vector layer style
- Display the loaded layer in a map viewer
- Inspect geometry type, feature count, and extent
- Read TAB attribute definitions
- Display feature attribute values
- Inspect `.tab`, `.map`, `.dat`, `.id`, and `.dbf` sidecar files
- Zoom the viewer to the loaded layer extent
