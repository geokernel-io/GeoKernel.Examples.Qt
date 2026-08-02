# AllFeaturesAtPoint

AllFeaturesAtPoint demonstrates how to query every vector feature at a clicked map position in GeoKernel.

## Overview

It prepares world boundaries, USA states, and city sample data, loads the layers with distinct styles, and performs a multi-feature hit test whenever the user clicks the map. All intersecting results are listed in a table, and selecting a hit displays its layer, feature identifier, geometry information, extent, and attribute values.

## GIS Workflow

This example shows how to:

- Download and load multiple vector sample layers
- Switch between identify and pan tools
- Convert a map click into a multi-feature hit test
- Query all features within a screen tolerance
- List overlapping results from multiple layers
- Select and highlight an individual hit
- Display feature geometry and attribute details
