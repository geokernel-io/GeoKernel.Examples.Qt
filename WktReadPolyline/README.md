# WktReadPolyline

WktReadPolyline demonstrates how to parse polyline geometry from Well-Known Text and display it with GeoKernel.

## Overview

It accepts a `LINESTRING` WKT string, parses it with `GisWktReader::readLineString`, and places the result in an editable in-memory polyline layer using EPSG:4326. The geometry is displayed over an OpenStreetMap basemap, while a details panel reports its part and vertex counts, extent, coordinate systems, and round-trip WKT output.

## GIS Workflow

This example shows how to:

- Read polyline geometry from a WKT string
- Create an editable in-memory polyline layer
- Assign a coordinate system to vector geometry
- Transform coordinates from EPSG:4326 to EPSG:3857
- Display parsed geometry over OpenStreetMap
- Replace an existing memory feature after new input
- Inspect polyline vertices and geographic extent
- Write the parsed polyline back to WKT
- Handle invalid WKT input interactively
