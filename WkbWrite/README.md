# WkbWrite

WkbWrite demonstrates how to serialize interactively created geometry to Well-Known Binary with GeoKernel.

## Overview

It provides point, polyline, and polygon drawing modes over an OpenStreetMap basemap. After the user finishes drawing, the selected geometry is written with `GisWkbWriter`, and the details panel displays the writer API, geometry information, binary size, and hexadecimal WKB output.

## GIS Workflow

This example shows how to:

- Create point, polyline, and polygon geometry interactively
- Store drawn geometry in editable in-memory layers
- Select the appropriate WKB writer for each geometry type
- Serialize point geometry to WKB
- Serialize polyline geometry to WKB
- Serialize polygon geometry to WKB
- Display binary geometry as hexadecimal text
- Inspect WKB byte size and geometry details
- Reset and redraw geometry at runtime
