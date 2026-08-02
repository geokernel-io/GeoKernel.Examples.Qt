# AddPolylineProgrammatic

AddPolylineProgrammatic demonstrates how to add polyline features to an editable layer from application code in GeoKernel.

## Overview

It prepares the required world sample data, loads a reference map layer, and creates an editable in-memory polyline layer. Each Add Polyline action generates a sample vertex sequence and inserts it through the viewer editing API, while the remaining controls clear the temporary lines or return the map to the full extent.

## GIS Workflow

This example shows how to:

- Create an editable in-memory polyline layer
- Start and activate a layer edit session
- Generate polyline vertices programmatically
- Add polylines through the viewer editing API
- Track the polyline count after edits
- Clear temporary line features
- Refresh the map after editing operations
