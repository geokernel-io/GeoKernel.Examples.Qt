# FeatureAttributes

FeatureAttributes demonstrates how to inspect the complete attribute collection of a vector feature in GeoKernel.

## Overview

It prepares world boundaries, USA states, and city sample data, loads the layers with distinct styles, and identifies the top-most feature at a clicked map position. The selected feature is highlighted, while a side table displays its layer information, feature identifier, geometry type, extent, and every available attribute field and value.

## GIS Workflow

This example shows how to:

- Download and load multiple vector sample layers
- Activate click-based feature identification
- Hit test the top-most feature at a screen position
- Highlight the identified vector feature
- Read feature geometry and extent information
- Access the complete feature attribute collection
- Sort and display attribute fields and values
- Clear the details when no feature is found
