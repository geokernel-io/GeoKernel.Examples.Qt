# SelectionBoxSignal

SelectionBoxSignal demonstrates how to observe and process completed box-selection gestures in a GeoKernel viewer.

## Overview

It prepares world boundaries, USA states, and city sample data, loads the layers with distinct styles, and connects to the viewer's `mapSelectionBoxFinished` signal. Each completed rectangle reports its screen bounds, geographic extent, and keyboard modifiers, then queries and highlights the intersecting features while recording the event in a timestamped log.

## GIS Workflow

This example shows how to:

- Download and load multiple vector sample layers
- Activate rectangle-based feature selection
- Connect to the completed selection box signal
- Read screen rectangle and world extent values
- Inspect keyboard modifiers for selection behavior
- Query features intersecting a screen rectangle
- Replace, extend, or toggle the current selection
- Display selected feature details and log selection events
