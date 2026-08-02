# DeleteFeature

DeleteFeature demonstrates how to remove editable vector features from a GeoKernel layer.

## Overview

It prepares the required world sample data, loads a reference map layer, and creates an editable in-memory point layer populated with attributed features. The user can select one or more points on the map, delete a single feature or all selected features, inspect the updated feature table, and reset the sample data at any time.

## GIS Workflow

This example shows how to:

- Create and populate an editable in-memory point layer
- Select individual or multiple vector features
- Delete one feature by its shape identifier
- Delete all currently selected features
- Keep the feature table and selection state synchronized
- Reset the editable sample layer
- Refresh the map after delete operations
