# GeoJsonRead

GeoJsonRead demonstrates how to parse GeoJSON geometry and display it with GeoKernel.

## Overview

It accepts Point, LineString, and Polygon GeoJSON input, parses the selected geometry with `GisGeoJsonReader`, and places it in a matching editable in-memory layer using EPSG:4326. The geometry is transformed for display over an OpenStreetMap basemap, while the details panel reports the input type, vertex information, extent, coordinate systems, and parsing result.

## GIS Workflow

This example shows how to:

- Read geometry from a GeoJSON string
- Parse Point, LineString, and Polygon geometry
- Create matching in-memory vector layers
- Assign a coordinate system to parsed geometry
- Transform coordinates from EPSG:4326 to EPSG:3857
- Display GeoJSON geometry over OpenStreetMap
- Replace previously parsed memory features
- Inspect geometry type, vertices, and extent
- Handle invalid GeoJSON input interactively
