# FindDeleteLoops

FindDeleteLoops demonstrates how to detect and remove self-intersecting polygon parts with GeoKernel topology operations.

## Overview

It creates a multipart polygon entirely in memory with one valid rectangular part and one self-intersecting bow-tie loop. Running `GisTopology::FindAndDeleteLoops` removes the invalid loop while preserving the valid polygon part. The source and cleaned result use distinct styles, and a details panel compares their part counts, vertex counts, per-part structure, and geographic extents.

## GIS Workflow

This example shows how to:

- Create a multipart polygon in memory
- Combine valid and self-intersecting polygon parts
- Display and label the source geometry
- Initialize the GeoKernel topology engine
- Find and remove invalid polygon loops
- Preserve valid parts during geometry cleanup
- Compare source and result parts and vertices
- Visualize the cleaned geometry with a distinct style
