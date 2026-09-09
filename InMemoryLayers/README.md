# InMemoryLayers

InMemoryLayers demonstrates how to create and update map layers entirely in memory.

## Overview

It prepares the required world boundary sample data, loads it as a reference layer, and creates in-memory point, polyline, and polygon layers for cities, routes, and regions. Toolbar actions let the user add new memory features, clear and reset the memory layers, and zoom back to the full extent.

## GIS Workflow

This example shows the basic workflow for:

- Initialize a map viewer
- Download and load reference vector data
- Create point, line, and polygon layers in memory
- Add features dynamically at runtime
- Refresh the map after memory layer changes
