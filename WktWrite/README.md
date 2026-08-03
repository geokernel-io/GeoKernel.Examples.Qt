# WktWrite

WktWrite demonstrates how to serialize interactively created geometry to Well-Known Text with GeoKernel.

## Overview

It provides point, polyline, and polygon drawing modes over an OpenStreetMap basemap. After the user finishes drawing, the selected geometry is written with `GisWktWriter`, and the details panel displays the writer API, geometry information, and generated WKT text.

## GIS Workflow

This example shows how to:

- Create point, polyline, and polygon geometry interactively
- Store drawn geometry in editable in-memory layers
- Select the appropriate WKT writer for each geometry type
- Serialize point geometry to WKT
- Serialize polyline geometry to WKT
- Serialize polygon geometry to WKT
- Display generated geometry as readable text
- Inspect geometry type and writer API details
- Reset and redraw geometry at runtime
