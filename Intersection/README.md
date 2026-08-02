# Intersection

Intersection demonstrates how to extract the shared area of two overlapping polygon geometries with GeoKernel.

## Overview

It creates two differently shaped polygons entirely in memory, displays them with separate styles, and applies `GisTopology::Intersection` when the user runs the operation. The shared geometry is rendered as a highlighted overlay, while a details panel reports the input extents, result geometry type, part count, and intersection extent.

## GIS Workflow

This example shows how to:

- Create polygon geometries in memory
- Build and close polygon rings from vertices
- Display source polygons with separate styles
- Initialize the GeoKernel topology engine
- Calculate the intersection of two polygons
- Validate empty and non-empty topology results
- Inspect result geometry type, parts, and extent
- Render the shared polygon area as a styled overlay
