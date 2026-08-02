# ExtentOperations

ExtentOperations demonstrates common geographic extent calculations and visualizes their results in GeoKernel.

## Overview

It creates two sample extents entirely in memory, calculates expanded and inflated variants, tests intersection and point containment, and draws each result as styled geometry in the viewer. A side panel reports the coordinates, dimensions, and boolean results so the numeric operations can be compared directly with their map representation.

## GIS Workflow

This example shows how to:

- Create geographic extents from coordinate bounds
- Read extent width and height values
- Expand one extent to include another
- Inflate an extent by horizontal and vertical distances
- Test whether two extents intersect
- Test whether an extent contains a point
- Convert extent bounds into polygon geometry
- Visualize extent operations with styled overlays
