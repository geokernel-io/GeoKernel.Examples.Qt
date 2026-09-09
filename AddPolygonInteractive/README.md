# AddPolygonInteractive

AddPolygonInteractive demonstrates how to draw polygon features interactively on a map in GeoKernel.

## Overview

It prepares the required world sample data, loads a reference map layer, and creates an editable in-memory polygon layer. The user can activate the Add Polygon tool, click multiple vertices on the map, finish each polygon with Enter or a double-click, pan the map, clear drawn polygons, and return to the full extent.

## GIS Workflow

This example shows how to:

- Create an editable in-memory polygon layer
- Activate the polygon digitizing tool
- Add polygon vertices interactively from map clicks
- Complete and store drawn polygon features
- Track the polygon count after edits
- Clear temporary polygon features
- Refresh the map after editing operations
