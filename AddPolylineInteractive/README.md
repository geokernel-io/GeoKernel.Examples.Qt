# AddPolylineInteractive

AddPolylineInteractive demonstrates how to draw polyline features interactively on a map in GeoKernel.

## Overview

It prepares the required world sample data, loads a reference map layer, and creates an editable in-memory polyline layer. The user can activate the Add Polyline tool, click multiple vertices on the map, finish each line with Enter or a double-click, pan the map, clear drawn lines, and return to the full extent.

## GIS Workflow

This example shows how to:

- Create an editable in-memory polyline layer
- Activate the polyline digitizing tool
- Add vertices interactively from map clicks
- Complete and store drawn polyline features
- Track the polyline count after edits
- Clear temporary line features
- Refresh the map after editing operations
