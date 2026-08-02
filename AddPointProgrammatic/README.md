# AddPointProgrammatic

AddPointProgrammatic demonstrates how to add point features to an editable layer from application code in GeoKernel.

## Overview

It prepares the required world sample data, loads a reference map layer, and creates an editable in-memory point layer. Each Add Point action generates a sample coordinate and inserts it through the viewer editing API, while the remaining controls clear the temporary points or return the map to the full extent.

## GIS Workflow

This example shows how to:

- Create an editable in-memory point layer
- Start and activate a layer edit session
- Generate point coordinates programmatically
- Add points through the viewer editing API
- Track the point count after edits
- Clear temporary point features
- Refresh the map after editing operations
