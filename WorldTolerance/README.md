# WorldTolerance

WorldTolerance demonstrates how to query vector features with a tolerance expressed in map coordinates in GeoKernel.

## Overview

It prepares world boundaries, USA states, and city sample data, loads the layers with distinct styles, and lets the user configure a tolerance in degrees. Clicking the map runs a feature query around the clicked world coordinate, lists every matching feature, and displays the geometry and attributes of the selected result.

## GIS Workflow

This example shows how to:

- Download and load multiple vector sample layers
- Convert a map click to world coordinates
- Configure a tolerance in map coordinate units
- Query all features near a world point
- Compare query results at different tolerance values
- Select and highlight an individual result
- Display feature geometry and attribute details
