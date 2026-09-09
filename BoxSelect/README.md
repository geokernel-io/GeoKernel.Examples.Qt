# BoxSelect

BoxSelect demonstrates how to select multiple vector features with a screen-space rectangle in GeoKernel.

## Overview

It prepares world boundaries, USA states, and city sample data, loads the layers with distinct styles, and activates the box selection tool. Dragging a rectangle selects every intersecting feature, while keyboard modifiers let the user add to or toggle the current selection. The selected results and their attributes are displayed in side panels.

## GIS Workflow

This example shows how to:

- Download and load multiple vector sample layers
- Activate rectangle-based feature selection
- Query features inside a screen rectangle
- Replace, extend, or toggle the current selection
- Highlight selected features across multiple layers
- List selected layer and feature identifiers
- Display geometry and attribute details
- Clear the current selection
