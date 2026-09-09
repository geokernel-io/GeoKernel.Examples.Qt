# ConvexHullTwo

ConvexHullTwo demonstrates how to calculate one convex hull around two separate geometries with GeoKernel.

## Overview

It creates two concave polygons entirely in memory, displays each source with a distinct style, and applies `GisTopology::ConvexHull(left, right)` when the user runs the operation. The resulting hull encloses both source geometries and is rendered as a highlighted overlay, while a details panel compares the input vertex counts, source extents, hull parts, and hull extent.

## GIS Workflow

This example shows how to:

- Create multiple concave polygon geometries in memory
- Build and close polygon rings from vertices
- Display source geometries with separate styles
- Initialize the GeoKernel topology engine
- Calculate a shared convex hull from two geometries
- Validate the returned hull polygon
- Compare source and result geometry information
- Render the enclosing polygon as a styled overlay
