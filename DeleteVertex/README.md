# DeleteVertex

DeleteVertex demonstrates how to remove vertices from editable polygon geometry in GeoKernel.

## Overview

It prepares the required world sample data, loads a reference map layer, and creates an editable in-memory polygon layer. The user can delete the active vertex with the Edit Vertices tool or select the polygon, choose a geometry part and vertex index, and remove that vertex directly through the viewer editing API.

## GIS Workflow

This example shows how to:

- Create and populate an editable in-memory polygon layer
- Start and activate a polygon edit session
- Select an editable polygon feature
- Delete the active vertex interactively
- Inspect polygon parts and vertex indices
- Delete a vertex programmatically by part and index
- Reset the geometry and refresh the map
