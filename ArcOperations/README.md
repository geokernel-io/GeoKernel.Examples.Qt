# ArcOperations

ArcOperations demonstrates several polyline topology operations with GeoKernel.

## Overview

It creates multiple line geometries entirely in memory and presents three focused operations in one viewer: finding a matching arc, connecting adjoining arcs, and splitting an arc where another line crosses it. Running the operations highlights the matched, connected, and split results with distinct styles, while a details panel reports source vertices, extents, returned indices, and result part counts.

## GIS Workflow

This example shows how to:

- Create polyline geometries in memory
- Display query, source, and cutter arcs with separate styles
- Initialize the GeoKernel topology engine
- Find a matching arc in a line collection
- Connect adjoining polyline geometries
- Split an arc at crossing locations
- Inspect returned indices, vertices, and part counts
- Render topology results with distinct line styles
