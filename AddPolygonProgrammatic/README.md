# AddPolygonProgrammatic

AddPolygonProgrammatic demonstrates how to add polygon features to an editable layer from application code in GeoKernel.

## Overview

It prepares the required world sample data, loads a reference map layer, and creates an editable in-memory polygon layer. Each Add Polygon action generates a closed sample vertex sequence and inserts it through the viewer editing API, while the remaining controls clear the temporary polygons or return the map to the full extent.

## GIS Workflow

This example shows how to:

- Create an editable in-memory polygon layer
- Start and activate a layer edit session
- Generate closed polygon vertices programmatically
- Add polygons through the viewer editing API
- Track the polygon count after edits
- Clear temporary polygon features
- Refresh the map after editing operations
