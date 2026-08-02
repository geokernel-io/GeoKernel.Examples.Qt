# TopologyFix

TopologyFix demonstrates how to clean and repair problematic line geometry with GeoKernel topology operations.

## Overview

It creates a deliberately messy multipart polyline entirely in memory with duplicate vertices, undersized parts, and parts that collapse during cleanup. The operation selector compares `FixShape`, `FixShapeEx`, and `ClearShape`, rendering each result with a distinct style while the details panel reports changes to part counts, vertex counts, per-part structure, and geographic extent.

## GIS Workflow

This example shows how to:

- Create a multipart polyline with common geometry problems
- Detect duplicate vertices and invalid short parts
- Initialize the GeoKernel topology engine
- Clean geometry with `FixShape`
- Preserve diagnostic parts with `FixShapeEx`
- Apply the `ClearShape` cleanup path
- Compare source and result part and vertex counts
- Visualize repaired geometry with distinct styles
