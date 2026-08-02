# ClickHitTest

ClickHitTest demonstrates how to identify the top-most vector feature at a clicked map position in GeoKernel.

## Overview

It prepares world boundaries, USA states, and city sample data, loads the layers with distinct styles, and runs a top-feature hit test whenever the user clicks the map. The selected result is highlighted and the side panel displays its layer, feature identifier, geometry type, extent, and attribute values.

## GIS Workflow

This example shows how to:

- Download and load multiple vector sample layers
- Switch between identify and pan tools
- Convert a map click into a feature hit test
- Query the top-most feature within a screen tolerance
- Select and highlight the hit feature
- Read feature geometry and extent information
- Display feature attributes in a details table
