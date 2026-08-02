# BufferPoint

BufferPoint demonstrates how to create a polygon buffer around a point geometry with GeoKernel topology operations.

## Overview

It creates a source point entirely in memory, generates a polygon buffer with `GisTopology::MakeBuffer`, and displays both geometries with distinct styles. A runtime distance control rebuilds the buffer immediately, while a details panel reports the input distance, result geometry type, part count, and calculated extent.

## GIS Workflow

This example shows how to:

- Create a point geometry in memory
- Initialize the GeoKernel topology engine
- Generate a polygon buffer around a point
- Configure buffer distance and segment count
- Recalculate topology results interactively
- Inspect the returned geometry type and part count
- Read the buffer result extent
- Display source and result geometries with separate styles
