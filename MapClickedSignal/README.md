# MapClickedSignal

MapClickedSignal demonstrates how to observe and inspect map click events emitted by a GeoKernel viewer.

## Overview

It prepares world boundaries, USA states, and city sample data, loads the layers with distinct styles, and connects to the viewer's `mapClicked` signal. Every click is recorded with its active tool, screen position, world coordinate, keyboard modifiers, and any top-most feature found at that location.

## GIS Workflow

This example shows how to:

- Download and load multiple vector sample layers
- Connect to the viewer map click signal
- Read the active tool for each click
- Convert and display screen and world coordinates
- Inspect keyboard modifiers
- Hit test the top-most feature at the click position
- Highlight the identified feature
- Record map click events in a timestamped table
