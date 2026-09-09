# InfoTool

InfoTool demonstrates how to inspect map clicks and identify the top-most vector feature with GeoKernel’s information tool.

## Overview

It prepares world boundaries, USA states, and city sample data, loads the layers with distinct styles, and activates `GisViewerTool::Info`. Each map click reports both screen and world coordinates, performs a feature hit test, highlights the identified feature, and displays its layer, geometry, extent, and attribute values in a side table.

## GIS Workflow

This example shows how to:

- Download and load multiple vector sample layers
- Activate the viewer information tool
- Handle map click events
- Read screen and world click coordinates
- Hit test the top-most feature at a screen position
- Highlight the identified feature
- Display geometry and extent information
- Inspect feature attribute fields and values
