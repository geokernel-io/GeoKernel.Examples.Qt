# SplitByArc

SplitByArc demonstrates how to divide a polygon into separate pieces using a polyline with GeoKernel topology operations.

## Overview

It creates a polygon and a crossing split arc entirely in memory, displays both source geometries with distinct styles, and applies `GisTopology::SplitByArc` when the user runs the operation. The resulting polygon pieces are rendered with separate colors, while a details panel reports the source parts and extents together with each result piece's part count and extent.

## GIS Workflow

This example shows how to:

- Create polygon and polyline geometries in memory
- Build polygon rings and multipart line geometry from vertices
- Display source geometries with separate styles
- Initialize the GeoKernel topology engine
- Split a polygon with a crossing arc
- Inspect the collection of returned geometries
- Compare source and result parts and extents
- Render each split polygon piece with a distinct style
