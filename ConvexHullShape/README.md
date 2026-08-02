# ConvexHullShape

ConvexHullShape demonstrates how to calculate the smallest convex polygon enclosing a single geometry with GeoKernel.

## Overview

It creates a concave polygon entirely in memory, displays the source with a distinct style, and applies `GisTopology::ConvexHull` when the user runs the operation. The resulting hull is rendered as a highlighted overlay, while a details panel compares the source and hull vertex counts, part counts, and geographic extents.

## GIS Workflow

This example shows how to:

- Create a concave polygon geometry in memory
- Build and close a polygon ring from vertices
- Display the source geometry with a custom style
- Initialize the GeoKernel topology engine
- Calculate the convex hull of a single geometry
- Validate the returned hull polygon
- Compare source and hull vertex counts
- Render the enclosing polygon as a styled overlay
