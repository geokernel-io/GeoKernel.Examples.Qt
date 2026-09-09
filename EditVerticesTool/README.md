# EditVerticesTool

EditVerticesTool demonstrates how to edit polyline and polygon vertices interactively in GeoKernel.

## Overview

It prepares the required world sample data, loads a reference map layer, and creates editable in-memory polyline and polygon layers. The Edit Vertices tool lets the user drag existing vertices, double-click a segment to insert a new vertex, and delete the active vertex while the side panel reports the updated vertex counts.

## GIS Workflow

This example shows how to:

- Create editable in-memory polyline and polygon layers
- Start edit sessions for multiple vector layers
- Activate the interactive Edit Vertices tool
- Move existing vertices by dragging
- Insert vertices into selected segments
- Delete the active vertex
- Monitor vertex counts and refresh the map after geometry changes
