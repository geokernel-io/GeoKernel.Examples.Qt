# Difference

Difference demonstrates how to subtract one polygon geometry from another with GeoKernel topology operations.

## Overview

It creates two overlapping polygons entirely in memory, displays them with separate styles, and applies `GisTopology::Difference` to calculate the portion of the left polygon that remains outside the right polygon. The result is rendered as a highlighted overlay, while a details panel reports the input extents, result geometry type, part count, and remaining extent.

## GIS Workflow

This example shows how to:

- Create polygon geometries in memory
- Build and close polygon rings from vertices
- Display source polygons with separate styles
- Initialize the GeoKernel topology engine
- Subtract one polygon from another
- Understand operation order as left minus right
- Validate empty and non-empty topology results
- Inspect and render the remaining geometry
