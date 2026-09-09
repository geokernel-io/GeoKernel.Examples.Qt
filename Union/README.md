# Union

Union demonstrates how to combine overlapping polygon geometries into a single topology result with GeoKernel.

## Overview

It creates two differently shaped polygons entirely in memory, displays them with separate styles, and applies `GisTopology::Union` when the user runs the operation. The resulting geometry is drawn as an overlay, while a details panel reports the input extents, result geometry type, part count, and combined extent.

## GIS Workflow

This example shows how to:

- Create polygon geometries in memory
- Build and close polygon rings from vertices
- Display source polygons with separate styles
- Initialize the GeoKernel topology engine
- Calculate the union of two polygon geometries
- Validate the returned topology result
- Inspect result geometry type, parts, and extent
- Render the combined polygon as a styled overlay
