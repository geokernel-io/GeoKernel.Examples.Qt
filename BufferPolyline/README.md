# BufferPolyline

BufferPolyline demonstrates how to create a polygon corridor around a polyline with GeoKernel topology operations.

## Overview

It creates a multi-vertex polyline entirely in memory, generates a polygon buffer with `GisTopology::MakeBuffer`, and displays the source line above the resulting corridor. A runtime distance control rebuilds the buffer immediately, while a details panel reports source parts and vertices, buffer distance, result geometry type, part count, and extent.

## GIS Workflow

This example shows how to:

- Create a polyline geometry in memory
- Build a line from multiple vertices
- Initialize the GeoKernel topology engine
- Generate a polygon buffer around a polyline
- Configure buffer distance and segment count
- Recalculate topology results interactively
- Inspect source and result geometry information
- Display the source line and buffer corridor with separate styles
