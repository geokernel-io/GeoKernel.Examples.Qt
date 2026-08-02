# TopologyCheck

TopologyCheck demonstrates how to validate polygon geometry before running topology operations with GeoKernel.

## Overview

It creates one valid polygon and one self-intersecting bow-tie polygon entirely in memory, displays both with labels, and applies `GisTopology::CheckShape` when the user runs the check. The viewer highlights the validation result for each geometry, while a details panel explains the input rings, reports their extents, and identifies which polygon is valid or invalid.

## GIS Workflow

This example shows how to:

- Create valid and self-intersecting polygons in memory
- Build and close polygon rings from vertices
- Label and style comparison geometries
- Initialize the GeoKernel topology engine
- Validate geometries with `CheckShape`
- Detect a self-intersecting polygon ring
- Compare geometry extents and validation results
- Identify invalid input before other topology operations
