# GeoJsonWrite

GeoJsonWrite demonstrates how to serialize map geometry as GeoJSON with GeoKernel.

## Overview

It displays an OpenStreetMap basemap with an editable in-memory polygon layer and lets the user draw polygon geometry interactively. When drawing is completed, the polygon is serialized with `GisGeoJsonWriter`, and the generated GeoJSON string is shown together with ring, vertex, and extent details.

## GIS Workflow

This example shows how to:

- Create an editable in-memory polygon layer
- Draw polygon geometry interactively
- Assign EPSG:4326 to editable geometry
- Serialize a polygon with `GisGeoJsonWriter`
- Display the generated GeoJSON string
- Inspect polygon rings, vertices, and extent
- Clear the current polygon and draw another
