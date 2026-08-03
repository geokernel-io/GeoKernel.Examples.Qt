# WkbRead

WkbRead demonstrates how to parse Well-Known Binary geometry and display it with GeoKernel.

## Overview

It provides Point, LineString, and Polygon WKB presets as hexadecimal text, converts the selected input to binary, and parses it with `GisWkbReader`. The resulting geometry is placed in a matching editable in-memory layer using EPSG:4326 and displayed over an OpenStreetMap basemap, while the details panel reports geometry type, byte size, vertices, extent, and coordinate system information.

## GIS Workflow

This example shows how to:

- Convert hexadecimal text to WKB bytes
- Parse Point, LineString, and Polygon geometry
- Create matching in-memory vector layers
- Assign a coordinate system to parsed geometry
- Transform coordinates from EPSG:4326 to EPSG:3857
- Display WKB geometry over OpenStreetMap
- Inspect geometry type, byte size, vertices, and extent
- Replace previously parsed memory features
- Handle invalid WKB input interactively
