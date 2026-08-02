# ToleranceConfig

ToleranceConfig demonstrates how topology tolerance changes spatial operation results in GeoKernel.

## Overview

It creates a horizontal baseline and a nearby test point entirely in memory, then applies an adjustable tolerance through `GisTopology::SetTolerance`. Moving the toolbar slider updates the crossing and intersection tests immediately. A tolerance circle visualizes the active search distance, while colors and a details panel show whether the point is accepted as touching the line.

## GIS Workflow

This example shows how to:

- Create line and point geometries in memory
- Configure topology tolerance in map units
- Read the active topology tolerance
- Evaluate line-point crossings and intersection
- Adjust tolerance interactively at runtime
- Visualize tolerance as a radius around a point
- Compare accepted and rejected spatial results
- Refresh topology results after configuration changes
