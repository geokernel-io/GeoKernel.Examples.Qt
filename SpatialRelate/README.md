# SpatialRelate

SpatialRelate demonstrates how to inspect the spatial relationship between two geometries with GeoKernel.

## Overview

It creates two overlapping polygons entirely in memory, displays them with labels and distinct styles, and applies `GisTopology::Relate` when the user runs the operation. The example reports the DE-9IM relation matrix and evaluates standard spatial patterns such as equality, disjoint, intersect, within, contains, touch, cross, and overlap.

## GIS Workflow

This example shows how to:

- Create overlapping polygon geometries in memory
- Display and label comparison geometries
- Initialize the GeoKernel topology engine
- Calculate a DE-9IM spatial relation matrix
- Test a relation against standard spatial patterns
- Compare intersect, containment, touch, and overlap states
- Inspect source geometry extents
- Present spatial relationship results interactively
