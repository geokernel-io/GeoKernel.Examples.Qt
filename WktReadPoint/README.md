# WktReadPoint

WktReadPoint demonstrates how to parse point geometry from Well-Known Text and display it with GeoKernel.

## Overview

It accepts a `POINT` WKT string, parses it with `GisWktReader::readPoint`, and places the result in an editable in-memory point layer using EPSG:4326. The coordinate is transformed to Web Mercator so the viewer can zoom to it over an OpenStreetMap basemap. A details panel reports the parsed longitude and latitude, projected coordinates, coordinate systems, and round-trip WKT output.

## GIS Workflow

This example shows how to:

- Read point geometry from a WKT string
- Create an editable in-memory point layer
- Assign a coordinate system to vector geometry
- Transform coordinates from EPSG:4326 to EPSG:3857
- Display parsed geometry over OpenStreetMap
- Replace an existing memory feature after new input
- Write the parsed point back to WKT
- Handle invalid WKT input interactively
