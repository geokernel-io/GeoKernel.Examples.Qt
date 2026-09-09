# SelectAdd

SelectAdd demonstrates how to build a multi-feature selection one map click at a time in GeoKernel.

## Overview

It prepares world boundaries, USA states, and city sample data, loads the layers with distinct styles, and uses feature hit testing to add the top-most feature under each click to the current selection. Ctrl+Click toggles an individual feature, while a side table tracks the selected layer, feature identifier, geometry type, and display name.

## GIS Workflow

This example shows how to:

- Download and load multiple vector sample layers
- Activate click-based feature identification
- Hit test the top-most feature at a screen position
- Add features to an existing selection
- Toggle individual features with a keyboard modifier
- Highlight selected features across multiple layers
- Display the current selection in a table
- Clear the complete selection set
