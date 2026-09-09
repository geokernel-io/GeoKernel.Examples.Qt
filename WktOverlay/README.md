# WktOverlay

WktOverlay demonstrates how to parse multiple Well-Known Text geometry types and display them as map overlays with GeoKernel.

## Overview

It reads point, polyline, and polygon WKT strings with `GisWktReader`, creates a separate styled in-memory layer for each geometry type, and adds all three layers to the viewer. The resulting map shows how independent WKT geometries can be combined into a single overlay composition.

## GIS Workflow

This example shows how to:

- Read point geometry from WKT
- Read polyline geometry from WKT
- Read polygon geometry from WKT
- Create separate in-memory layers by geometry type
- Apply distinct point, line, and polygon styles
- Add multiple geometry layers to one viewer
- Combine independent WKT geometries as overlays
- Zoom the map to the combined geometry extent
