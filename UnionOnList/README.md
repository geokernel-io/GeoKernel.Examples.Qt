# UnionOnList

UnionOnList demonstrates how to combine a collection of polygon geometries in a single GeoKernel topology operation.

## Overview

It creates five overlapping polygons entirely in memory, displays each source with a distinct style, and builds a `GisTopology::ShapeList` for `UnionOnList`. Running the operation creates one combined result overlay, while a details panel reports every source extent together with the result geometry type, part count, and overall extent.

## GIS Workflow

This example shows how to:

- Create multiple polygon geometries in memory
- Build and close polygon rings from vertices
- Display source polygons with distinct styles
- Populate a topology shape list
- Calculate the union of a geometry collection
- Validate the returned topology result
- Inspect source and result extents
- Render the combined geometry as a styled overlay
