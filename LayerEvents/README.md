# LayerEvents

LayerEvents demonstrates how to observe and log layer-related events in a GeoKernel viewer.

## Overview

It prepares the required sample data, loads world boundaries, USA states, and USA city point layers, and records layer lifecycle signals such as add, remove, visibility change, order change, edit state, and spatial index events. The side panel lets the user add layers, remove selected layers, toggle visibility, reorder layers, refresh the map, and inspect the emitted event log.

## GIS Workflow

This example shows the basic workflow for:

- Initialize a map viewer
- Download and load multiple vector layers
- Connect to layer event signals
- Log layer add/remove/visibility/order changes
- Interact with the layer stack while observing emitted events
