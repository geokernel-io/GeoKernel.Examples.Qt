# ShapeCentroid

ShapeCentroid demonstrates how to calculate a polygon centroid and a safe interior label point with GeoKernel.

## Overview

It creates a concave polygon entirely in memory, calculates both `centroid()` and `labelPoint()`, and renders the results as separately styled point overlays. The example highlights that an area-weighted centroid may fall outside a concave polygon, while the label point provides an interior position suitable for text placement. A details panel reports both coordinates and containment checks.

## GIS Workflow

This example shows how to:

- Create a concave polygon geometry in memory
- Calculate an area-weighted polygon centroid
- Calculate a safe interior label point
- Test whether result points are inside the polygon
- Display polygon and point results with distinct styles
- Label calculated geometry points
- Compare centroid and label point behavior
- Inspect calculated coordinates interactively
