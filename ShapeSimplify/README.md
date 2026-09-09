# ShapeSimplify

ShapeSimplify demonstrates how to reduce polygon complexity with GeoKernel while preserving its overall form.

## Overview

It creates a detailed polygon entirely in memory and applies the geometry's `simplify` operation using an adjustable tolerance. Moving the toolbar slider recalculates the Douglas-Peucker result immediately, allowing the source and simplified shapes to be compared visually. A details panel reports tolerance, source and result vertex counts, removed vertices, part structure, and extents.

## GIS Workflow

This example shows how to:

- Create a detailed polygon geometry in memory
- Display source vertices and polygon styling
- Simplify geometry with the Douglas-Peucker algorithm
- Control simplification tolerance interactively
- Compare source and simplified vertex counts
- Inspect removed vertices, parts, and extents
- Handle empty simplification results
- Refresh geometry rendering after tolerance changes
