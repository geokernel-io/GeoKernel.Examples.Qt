# EditAndSave

EditAndSave demonstrates how to create editable features and save them to a vector file in GeoKernel.

## Overview

It prepares the required world sample data, loads a reference map layer, and creates an editable in-memory point layer. The user can add points interactively, clear temporary edits, and export the current point collection to a shapefile in the application output directory while preserving the feature geometry and attributes.

## GIS Workflow

This example shows how to:

- Create an editable in-memory point layer
- Add point features interactively from map clicks
- Store feature geometry and attribute values
- Convert in-memory features to a file-backed vector layer
- Save edited features as a shapefile
- Report the generated output path
- Clear temporary features and refresh the map
