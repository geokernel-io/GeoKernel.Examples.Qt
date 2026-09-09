# SelectClear

SelectClear demonstrates how to clear a multi-feature selection in GeoKernel.

## Overview

It prepares world boundaries, USA states, and city sample data, loads the layers with distinct styles, and lets the user build a selection by clicking the top-most feature at each map position. The toolbar clear action removes the complete selection set, while Ctrl+Click demonstrates the same clear operation directly from the map interaction.

## GIS Workflow

This example shows how to:

- Download and load multiple vector sample layers
- Activate click-based feature identification
- Hit test the top-most feature at a screen position
- Add multiple features to the current selection
- Monitor the selected feature count
- Display selected features in a table
- Clear all selected features from a toolbar action
- Clear the selection from a modified map click
