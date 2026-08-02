# ZoomToSelection

ZoomToSelection demonstrates how to navigate the map directly to the combined extent of selected vector features in GeoKernel.

## Overview

It prepares world boundaries, USA states, and city sample data, loads the layers with distinct styles, and lets the user build a multi-layer selection by clicking features on the map. The selected feature extent is displayed in the toolbar, and a dedicated action zooms the viewer to the complete selection while preserving all selected items.

## GIS Workflow

This example shows how to:

- Download and load multiple vector sample layers
- Select features with map hit testing
- Add or toggle features in a multi-layer selection
- Read the combined selected feature extent
- Display selection extent coordinates
- Zoom the viewer to all selected features
- Clear the current selection
- Return the map to its full extent
