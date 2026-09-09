# InsertVertex

InsertVertex demonstrates how to insert a new vertex into an editable polygon from application code in GeoKernel.

## Overview

It prepares the required world sample data, loads a reference map layer, and creates an editable in-memory polygon layer. The user can select the polygon, choose a geometry part and insertion index, then add a calculated midpoint vertex through the viewer editing API while the side panel reports the updated geometry details.

## GIS Workflow

This example shows how to:

- Create and populate an editable in-memory polygon layer
- Select an editable polygon feature
- Inspect polygon parts and vertex counts
- Choose a vertex insertion index
- Calculate a new point along a polygon segment
- Insert a vertex programmatically through the editing API
- Reset the geometry and refresh the map
