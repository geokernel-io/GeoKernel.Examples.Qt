# MoveFeatureTool

MoveFeatureTool demonstrates how to reposition editable vector features interactively in GeoKernel.

## Overview

It prepares the required world sample data, loads a reference map layer, and creates an editable in-memory point layer populated with attributed features. The user can select a point, activate the Move Feature tool, drag the selected feature to a new map position, inspect its updated coordinates in the table, and reset all points to their original locations.

## GIS Workflow

This example shows how to:

- Create and populate an editable in-memory point layer
- Select a feature on the map
- Activate the interactive Move Feature tool
- Drag a selected point to a new location
- Synchronize updated geometry with a feature table
- Reset the editable point layer
- Refresh the map after geometry changes
