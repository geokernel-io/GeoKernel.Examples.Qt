# TopologyBatch

TopologyBatch demonstrates how to validate and process a collection of geometries in one GeoKernel topology workflow.

## Overview

It creates a collection of overlapping rectangles and diamonds entirely in memory, validates every polygon with `CheckShape`, and sends only valid geometries to `UnionOnList`. Running the batch highlights validated inputs and renders the combined result, while a details panel reports per-shape validity, vertices and extents together with aggregate counts, union geometry information, and elapsed processing time.

## GIS Workflow

This example shows how to:

- Create a collection of polygon geometries in memory
- Validate multiple geometries in a batch
- Skip invalid shapes before topology processing
- Build a `ShapeList` from valid polygons
- Combine a geometry collection with `UnionOnList`
- Measure topology processing duration
- Inspect source and result vertices, parts, and extents
- Render validated inputs and the combined result
