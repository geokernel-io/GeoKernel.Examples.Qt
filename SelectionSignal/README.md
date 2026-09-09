# SelectionSignal

SelectionSignal demonstrates how to observe feature selection changes emitted by a GeoKernel viewer.

## Overview

It prepares world boundaries, USA states, and city sample data, loads the layers with distinct styles, and lets the user add or toggle selected features by clicking the map. Every selection update triggers the `selectionChanged(int count)` signal, refreshes the selected feature table, and appends a timestamped entry to a signal log.

## GIS Workflow

This example shows how to:

- Download and load multiple vector sample layers
- Select features with map hit testing
- Add or toggle features in a multi-layer selection
- Connect to the viewer selection change signal
- Read the current selected feature count
- Refresh UI state when selection changes
- Record selection events in a timestamped log
- Clear the complete selection set
