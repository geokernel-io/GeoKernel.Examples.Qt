# WktReadPolygon

WktReadPolygon demonstrates how to parse polygon and multipolygon geometry from Well-Known Text and display it with GeoKernel.

## Overview

It accepts `POLYGON` or `MULTIPOLYGON` WKT input, parses the geometry with `GisWktReader`, and places the result in an editable in-memory polygon layer using EPSG:4326. The geometry is displayed over an OpenStreetMap basemap, while the controls provide sample inputs and the details panel reports geometry parts, vertices, extent, coordinate systems, and round-trip WKT output.

## GIS Workflow

This example shows how to:

- Read polygon geometry from a WKT string
- Parse both polygon and multipolygon input
- Create an editable in-memory polygon layer
- Assign a coordinate system to vector geometry
- Transform coordinates from EPSG:4326 to EPSG:3857
- Display parsed polygons over OpenStreetMap
- Replace existing memory features after new input
- Inspect polygon parts, vertices, and geographic extent
- Write parsed polygon geometry back to WKT
- Handle invalid WKT input interactively
