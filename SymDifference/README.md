# SymDifference

SymDifference demonstrates how to keep only the non-overlapping areas of two polygon geometries with GeoKernel topology operations.

## Overview

It creates two overlapping polygons entirely in memory, displays them with separate styles, and applies `GisTopology::SymmetricalDifference` when the user runs the operation. The shared area is removed while regions belonging to only one source polygon remain, and a details panel reports the input extents, result geometry type, part count, and output extent.

## GIS Workflow

This example shows how to:

- Create polygon geometries in memory
- Build and close polygon rings from vertices
- Display source polygons with separate styles
- Initialize the GeoKernel topology engine
- Calculate the symmetrical difference of two polygons
- Remove their shared intersection area
- Validate empty and non-empty topology results
- Inspect and render the remaining polygon regions
